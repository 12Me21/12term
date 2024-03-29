#include "xftint.h"

FT_Library ft_library;

// linked list of all loaded fonts
static XftFont* fonts = NULL;

// Many fonts can share the same underlying face data; this
// structure references that.  Note that many faces may in fact
// live in the same font file; that is irrelevant to this structure
// which is concerned only with the individual faces themselves
typedef struct FontFile {
	struct FontFile* next;
	int ref; // number of font infos using this file
	
	utf8* filename;
	int id; // font index within that file
	
	FT_F26Dot6 xsize;	// current xsize setting
	FT_F26Dot6 ysize;	// current ysize setting
	FT_Matrix matrix;	// current matrix setting
	
	FT_Face face; // pointer to face; only valid when lock
} FontFile;

// Linked list of all open files (each face in a file is managed separately)
static FontFile* xft_files = NULL;

FcConfig* defc;

void font_init(void) {
	if (!FcInit())
		die("fontconfig init failed");
	//defc = FcInitLoadConfig();
	if (FT_Init_FreeType(&ft_library))
		die("freetype init failed");
}

// create a new FontFile from a filename and an id
static FontFile* get_file(const utf8* filename, int id) {
	FontFile* f;
	// search all files for one with a matching name/id
	for (f=xft_files; f; f=f->next) {
		if (!strcmp(f->filename, filename) && f->id == id) {
			if (DEBUG.ref)
				print("FontFile %s/%d matches existing (%d)\n", filename, id, f->ref);
			++f->ref;
			goto found;
		}
	}
	if (DEBUG.ref)
		print("FontFile %s/%d matches new\n", filename, id);
	
	// otherwise, create a new one
	f = malloc(sizeof(FontFile)+strlen(filename)+1);
	
	*f = (FontFile){
		.next = xft_files,
		.ref = 1,
		.filename = (utf8*)&f[1], // points to the extra memory we allocated after the struct
		.id = id,
		.face = NULL,
		// rest of fields are 0
	};
	strcpy(f->filename, filename);
	xft_files = f;
 found:
	return f;
}

// create a new FontFile from an existing face
static FontFile* make_face_file(FT_Face face) {
	FontFile* f = malloc(sizeof(FontFile));
	if (!f)
		return NULL;
	*f = (FontFile){
		.next = NULL,
		.ref = 1,
		.filename = NULL,
		.id = 0,
		.face = face,
		// rest are 0
	};
	return f;
}

static bool matrix_equal(FT_Matrix* a, FT_Matrix* b) {
	return a->xx==b->xx && a->yy==b->yy && a->xy==b->xy && a->yx==b->yx;
}

static FT_F26Dot6 dist(FT_F26Dot6 a, FT_F26Dot6 b) {
	if (a>b)
		return a-b;
	return b-a;
}

// for debug only
static double f26dot6_to_float(FT_F26Dot6 n) {
	return (double)n / (1<<6);
}
// for debug only
static double fixed_to_float(FT_Fixed n) {
	return (double)n / (1<<16);
}

