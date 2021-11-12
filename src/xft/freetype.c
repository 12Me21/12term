#include "xftint.h"

FT_Library ft_library;

void font_init(void) {
	if (!FcInit())
		die("fontconfig init failed");
	if (FT_Init_FreeType(&ft_library))
		die("freetype init failed");
}

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

// linked list of all loaded fonts
static XftFont* fonts = NULL;

// List of all open files (each face in a file is managed separately)
static FontFile* xft_files = NULL;

// create a new FontFile from a filename and an id
static FontFile* get_file(const utf8* filename, int id) {
	FontFile* f;
	// search all files for one with a matching name/id
	for (f=xft_files; f; f=f->next) {
		if (!strcmp(f->filename, filename) && f->id == id) {
			++f->ref;
			if (XftDebug() & XFT_DBG_REF)
				print("FontFile %s/%d matches existing (%d)\n", filename, id, f->ref);
			goto found;
		}
	}
	// otherwise, create a new one
	f = malloc(sizeof(FontFile)+strlen(filename)+1);
	if (!f)
		return NULL;
	
	if (XftDebug() & XFT_DBG_REF)
		print("FontFile %s/%d matches new\n", filename, id);
	f->next = xft_files;
	xft_files = f;
	
	f->ref = 1;
	
	f->filename = (utf8*)&f[1];
	strcpy(f->filename, filename);
	f->id = id;
	
	f->face = NULL;
	f->xsize = 0;
	f->ysize = 0;
	f->matrix.xx = f->matrix.xy = f->matrix.yx = f->matrix.yy = 0;
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
		.xsize = 0,
		.ysize = 0,
		.matrix = {0,0,0,0},
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

// set the current size and matrix for a font
// todo: check how much lag this causes and whether we ever need to call this after loading a font?
static bool set_face(FontFile* f, FT_F26Dot6 xsize, FT_F26Dot6 ysize, FT_Matrix* matrix) {
	FT_Face face = f->face;
	
	if (f->xsize != xsize || f->ysize != ysize) {
		if (XftDebug() & XFT_DBG_GLYPH)
			print("Set face size to %dx%d (%dx%d)\n",
			       (int) (xsize >> 6), (int) (ysize >> 6), (int) xsize, (int) ysize);
		// Bitmap only faces must match exactly, so find the closest
		// one (height dominant search)
		if (!(face->face_flags & FT_FACE_FLAG_SCALABLE)) {
			FT_Bitmap_Size* best = &face->available_sizes[0];
			
			for (int i=1; i<face->num_fixed_sizes; i++) {
				FT_Bitmap_Size* si = &face->available_sizes[i];
				if (dist(ysize, si->y_ppem) < dist(ysize, best->y_ppem) ||
				    (dist(ysize, si->y_ppem) == dist(ysize, best->y_ppem) &&
				     dist(xsize, si->x_ppem) < dist(xsize, best->x_ppem))) {
					best = si;
				}
			}
			// Freetype 2.1.7 and earlier used width/height
			// for matching sizes in the BDF and PCF loaders.
			// This has been fixed for 2.1.8.  Because BDF and PCF
			// files have but a single strike per file, we can
			// simply try both sizes.
			if (FT_Set_Char_Size(face, best->x_ppem, best->y_ppem, 0, 0) != 0 &&
			    FT_Set_Char_Size(face, best->width<<6, best->height<<6, 0, 0) != 0)
				return false;
		} else {
			if (FT_Set_Char_Size(face, xsize, ysize, 0, 0))
				return false;
		}
		f->xsize = xsize;
		f->ysize = ysize;
	}
	if (!matrix_equal(&f->matrix, matrix)) {
		if (XftDebug() & XFT_DBG_GLYPH)
			print("Set face matrix to (%g,%g,%g,%g)\n",
			      (double)matrix->xx / 0x10000,
			      (double)matrix->xy / 0x10000,
			      (double)matrix->yx / 0x10000,
			      (double)matrix->yy / 0x10000);
		FT_Set_Transform(face, matrix, NULL);
		f->matrix = *matrix;
	}
	return true;
}

static void release_file(FontFile* f) {
	if (f->filename) {
		if (f->face) {
			FT_Done_Face(f->face);
		}
	}
	free(f);
}

FT_Face xft_lock_face(XftFont* font) {
	XftFontInfo* fi = &font->info;
	FT_Face face = fi->file->face;
	// Make sure the face is usable at the requested size
	// this is necessary if you have like
	// multiple fonts using the same face but different matricies i
	if (face && !set_face(fi->file, fi->xsize, fi->ysize, &fi->matrix))
		face = NULL;
	return face;
}

static FT_Int get_load_flags(const FcPattern* pattern, const XftFontInfo* fi) {
	FT_Int flags = FT_LOAD_DEFAULT | FT_LOAD_COLOR;
	
	// disable bitmaps when anti-aliasing or transforming glyphs
	FcBool bitmap = false;
	FcPatternGetBool(pattern, FC_EMBEDDED_BITMAP, 0, &bitmap);
	if ((!bitmap && fi->antialias) || fi->transform)
		flags |= FT_LOAD_NO_BITMAP;
	
	FcBool hinting = true;
	FcPatternGetBool(pattern, FC_HINTING, 0, &hinting);
	int hint_style = FC_HINT_FULL;
	FcPatternGetInteger(pattern, FC_HINT_STYLE, 0, &hint_style);
	
	// disable hinting if requested
	if (!hinting || hint_style == FC_HINT_NONE)
		flags |= FT_LOAD_NO_HINTING;
	
	// Figure out the load target, which modifies the hinting
	// behavior of FreeType based on the intended use of the glyphs.
	if (fi->antialias) {
		if (FC_HINT_NONE < hint_style && hint_style < FC_HINT_FULL) {
			flags |= FT_LOAD_TARGET_LIGHT;
		} else {
			// autohinter will snap stems to integer widths, when
			// the LCD targets are used.
			switch (fi->rgba) {
			case FC_RGBA_RGB:
			case FC_RGBA_BGR:
				flags |= FT_LOAD_TARGET_LCD;
				break;
			case FC_RGBA_VRGB:
			case FC_RGBA_VBGR:
				flags |= FT_LOAD_TARGET_LCD_V;
				break;
			}
		}
	} else
		flags |= FT_LOAD_TARGET_MONO;
	
	// force autohinting if requested
	FcBool autohint = false;
	FcPatternGetBool(pattern, FC_AUTOHINT, 0, &autohint);
	if (autohint)
		flags |= FT_LOAD_FORCE_AUTOHINT;
	
	// disable global advance width (for broken DynaLab TT CJK fonts)
	FcBool global_advance = true;
	FcPatternGetBool(pattern, FC_GLOBAL_ADVANCE, 0, &global_advance);
	if (!global_advance)
		flags |= FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;
	
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
	
	fi->ysize = (FT_F26Dot6)(dsize * 64.0);
	fi->xsize = (FT_F26Dot6)(dsize * aspect * 64.0);
	
	if (XftDebug() & XFT_DBG_OPEN)
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
	FcMatrix* font_matrix;
	switch (FcPatternGetMatrix(pattern, FC_MATRIX, 0, &font_matrix)) {
	case FcResultNoMatch:
		fi->matrix.xx = fi->matrix.yy = 0x10000;
		fi->matrix.xy = fi->matrix.yx = 0;
		break;
	case FcResultMatch:
		fi->matrix.xx = 0x10000L * font_matrix->xx;
		fi->matrix.yy = 0x10000L * font_matrix->yy;
		fi->matrix.xy = 0x10000L * font_matrix->xy;
		fi->matrix.yx = 0x10000L * font_matrix->yx;
		break;
	default:
		goto bail1;
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
	if (XftDebug() & XFT_DBG_CACHE)
		print("New font %s/%d size %dx%d\n",
		      fi->file->filename, fi->file->id,
		      (int) fi->xsize >> 6, (int) fi->ysize >> 6);
	
	FontFile* f = fi->file;
	
	if (!f->face) {
		if (XftDebug() & XFT_DBG_REF)
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
	if (color)
		font->format = PictStandardARGB32;
	else if (antialias) {
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
	
	int ascent, descent, height;
	// Public fields
	if (fi->transform) {
		FT_Vector vector;
		
		vector.x = 0;
		vector.y = face->size->metrics.descender;
		FT_Vector_Transform(&vector, &fi->matrix);
		descent = -(vector.y >> 6);
		
		vector.x = 0;
		vector.y = face->size->metrics.ascender;
		FT_Vector_Transform(&vector, &fi->matrix);
		
		ascent = vector.y >> 6;
		
		if (fi->minspace)
			height = ascent + descent;
		else {
			vector.x = 0;
			vector.y = face->size->metrics.height;
			FT_Vector_Transform(&vector, &fi->matrix);
			height = vector.y >> 6;
		}
	} else {
		descent = -(face->size->metrics.descender >> 6);
		ascent = face->size->metrics.ascender >> 6;
		if (fi->minspace)
			height = ascent + descent;
		else
			height = face->size->metrics.height >> 6;
	}
	font->ascent = ascent;
	font->descent = descent;
	font->height = height;
	
	if (fi->char_width)
		font->max_advance_width = fi->char_width;
	else {
		if (fi->transform) {
			FT_Vector vector;
			vector.x = face->size->metrics.max_advance;
			vector.y = 0;
			FT_Vector_Transform(&vector, &fi->matrix);
			font->max_advance_width = vector.x >> 6;
		} else
			font->max_advance_width = face->size->metrics.max_advance >> 6;
	}
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
