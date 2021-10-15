#include "xftint.h"

FT_Library ft_library;

// List of all open files (each face in a file is managed separately)
static XftFtFile* _XftFtFiles;
static int XftMaxFreeTypeFiles = 5;

static XftFtFile* _XftGetFile(const FcChar8* file, int id) {
	XftFtFile* f;
	for (f = _XftFtFiles; f; f = f->next) {
		if (!strcmp(f->file, (char*)file) && f->id == id) {
			++f->ref;
			if (XftDebug () & XFT_DBG_REF)
				printf ("FontFile %s/%d matches existing (%d)\n",
				        file, id, f->ref);
			return f;
		}
	}
	// hmm so this is safe because char doesn't have alignment limits, but
	// i wonder if there is a safe way to do this for larger data types?
	f = XftMalloc(XFT_MEM_FILE, sizeof(XftFtFile)+strlen((char*)file)+1);
	if (!f)
		return NULL;
	
	if (XftDebug() & XFT_DBG_REF)
		printf("FontFile %s/%d matches new\n", file, id);
	f->next = _XftFtFiles;
	_XftFtFiles = f;
	
	f->ref = 1;
	
	f->file = (char*)&f[1];
	strcpy(f->file, (char*)file);
	f->id = id;
	
	f->lock = 0;
	f->face = NULL;
	f->xsize = 0;
	f->ysize = 0;
	f->matrix.xx = f->matrix.xy = f->matrix.yx = f->matrix.yy = 0;
	return f;
}

static XftFtFile* _XftGetFaceFile(FT_Face face) {
	XftFtFile* f = XftMalloc(XFT_MEM_FILE, sizeof(XftFtFile));
	if (!f)
		return NULL;
	
	f->next = NULL;
	
	f->ref = 1;
	
	f->file = NULL;
	f->id = 0;
	f->lock = 0;
	f->face = face;
	f->xsize = 0;
	f->ysize = 0;
	f->matrix.xx = f->matrix.xy = f->matrix.yx = f->matrix.yy = 0;
	return f;
}

static int _XftNumFiles(void) {
	int count = 0;
	for (XftFtFile* f=_XftFtFiles; f; f=f->next)
		if (f->face && !f->lock)
			++count;
	return count;
}

static XftFtFile* _XftNthFile(int n) {
	int count = 0;
	XftFtFile* f;
	for (f=_XftFtFiles; f; f=f->next)
		if (f->face && !f->lock)
			if (count++ == n)
				break;
	return f;
}

static void _XftUncacheFiles(void) {
	int n;
	while ((n = _XftNumFiles()) > XftMaxFreeTypeFiles) {
		XftFtFile* f = _XftNthFile(rand() % n);
		if (f) {
			if (XftDebug() & XFT_DBG_REF)
				printf("Discard file %s/%d from cache\n",
				        f->file, f->id);
			FT_Done_Face(f->face);
			f->face = NULL;
		}
	}
}

static FT_Face _XftLockFile(XftFtFile* f) {
	++f->lock;
	if (!f->face) {
		if (XftDebug() & XFT_DBG_REF)
			printf("Loading file %s/%d\n", f->file, f->id);
		if (FT_New_Face(ft_library, f->file, f->id, &f->face))
			--f->lock;
		
		f->xsize = 0;
		f->ysize = 0;
		f->matrix.xx = f->matrix.xy = f->matrix.yx = f->matrix.yy = 0;
		_XftUncacheFiles();
	}
	return f->face;
}

static void _XftLockError(const char* reason) {
	fprintf(stderr, "Xft: locking error %s\n", reason);
}

static void _XftUnlockFile(XftFtFile* f) {
	if (--f->lock < 0)
		_XftLockError ("too many file unlocks");
}

static bool matrix_equal(FT_Matrix* a, FT_Matrix* b) {
	return a->xx==b->xx && a->yy==b->yy && a->xy==b->xy && a->yx==b->yx;
}

static FT_F26Dot6 dist(FT_F26Dot6 a, FT_F26Dot6 b) {
	if (a>b)
		return a-b;
	return b-a;
}

bool _XftSetFace(XftFtFile* f, FT_F26Dot6 xsize, FT_F26Dot6 ysize, FT_Matrix* matrix) {
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
				return False;
		} else {
			if (FT_Set_Char_Size(face, xsize, ysize, 0, 0))
				return False;
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
	return True;
}

static void _XftReleaseFile(XftFtFile* f) {
	if (--f->ref != 0)
		return;
	if (f->lock)
		_XftLockError ("Attempt to close locked file");
	if (f->file) {
		for (XftFtFile** prev = &_XftFtFiles; *prev; prev = &(*prev)->next) {
			if (*prev == f) {
				*prev = f->next;
				break;
			}
		}
		if (f->face)
			FT_Done_Face(f->face);
	}
	XftMemFree(XFT_MEM_FILE, sizeof(XftFtFile) + (f->file ? strlen(f->file)+1 : 0));
	free(f);
}

/*
 * Find a prime larger than the minimum reasonable hash size
 */