// set the current size and matrix for a font
static bool set_face(FontFile* f, FT_F26Dot6 xsize, FT_F26Dot6 ysize, FT_Matrix* matrix) {
	FT_Face face = f->face;
	
	if (f->xsize != xsize || f->ysize != ysize) {
		if (DEBUG.glyph)
			print("Set face size to %g×%g\n", f26dot6_to_float(xsize), f26dot6_to_float(ysize));
		
		FT_F26Dot6 rx = xsize, ry = ysize;
		// Bitmap only faces must match exactly, so find the closest
		// one (height dominant search)
		if (!(face->face_flags & FT_FACE_FLAG_SCALABLE)) {
			FT_Bitmap_Size* best = &face->available_sizes[0];
			
			for (int i=1; i<face->num_fixed_sizes; i++) {
				FT_Bitmap_Size* si = &face->available_sizes[i];
				if (dist(ysize, si->y_ppem) < dist(ysize, best->y_ppem) || (dist(ysize, si->y_ppem) == dist(ysize, best->y_ppem) && dist(xsize, si->x_ppem) < dist(xsize, best->x_ppem)))
					best = si;
			}
			rx = best->x_ppem;
			ry = best->y_ppem;
		}
		
		if (FT_Set_Char_Size(face, rx, ry, 0, 0))
			return false;
		f->xsize = xsize;
		f->ysize = ysize;
	}
	
	if (!matrix_equal(&f->matrix, matrix)) {
		if (DEBUG.glyph)
			print("Set face matrix to (%g,%g,%g,%g)\n",
				fixed_to_float(matrix->xx),
				fixed_to_float(matrix->xy),
				fixed_to_float(matrix->yx),
				fixed_to_float(matrix->yy)
			);
		FT_Set_Transform(face, matrix, NULL);
		f->matrix = *matrix;
	}
	return true;
}

static void release_file(FontFile* f) {
	if (f->filename) {
		if (f->face)
			FT_Done_Face(f->face);
	}
	free(f);
}

FT_Face xft_lock_face(XftFont* font) {
	XftFontInfo* fi = &font->info;
	FT_Face face = fi->file->face;
	// Make sure the face is usable at the requested size
	if (face && !set_face(fi->file, fi->xsize, fi->ysize, &fi->matrix))
		face = NULL;
	return face;
}

static FT_Int get_load_flags(const FcPattern* pattern, const XftFontInfo* fi) {
	print("Setting fc load flags:\n");
	
	FT_Int flags = FT_LOAD_DEFAULT | FT_LOAD_COLOR;
	
	// disable bitmaps when anti-aliasing or transforming glyphs
	FcBool bitmap = false;
	FcPatternGetBool(pattern, FC_EMBEDDED_BITMAP, 0, &bitmap);
	if ((!bitmap && fi->antialias) || fi->transform) {
		flags |= FT_LOAD_NO_BITMAP;
		print("\t+ NO BITMAP\n");
	}
	
	FcBool hinting = true;
	FcPatternGetBool(pattern, FC_HINTING, 0, &hinting);
	int hint_style = FC_HINT_FULL;
	FcPatternGetInteger(pattern, FC_HINT_STYLE, 0, &hint_style);
	
	// disable hinting if requested
	if (!hinting || hint_style == FC_HINT_NONE) {
		flags |= FT_LOAD_NO_HINTING;
		print("+ NO HINTING\n");
	}
	
	// this only applies to the autohinter
	FT_Int target = FT_LOAD_TARGET_LIGHT;
	if (!fi->antialias) {
		target = FT_LOAD_TARGET_MONO;
		print("+ TARGET MONO\n");
	} else {
		/// if (hint_style <= FC_HINT_NONE || hint_style >= FC_HINT_FULL) 
		switch (fi->rgba) {
		case FC_RGBA_RGB:
		case FC_RGBA_BGR:
			target = FT_LOAD_TARGET_LCD;
			print("+ TARGET LCD\n");
			break;
		case FC_RGBA_VRGB:
		case FC_RGBA_VBGR:
			target = FT_LOAD_TARGET_LCD_V;
			print("+ TARGET LCD V\n");
			break;
		}
	}
	flags |= target;
	
	// force autohinting if requested
	FcBool autohint = false;
	FcPatternGetBool(pattern, FC_AUTOHINT, 0, &autohint);
	if (autohint) {
		flags |= FT_LOAD_FORCE_AUTOHINT;
		print("+ FORCE AUTOHINT\n");
	}
	
	return flags;
}

