#include "xftint.h"

FT_Library ft_library;

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

// A hash table translates Unicode values into glyph indexes
typedef struct UcsHash {
	Char ucs4;
	FT_UInt glyph;
} UcsHash;

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
				printf("FontFile %s/%d matches existing (%d)\n", filename, id, f->ref);
			goto found;
		}
	}
	// otherwise, create a new one
	f = XftMalloc(XFT_MEM_FILE, sizeof(FontFile)+strlen(filename)+1);
	if (!f)
		return NULL;
	
	if (XftDebug() & XFT_DBG_REF)
		printf("FontFile %s/%d matches new\n", filename, id);
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
	FontFile* f = XftMalloc(XFT_MEM_FILE, sizeof(FontFile));
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

static bool set_face(FontFile* f, FT_F26Dot6 xsize, FT_F26Dot6 ysize, FT_Matrix* matrix) {
	FT_Face face = f->face;
	
	if (f->xsize != xsize || f->ysize != ysize) {
		if (XftDebug() & XFT_DBG_GLYPH)
			printf("Set face size to %dx%d (%dx%d)\n",
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
			printf ("Set face matrix to (%g,%g,%g,%g)\n",
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
	if (--f->ref != 0)
		return;
	if (f->filename) {
		for (FontFile** prev = &xft_files; *prev; prev = &(*prev)->next) {
			if (*prev == f) {
				*prev = f->next;
				break;
			}
		}
		if (f->face)
			FT_Done_Face(f->face);
	}
	XftMemFree(XFT_MEM_FILE, sizeof(FontFile) + (f->filename ? strlen(f->filename)+1 : 0));
	free(f);
}

// Find a prime larger than the minimum reasonable hash size
static Char xft_sqrt(Char a) {
	Char l = 2;
	Char h = a/2;
	while ((h-l) > 1) {
		Char m = (h+l) >> 1;
		if (m * m < a)
			l = m;
		else
			h = m;
	}
	return h;
}

static bool is_prime(Char i) {
	if (i < 2)
		return false;
	if ((i&1) == 0) {
		if (i == 2)
			return true;
		return false;
	}
	Char l = xft_sqrt(i) + 1;
	for (Char t = 3; t <= l; t += 2)
		if (i % t == 0)
			return false;
	return true;
}

static Char hash_size(Char num_unicode) {
	// at least 31.25% extra space
	//return 1000;
	Char hash = num_unicode + num_unicode/4 + num_unicode/16;
	
	if ((hash&1) == 0)
		hash++;
	while (!is_prime(hash))
		hash += 2;
	return hash;
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

static bool XftFontInfoFill(const FcPattern* pattern, XftFontInfo* fi) {
	// Find the associated file
	FcChar8* filename = NULL;
	FcPatternGetString(pattern, FC_FILE, 0, &filename);
	
	int id = 0;
	FcPatternGetInteger(pattern, FC_INDEX, 0, &id);
	
	FT_Face face;
	if (filename)
		fi->file = get_file((utf8*)filename, id);
	else if (FcPatternGetFTFace(pattern, FC_FT_FACE, 0, &face) == FcResultMatch && face)
		fi->file = make_face_file(face);
	if (!fi->file)
		goto bail0;
	
	// Compute pixel size
	double dsize;
	if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &dsize) != FcResultMatch)
		goto bail1;
	double aspect = 1.0;
	FcPatternGetDouble(pattern, FC_ASPECT, 0, &aspect);
	
	fi->ysize = (FT_F26Dot6)(dsize * 64.0);
	fi->xsize = (FT_F26Dot6)(dsize * aspect * 64.0);
	
	if (XftDebug() & XFT_DBG_OPEN)
		printf("XftFontInfoFill: %s: %d (%g pixels)\n",
		       (filename ? (utf8*)filename : "<none>"), id, dsize);
	
	fi->antialias = true;
	// Get antialias value
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

static void XftFontInfoEmpty(XftFontInfo* fi) {
	if (fi->file)
		release_file(fi->file);
}

static XftFont* XftFontOpenInfo(FcPattern* pattern, XftFontInfo* fi) {
	// No existing font, create another.
	if (XftDebug() & XFT_DBG_CACHE)
		printf("New font %s/%d size %dx%d\n",
		       fi->file->filename, fi->file->id,
		       (int) fi->xsize >> 6, (int) fi->ysize >> 6);
	int max_glyph_memory;
	if (FcPatternGetInteger(pattern, XFT_MAX_GLYPH_MEMORY, 0, &max_glyph_memory) != FcResultMatch)
		max_glyph_memory = XFT_FONT_MAX_GLYPH_MEMORY;
	
	FontFile* f = fi->file;
	
	if (!f->face) {
		if (XftDebug() & XFT_DBG_REF)
			printf("Loading file %s/%d\n", f->filename, f->id);
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
	XRenderPictFormat* format;
	// Find the appropriate picture format
	// we should probably cache the format list
	if (color)
		format = XRenderFindStandardFormat(W.d, PictStandardARGB32);
	else if (antialias) {
		switch (fi->rgba) {
		case FC_RGBA_RGB:
		case FC_RGBA_BGR:
		case FC_RGBA_VRGB:
		case FC_RGBA_VBGR:
			format = XRenderFindStandardFormat(W.d, PictStandardARGB32);
			break;
		default:
			format = XRenderFindStandardFormat(W.d, PictStandardA8);
			break;
		}
	} else
		format = XRenderFindStandardFormat(W.d, PictStandardA1);
	if (!format)
		goto bail2;
	
	Char num_unicode = 0;
	Char hash_length = 0;
	Char rehash_value = 0;
	if (charset) {
		num_unicode = FcCharSetCount(charset);
		hash_length = hash_size(num_unicode);
		print("hash table size: %d\n", hash_length);
		rehash_value = hash_length-2;
	}
	
	// Sometimes the glyphs are numbered 1..n, other times 0..n-1,
	// accept either numbering scheme by making room in the table
	int num_glyphs = face->num_glyphs + 1;
	int alloc_size = sizeof(XftFont) +
	                 num_glyphs*sizeof(XftGlyph*) +
	                 hash_length*sizeof(UcsHash);
	
	XftFont* font = XftMalloc(XFT_MEM_FONT, alloc_size);
	if (!font)
		goto bail2;
	
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
	
	// Management fields
	font->ref = 1;
	
	font->next = info.fonts;
	info.fonts = font;
	
	// Copy the info over
	font->info = *fi;
	
	// reset the antialias field.  It can't
	// be set correctly until the font is opened,
	// which doesn't happen in XftFontInfoFill
	font->info.antialias = antialias;
	
	// Set color value, which is only known once the
	// font was loaded
	font->info.color = color;
	
	// bump XftFile reference count
	font->info.file->ref++;
	
	// Per glyph information
	font->glyphs = (XftGlyph**)&font[1];
	memset(font->glyphs, '\0', num_glyphs*sizeof(XftGlyph*));
	font->num_glyphs = num_glyphs;
	// Unicode hash table information
	font->hash_table = (UcsHash*)(font->glyphs+font->num_glyphs);
	for (int i=0; i<hash_length; i++) {
		font->hash_table[i].ucs4 = -1;
		font->hash_table[i].glyph = 0;
	}
	font->hash_length = hash_length;
	font->rehash_value = rehash_value;
	// X specific fields
	font->format = format;
	font->glyphset = XRenderCreateGlyphSet(W.d, font->format);
	// Glyph memory management fields
	font->glyph_memory = 0;
	font->max_glyph_memory = max_glyph_memory;
	
	return font;
	
 bail2:
	FcCharSetDestroy(charset);
 bail1:
 bail0:
	return NULL;
}

XftFont* XftFontOpenPattern(FcPattern* pattern) {
	XftFontInfo info;
	if (!XftFontInfoFill(pattern, &info))
		return NULL;
	
	XftFont* font = XftFontOpenInfo(pattern, &info);
	XftFontInfoEmpty(&info);
	return font;
}

static void XftFontDestroy(XftFont* font) {
	/* note reduction in memory use */
	info.glyph_memory -= font->glyph_memory;
	/* Clean up the info */
	XftFontInfoEmpty(&font->info);
	/* Free the glyphset */
	if (font->glyphset)
		XRenderFreeGlyphSet(W.d, font->glyphset);
	/* Free the glyphs */
	FOR (i, font->num_glyphs) {
		XftGlyph* xftg = font->glyphs[i];
		free(xftg);
	}
	
	/* Free the pattern and the charset */
	FcPatternDestroy(font->pattern);
	FcCharSetDestroy(font->charset);
	
	/* Finally, free the font structure */
	XftMemFree(XFT_MEM_FONT, sizeof(XftFont) +
	           font->num_glyphs * sizeof(XftGlyph*) +
	           font->hash_length * sizeof(UcsHash));
	free(font);
}
// i think i've been incorrectly marking these as static
static XftFont* XftFontFindNthUnref(int n) {
	XftFont* font;
	for (font=info.fonts; font; font=font->next) {
		if (!font->ref && !n--)
			break;
	}
	return font;
}

// different from xft_font_manage_memory
static void font_manage_memory(void) {
	while (info.num_unref_fonts > info.max_unref_fonts) {
		XftFont* font = XftFontFindNthUnref(rand() % info.num_unref_fonts);
		if (XftDebug() & XFT_DBG_CACHE)
			printf("freeing unreferenced font %s/%d size %dx%d\n",
			       font->info.file->filename, font->info.file->id,
			       (int)font->info.xsize >> 6, (int)font->info.ysize >> 6);
		
		XftFont** prev;
		/* Unhook from display list */
		for (prev = &info.fonts; *prev; prev = &(*prev)->next) {
			if (*prev == font) {
				*prev = font->next;
				break;
			}
		}
		/* Destroy the font */
		XftFontDestroy(font);
		--info.num_unref_fonts;
	}
}

void XftFontClose(XftFont* font) {
	if (--font->ref != 0)
		return;
	
	++info.num_unref_fonts;
	font_manage_memory();
}

FT_UInt XftCharIndex(XftFont* font, Char ucs4) {
	if (!font->hash_length)
		return 0;
	Char ent = ucs4 % font->hash_length;
	UcsHash* table = font->hash_table;
	Char offset = 0;
	while (table[ent].ucs4 != ucs4) {
		// empty slot
		if (table[ent].ucs4 == -1) {
			if (!XftCharExists(font, ucs4))
				return 0;
			FT_Face face = xft_lock_face(font);
			if (!face)
				return 0;
			table[ent] = (UcsHash){
				.ucs4 = ucs4,
				.glyph = FcFreeTypeCharIndex(face, ucs4),
			};
			break;
		}
		// collision: choose new index
		if (!offset) {
			// calculate offset if we haven't already
			offset = ucs4 % font->rehash_value;
			if (!offset)
				offset = 1;
		}
		ent += offset;
		ent %= font->hash_length;
	}
	return font->hash_table[ent].glyph;
}