static FcChar32 _XftSqrt(FcChar32 a) {
	FcChar32 l = 2;
	FcChar32 h = a/2;
	while ((h-l) > 1) {
		FcChar32 m = (h+l) >> 1;
		if (m * m < a)
			l = m;
		else
			h = m;
	}
	return h;
}

static bool _XftIsPrime (FcChar32 i) {
	FcChar32	l, t;
	
	if (i < 2)
		return false;
	if ((i&1) == 0) {
		if (i == 2)
			return true;
		return false;
	}
	l = _XftSqrt(i) + 1;
	for (t = 3; t <= l; t += 2)
		if (i % t == 0)
			return false;
	return true;
}

static FcChar32 _XftHashSize(FcChar32 num_unicode) {
	// at least 31.25% extra space
	FcChar32	hash = num_unicode + (num_unicode>>2) + (num_unicode>>4);
	
	if ((hash&1) == 0)
		hash++;
	while (!_XftIsPrime(hash))
		hash += 2;
	return hash;
}

FT_Face XftLockFace(XftFont* font) {
	XftFontInfo* fi = &font->info;
	FT_Face face = _XftLockFile(fi->file);
	// Make sure the face is usable at the requested size
	if (face && !_XftSetFace(fi->file, fi->xsize, fi->ysize, &fi->matrix)) {
		_XftUnlockFile(fi->file);
		face = NULL;
	}
	return face;
}

void XftUnlockFace(XftFont* font) {
	_XftUnlockFile(font->info.file);
}

static bool XftFontInfoFill(const FcPattern* pattern, XftFontInfo* fi) {
	// Initialize the whole XftFontInfo so that padding doesn't interfere with
	// hash or XftFontInfoEqual().
	memset(fi, '\0', sizeof(*fi));
	
	// Find the associated file
	FcChar8* filename = NULL;
	FcPatternGetString(pattern, FC_FILE, 0, &filename);
	
	int id = 0;
	FcPatternGetInteger(pattern, FC_INDEX, 0, &id);
	
	FT_Face face;
	if (filename)
		fi->file = _XftGetFile(filename, id);
	else if (FcPatternGetFTFace(pattern, FC_FT_FACE, 0, &face) == FcResultMatch && face)
		fi->file = _XftGetFaceFile(face);
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
		       (filename ? filename : (FcChar8*)"<none>"), id, dsize);
	
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
	
	// Compute glyph load flags
	fi->load_flags = FT_LOAD_DEFAULT | FT_LOAD_COLOR;
	
	FcBool bitmap = false;
	FcPatternGetBool(pattern, "embeddedbitmap", 0, &bitmap);
	
	// disable bitmaps when anti-aliasing or transforming glyphs
	if ((!bitmap && fi->antialias) || fi->transform)
		fi->load_flags |= FT_LOAD_NO_BITMAP;
	
	// disable hinting if requested
	FcBool hinting = true;
	FcPatternGetBool(pattern, FC_HINTING, 0, &hinting);
	
	fi->embolden = false;
	FcPatternGetBool(pattern, FC_EMBOLDEN, 0, &fi->embolden);
	
	int hint_style = FC_HINT_FULL;
	FcPatternGetInteger(pattern, FC_HINT_STYLE, 0, &hint_style);
	
	if (!hinting || hint_style == FC_HINT_NONE)
		fi->load_flags |= FT_LOAD_NO_HINTING;
	
	// Figure out the load target, which modifies the hinting
	// behavior of FreeType based on the intended use of the glyphs.
	if (fi->antialias) {
		if (FC_HINT_NONE < hint_style && hint_style < FC_HINT_FULL) {
			fi->load_flags |= FT_LOAD_TARGET_LIGHT;
		} else {
			// autohinter will snap stems to integer widths, when
			// the LCD targets are used.
			switch (fi->rgba) {
			case FC_RGBA_RGB:
			case FC_RGBA_BGR:
				fi->load_flags |= FT_LOAD_TARGET_LCD;
				break;
			case FC_RGBA_VRGB:
			case FC_RGBA_VBGR:
				fi->load_flags |= FT_LOAD_TARGET_LCD_V;
				break;
			}
		}
	} else
		fi->load_flags |= FT_LOAD_TARGET_MONO;
	
	/* set vertical layout if requested */
	FcBool vertical_layout = false;
	FcPatternGetBool(pattern, FC_VERTICAL_LAYOUT, 0, &vertical_layout);
	
	if (vertical_layout)
		fi->load_flags |= FT_LOAD_VERTICAL_LAYOUT;
	
	/* force autohinting if requested */
	FcBool autohint = false;
	FcPatternGetBool(pattern, FC_AUTOHINT, 0, &autohint);
	
	if (autohint)
		fi->load_flags |= FT_LOAD_FORCE_AUTOHINT;
	
	/* disable global advance width (for broken DynaLab TT CJK fonts) */
	FcBool global_advance = true;
	FcPatternGetBool(pattern, FC_GLOBAL_ADVANCE, 0, &global_advance);
	
	if (!global_advance)
		fi->load_flags |= FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;
	
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
	
	// Step over hash value in the structure
	FcChar32 hash = 0;
	FcChar32* hashp = (FcChar32*)fi+1;
	int nhash = (sizeof(XftFontInfo) / sizeof(FcChar32)) - 1;
	
	while (nhash--)
		hash += *hashp++;
	fi->hash = hash;

	// All done
	return true;

 bail1:
	_XftReleaseFile(fi->file);
	fi->file = NULL;
 bail0:
	return false;
}