static bool font_info_fill(const FcPattern* pattern, XftFontInfo* fi) {
	// Find the associated file
	FcChar8* filename = NULL;
	FcPatternGetString(pattern, FC_FILE, 0, &filename);
	int id = 0;
	FcPatternGetInteger(pattern, FC_INDEX, 0, &id);
	
	FT_Face face;
	if (filename) {
		// I think this is the only one which is used?
		fi->file = get_file((utf8*)filename, id);
	} else if (FcPatternGetFTFace(pattern, FC_FT_FACE, 0, &face) == FcResultMatch && face) {
		fi->file = make_face_file(face);
	} else {
		fi->file = NULL;
	}
	
	if (!fi->file)
		goto bail0;
	
	// Compute pixel size
	double dsize;
	if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &dsize) != FcResultMatch) {
		print("no size\n");
		goto bail1;
	}
	double aspect = 1.0;
	FcPatternGetDouble(pattern, FC_ASPECT, 0, &aspect);
	
	fi->ysize = dsize * (1<<6);
	fi->xsize = dsize * aspect * (1<<6);
	
	if (DEBUG.open)
		print("font_info_fill: %s: %d (%g pixels)\n",
		      (filename ? (utf8*)filename : "<none>"), id, dsize);
	
	// Get antialias value
	fi->antialias = true;
	FcPatternGetBool(pattern, FC_ANTIALIAS, 0, &fi->antialias);
	
	// Get rgba value
	fi->rgba = FC_RGBA_UNKNOWN;
	FcPatternGetInteger(pattern, FC_RGBA, 0, &fi->rgba);
	
	// Get lcd_filter value
	fi->lcd_filter = FC_LCD_DEFAULT;
	FcPatternGetInteger(pattern, FC_LCD_FILTER, 0, &fi->lcd_filter);
	
	// Get matrix and transform values
	// - why are we multiplying all this?
	FcMatrix* font_matrix;
	if (FcPatternGetMatrix(pattern, FC_MATRIX, 0, &font_matrix) == FcResultMatch) {
		fi->matrix.xx = 0x10000L * font_matrix->xx;
		fi->matrix.yy = 0x10000L * font_matrix->yy;
		fi->matrix.xy = 0x10000L * font_matrix->xy;
		fi->matrix.yx = 0x10000L * font_matrix->yx;
	} else {
		fi->matrix.xx = fi->matrix.yy = 0x10000;
		fi->matrix.xy = fi->matrix.yx = 0;
	}
	
	fi->transform = (fi->matrix.xx != 0x10000 || fi->matrix.xy != 0 || fi->matrix.yx != 0 || fi->matrix.yy != 0x10000);
	
	fi->load_flags = get_load_flags(pattern, fi);
	
	fi->embolden = false;
	FcPatternGetBool(pattern, FC_EMBOLDEN, 0, &fi->embolden);
	
	// Get requested spacing value
	fi->spacing = FC_PROPORTIONAL;
	FcPatternGetInteger(pattern, FC_SPACING, 0, &fi->spacing);
	
	// Check for minspace
	fi->minspace = false;
	FcPatternGetBool(pattern, FC_MINSPACE, 0, &fi->minspace);
	
	// Check for fixed pixel spacing
	fi->char_width = 0;
	FcPatternGetInteger(pattern, FC_CHAR_WIDTH, 0, &fi->char_width);
	if (fi->char_width)
		fi->spacing = FC_MONO;
	
	// All done
	return true;

 bail1:
	release_file(fi->file);
	fi->file = NULL;
 bail0:
	return false;
}