static void XftFontInfoEmpty(XftFontInfo* fi) {
	if (fi->file)
		_XftReleaseFile(fi->file);
}

XftFontInfo* XftFontInfoCreate(const FcPattern* pattern) {
	XftFontInfo* fi = XftMalloc(XFT_MEM_FONT, sizeof(XftFontInfo));
	if (!fi)
		return NULL;

	if (!XftFontInfoFill(pattern, fi)) {
		free(fi);
		XftMemFree(XFT_MEM_FONT, sizeof(XftFontInfo));
		fi = NULL;
	}
	return fi;
}

void XftFontInfoDestroy(XftFontInfo* fi) {
	XftFontInfoEmpty(fi);
	XftMemFree(XFT_MEM_FONT, sizeof(XftFontInfo));
	free(fi);
}

FcChar32 XftFontInfoHash(const XftFontInfo* fi) {
	return fi->hash;
}

bool XftFontInfoEqual(const XftFontInfo* a, const XftFontInfo* b) {
	return memcmp(a, b, sizeof(XftFontInfo))==0;
}

XftFont* XftFontOpenInfo(FcPattern* pattern, XftFontInfo* fi) {
	// No existing font, create another.
	if (XftDebug () & XFT_DBG_CACHE)
		printf ("New font %s/%d size %dx%d\n",
		        fi->file->file, fi->file->id,
		        (int) fi->xsize >> 6, (int) fi->ysize >> 6);
	int max_glyph_memory;
	if (FcPatternGetInteger(pattern, XFT_MAX_GLYPH_MEMORY, 0,
	                         &max_glyph_memory) != FcResultMatch)
		max_glyph_memory = XFT_FONT_MAX_GLYPH_MEMORY;
	
	FT_Face face = _XftLockFile(fi->file);
	if (!face)
		goto bail0;
	
	if (!_XftSetFace(fi->file, fi->xsize, fi->ysize, &fi->matrix))
		goto bail1;

    /*
     * Get the set of Unicode codepoints covered by the font.
     * If the incoming pattern doesn't provide this data, go
     * off and compute it.  Yes, this is expensive, but it's
     * required to map Unicode to glyph indices.
     */
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
	
	FcChar32 num_unicode;
	FcChar32 hash_value;
	FcChar32 rehash_value;
	if (charset) {
		num_unicode = FcCharSetCount(charset);
		hash_value = _XftHashSize(num_unicode);
		rehash_value = hash_value-2;
	} else {
		num_unicode = 0;
		hash_value = 0;
		rehash_value = 0;
	}
	
	// Sometimes the glyphs are numbered 1..n, other times 0..n-1,
	// accept either numbering scheme by making room in the table
	int num_glyphs = face->num_glyphs + 1;
	int alloc_size = (sizeof(XftFont) +
	                  num_glyphs * sizeof(XftGlyph*) +
	                  hash_value * sizeof(XftUcsHash));
	
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
			FT_Vector_Transform (&vector, &fi->matrix);
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
			FT_Vector_Transform (&vector, &fi->matrix);
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
	font->hash_table = (XftUcsHash*)(font->glyphs+font->num_glyphs);
	for (int i=0; i<hash_value; i++) {
		font->hash_table[i].ucs4 = ((FcChar32) ~0);
		font->hash_table[i].glyph = 0;
	}
	font->hash_value = hash_value;
	font->rehash_value = rehash_value;
	// X specific fields
	font->glyphset = 0;
	font->format = format;
	// Glyph memory management fields
	font->glyph_memory = 0;
	font->max_glyph_memory = max_glyph_memory;
	
	_XftUnlockFile(fi->file);
	
	return font;
	
 bail2:
	FcCharSetDestroy(charset);
 bail1:
	_XftUnlockFile(fi->file);
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
	for (int i=0; i<font->num_glyphs; i++) {
		XftGlyph* xftg = font->glyphs[i];
		free(xftg);
	}
	
	/* Free the pattern and the charset */
	FcPatternDestroy(font->pattern);
	FcCharSetDestroy(font->charset);
	
	/* Finally, free the font structure */
	XftMemFree(XFT_MEM_FONT, sizeof(XftFont) +
	           font->num_glyphs * sizeof(XftGlyph*) +
	           font->hash_value * sizeof(XftUcsHash));
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

void XftFontManageMemory(void) {
	while (info.num_unref_fonts > info.max_unref_fonts) {
		XftFont* font = XftFontFindNthUnref(rand() % info.num_unref_fonts);
		if (XftDebug() & XFT_DBG_CACHE)
			printf("freeing unreferenced font %s/%d size %dx%d\n",
			       font->info.file->file, font->info.file->id,
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
	XftFontManageMemory();
}