static XftFont* XftFontOpenInfo(FcPattern* pattern, XftFontInfo* fi) {
	// No existing font, create another.
	if (DEBUG.cache)
		print("New font %s/%d size %g×%g\n",
			fi->file->filename, fi->file->id,
			f26dot6_to_float(fi->xsize), f26dot6_to_float(fi->ysize)
		);
	
	FontFile* f = fi->file;
	
	if (!f->face) {
		if (DEBUG.ref)
			print("Loading file %s/%d\n", f->filename, f->id);
		FT_New_Face(ft_library, f->filename, f->id, &f->face);
		
		f->xsize = 0;
		f->ysize = 0;
		f->matrix.xx = f->matrix.xy = f->matrix.yx = f->matrix.yy = 0;
	}
	FT_Face face = f->face;
	
	if (!face)
		goto bail0;
	
	if (!set_face(fi->file, fi->xsize, fi->ysize, &fi->matrix))
		goto bail1;
	
	XftFont* font;
	ALLOC(font, 1);
	
	// Get the set of Unicode codepoints covered by the font.
	// If the incoming pattern doesn't provide this data, go
	// off and compute it.  Yes, this is expensive, but it's
	// required to map Unicode to glyph indices.
	FcCharSet* charset;
	if (FcPatternGetCharSet(pattern, FC_CHARSET, 0, &charset) == FcResultMatch)
		charset = FcCharSetCopy(charset);
	else
		charset = FcFreeTypeCharSet(face, FcConfigGetBlanks(NULL));
	
	bool antialias = fi->antialias;
	if (!(face->face_flags & FT_FACE_FLAG_SCALABLE))
		antialias = false;
	
	bool color = FT_HAS_COLOR(face);
	// Find the appropriate picture format
	// we should probably cache the format list
	if (color) {
		font->format = PictStandardARGB32;
	} else if (antialias) {
		/* wait, for normal glyphs why isn't this just A8? */
		/* OH or is it actually being used as  storing alpha in the rgb channels?..  i dont understand.. */
		switch (fi->rgba) {
		case FC_RGBA_RGB:
		case FC_RGBA_BGR:
		case FC_RGBA_VRGB:
		case FC_RGBA_VBGR:
			font->format = PictStandardARGB32;
			break;
		default:
			font->format = PictStandardA8;
			break;
		}
	} else
		font->format = PictStandardA1;
	
	// Public fields
	
	FT_F26Dot6 descent = -face->size->metrics.descender;
	FT_F26Dot6 ascent = face->size->metrics.ascender;
	FT_F26Dot6 height = face->size->metrics.height;
	FT_F26Dot6 char_width = face->size->metrics.max_advance;
	if (fi->minspace)
		height = ascent + descent;
	if (fi->transform) {
		descent = FT_MulFix(descent, fi->matrix.yy);
		ascent = FT_MulFix(descent, fi->matrix.yy);
		height = FT_MulFix(height, fi->matrix.yy);
		char_width = FT_MulFix(char_width, fi->matrix.xx);
	}
	if (fi->char_width)
		char_width = fi->char_width<<6;
	// todo: why dont we just keep these as fixed, hm?
	font->ascent = ascent>>6;
	font->descent = descent>>6;
	font->height = height>>6;
	font->max_advance_width = char_width>>6;
	
	font->charset = charset;
	font->pattern = pattern;
	
	// insert into global linked list
	font->next = fonts;
	fonts = font;
	
	// Copy the info over
	font->info = *fi;
	
	// reset the antialias field.  It can't
	// be set correctly until the font is opened,
	// which doesn't happen in font_info_fill
	font->info.antialias = antialias;
	
	// Set color value, which is only known once the
	// font was loaded
	font->info.color = color;
	
	// bump XftFile reference count
	font->info.file->ref++;
	
	return font;
	
	FcCharSetDestroy(charset);
 bail1:
 bail0:
	return NULL;
}

XftFont* XftFontOpenPattern(FcPattern* pattern) {
	XftFontInfo info;
	if (!font_info_fill(pattern, &info))
		return NULL;
	
	XftFont* font = XftFontOpenInfo(pattern, &info);
	return font;
}

static void XftFontDestroy(XftFont* font) {
	// Free the pattern and the charset
	FcPatternDestroy(font->pattern);
	FcCharSetDestroy(font->charset);
	
	// Finally, free the font structure
	free(font);
}

void close_all(void) {
	while (xft_files) {
		FontFile* next = xft_files->next;
		release_file(xft_files);
		xft_files = next;
	}
	while (fonts) {
		XftFont* next = fonts->next;
		XftFontDestroy(fonts);
		fonts = next;
	}
}
