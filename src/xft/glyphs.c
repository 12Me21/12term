#include "xftint.h"
#include <ft2build.h>
#include FT_OUTLINE_H
#include FT_LCD_FILTER_H

#include FT_SYNTHESIS_H

#include FT_GLYPH_H

// Validate the memory info for a font
static void _XftFontValidateMemory(XftFont* font) {
	unsigned long glyph_memory = 0;
	for (FT_UInt glyphindex=0; glyphindex<font->num_glyphs; glyphindex++) {
		XftGlyph* xftg = font->glyphs[glyphindex];
		if (xftg)
			glyph_memory += xftg->glyph_memory;
	}
	if (glyph_memory != font->glyph_memory)
		print("Font glyph cache incorrect has %ld bytes, should have %ld\n", font->glyph_memory, glyph_memory);
}

/* we sometimes need to convert the glyph bitmap in a FT_GlyphSlot
 * into a different format. For example, we want to convert a
 * FT_PIXEL_MODE_LCD or FT_PIXEL_MODE_LCD_V bitmap into a 32-bit
 * ARGB or ABGR bitmap.
 *
 * this function prepares a target descriptor for this operation.
 *
 * input :: target bitmap descriptor. The function will set its
 *          'width', 'rows' and 'pitch' fields, and only these
 *
 * slot  :: the glyph slot containing the source bitmap. this
 *          function assumes that slot->format == FT_GLYPH_FORMAT_BITMAP
 *
 * mode  :: the requested final rendering mode. supported values are
 *          MONO, NORMAL (i.e. gray), LCD and LCD_V
 *
 * the function returns the size in bytes of the corresponding buffer,
 * it's up to the caller to allocate the corresponding memory block
 * before calling _fill_xrender_bitmap
 *
 * it also returns -1 in case of error (e.g. incompatible arguments,
 * like trying to convert a gray bitmap into a monochrome one)
 */
static int _compute_xrender_bitmap_size(FT_Bitmap* target, FT_GlyphSlot slot, FT_Render_Mode mode, FT_Matrix* matrix) {
	if (slot->format != FT_GLYPH_FORMAT_BITMAP)
		return -1;
	
	// compute the size of the final bitmap
	FT_Bitmap* ftbit = &slot->bitmap;
	int width = ftbit->width;
	int height = ftbit->rows;
	
	FT_Vector vector;
	if (matrix && mode == FT_RENDER_MODE_NORMAL) {
		vector.x = ftbit->width;
		vector.y = ftbit->rows;
		FT_Vector_Transform(&vector, matrix);
		
		width = vector.x;
		height = vector.y;
	}
	int pitch = (width+3) & ~3;
	
	switch (ftbit->pixel_mode) {
	case FT_PIXEL_MODE_MONO:
		if (mode == FT_RENDER_MODE_MONO) {
			pitch = (((width+31) & ~31) >> 3);
			break;
		}
		/* fall-through */
	case FT_PIXEL_MODE_GRAY:
		if (mode == FT_RENDER_MODE_LCD || mode == FT_RENDER_MODE_LCD_V) {
			/* each pixel is replicated into a 32-bit ARGB value */
			pitch = width*4;
		}
		break;
		
	case FT_PIXEL_MODE_BGRA:
		pitch = width * 4;
		break;
		
	case FT_PIXEL_MODE_LCD:
		if (mode != FT_RENDER_MODE_LCD)
			return -1;
		/* horz pixel triplets are packed into 32-bit ARGB values */
		width /= 3;
		pitch = width*4;
		break;
		
	case FT_PIXEL_MODE_LCD_V:
		if (mode != FT_RENDER_MODE_LCD_V)
			return -1;
		/* vert pixel triplets are packed into 32-bit ARGB values */
		height /= 3;
		pitch = width*4;
		break;
		
	default: /* unsupported source format */
		return -1;
	}
	
	target->width = width;
	target->rows = height;
	target->pitch = pitch;
	target->buffer = NULL;
	
	return pitch * height;
}

static inline int clamp(int x, int min, int max) {
	if (x<min)
		return min;
	if (x>max)
		return max;
	return x;
}

/* this functions converts the glyph bitmap found in a FT_GlyphSlot
 * into a different format while scaling by applying the given matrix
 * (see _compute_xrender_bitmap_size)
 *
 * you should call this function after _compute_xrender_bitmap_size
 *
 * target :: target bitmap descriptor. Note that its 'buffer' pointer
 *           must point to memory allocated by the caller
 *
 * source :: the source bitmap descriptor
 *
 * matrix :: the scaling matrix to apply
 */
static void _scaled_fill_xrender_bitmap(FT_Bitmap* target, FT_Bitmap* source, const FT_Matrix* matrix) {
	unsigned char* src_buf = source->buffer;
	int src_pitch = source->pitch;
	if (src_pitch<0)
		src_buf -= src_pitch*(source->rows-1);
	
	FT_Matrix inverse = *matrix;
	FT_Matrix_Invert(&inverse);
	
	// compute how many source pixels a target pixel spans
	FT_Vector vector;
	vector.x = 1;
	vector.y = 1;
	FT_Vector_Transform(&vector, &inverse);
	int sampling_width = vector.x / 2;
	int sampling_height = vector.y / 2;
	int sample_count = (2*sampling_width + 1) * (2*sampling_height + 1);
	
	int width = target->width;
	int height = target->rows;
	int pitch = target->pitch;
	unsigned char* dst_line = target->buffer;
	for (int h=height; h>0; h--, dst_line+=pitch) {
		for (int x=0; x<width; x++) {
			unsigned char* src;
			
			// compute target pixel location in source space
			vector.x = (x            * 0x10000) + 0x10000 / 2;
			vector.y = ((height - h) * 0x10000) + 0x10000 / 2;
			FT_Vector_Transform(&vector, &inverse);
			vector.x = clamp(FT_RoundFix(vector.x) / 0x10000, 0, source->width - 1);
			vector.y = clamp(FT_RoundFix(vector.y) / 0x10000, 0, source->rows  - 1);
			
			switch (source->pixel_mode) {
			case FT_PIXEL_MODE_MONO: /* convert mono to 8-bit gray, scale using nearest pixel */
				src = src_buf + (vector.y*src_pitch);
				if (src[vector.x>>3] & (0x80 >> (vector.x & 7)) )
					dst_line[x] = 0xFF;
				break;
				
			case FT_PIXEL_MODE_GRAY: /* scale using nearest pixel */
				src = src_buf + (vector.y * src_pitch);
				dst_line[x] = src[vector.x];
				break;
				
			case FT_PIXEL_MODE_BGRA: /* scale by averaging all relevant source pixels, keep BGRA format */ {
				int bgra[4] = {0};
				for (int sample_y = -sampling_height; sample_y < sampling_height + 1; ++sample_y) {
					int src_y = clamp(vector.y + sample_y, 0, source->rows - 1);
					src = src_buf + (src_y * src_pitch);
					for (int sample_x = -sampling_width; sample_x < sampling_width + 1; ++sample_x) {
						int src_x = clamp(vector.x + sample_x, 0, source->width - 1);
						for (int i=0; i<4; ++i)
							bgra[i] += src[src_x*4+i];
					}
				}
				
				for (int i=0; i<4; ++i)
					dst_line[4*x+i] = bgra[i]/sample_count;
				break;
			}
			}
		}
	}
}

static unsigned int pack(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
	return a | b<<8 | c<<16 | d<<24;
}

/* this functions converts the glyph bitmap found in a FT_GlyphSlot
 * into a different format (see _compute_xrender_bitmap_size)
 *
 * you should call this function after _compute_xrender_bitmap_size
 *
 * target :: target bitmap descriptor. Note that its 'buffer' pointer
 *           must point to memory allocated by the caller
 *
 * slot   :: the glyph slot containing the source bitmap
 *
 * mode   :: the requested final rendering mode
 *
 * bgr    :: boolean, set if BGR or VBGR pixel ordering is needed
 */
static void _fill_xrender_bitmap(FT_Bitmap* target, FT_GlyphSlot slot, FT_Render_Mode mode, int bgr) {
	FT_Bitmap* ftbit = &slot->bitmap;
	int src_pitch = ftbit->pitch;
	unsigned char*	srcLine = ftbit->buffer;
	if (src_pitch < 0)
		srcLine -= src_pitch*(ftbit->rows-1);
	
	int width = target->width;
	int height = target->rows;
	int pitch = target->pitch;
	unsigned char*	dstLine = target->buffer;
	int subpixel = (mode==FT_RENDER_MODE_LCD || mode==FT_RENDER_MODE_LCD_V );
	switch (ftbit->pixel_mode) {
	case FT_PIXEL_MODE_MONO:
		if (subpixel) { // convert mono to ARGB32 values
			for (int h=height; h>0; h--, srcLine += src_pitch, dstLine += pitch) {
				for (int x=0; x<width; x++) {
					if (srcLine[x>>3] & (0x80 >> (x & 7)))
						((unsigned int*)dstLine)[x] = 0xffffffffU;
				}
			}
		} else if (mode == FT_RENDER_MODE_NORMAL) { // convert mono to 8-bit gray
			for (int h=height; h>0; h--, srcLine += src_pitch, dstLine += pitch) {
				for (int x=0; x<width; x++) {
					if (srcLine[x>>3] & (0x80 >> (x & 7)))
						dstLine[x] = 0xff;
				}
			}
		} else { // copy mono to mono
			int bytes = (width+7) >> 3;
			for (int h=height; h>0; h--, srcLine += src_pitch, dstLine += pitch )
				memcpy(dstLine, srcLine, bytes);
		}
		break;
		
	case FT_PIXEL_MODE_GRAY:
		if (subpixel)  /* convert gray to ARGB32 values */ {
			for (int h=height; h>0; h--, srcLine += src_pitch, dstLine += pitch) {
				unsigned int* dst = (unsigned int*)dstLine;
				
				for (int x=0; x<width; x++) {
					unsigned int pix = srcLine[x];
					dst[x] = pack(pix, pix, pix, pix);
				}
			}
		} else { // copy gray into gray
			for (int h=height; h>0; h--, srcLine += src_pitch, dstLine += pitch)
				memcpy(dstLine, srcLine, width);
		}
		break;
		
	case FT_PIXEL_MODE_BGRA: /* Preserve BGRA format */
		for (int h=height; h>0; h--, srcLine += src_pitch, dstLine += pitch)
			memcpy(dstLine, srcLine, width*4);
		break;
		
	case FT_PIXEL_MODE_LCD:
		if (!bgr) {
			/* convert horizontal RGB into ARGB32 */
			for (int h=height; h>0; h--, srcLine += src_pitch, dstLine += pitch ) {
				unsigned char* src = srcLine;
				unsigned int* dst = (unsigned int*)dstLine;
				
				for (int x = 0; x < width; x++, src += 3 ) {
					dst[x] = pack(src[2], src[1], src[0], src[1]); // is this supposed to be 3?
				}
			}
		} else {
			/* convert horizontal BGR into ARGB32 */
			for (int h=height; h>0; h--, srcLine += src_pitch, dstLine += pitch) {
				unsigned char* src = srcLine;
				unsigned int* dst = (unsigned int*)dstLine;
				
				for (int x=0; x<width; x++, src+=3) {
					dst[x] = pack(src[0], src[1], src[2], src[1]);
				}
			}
		}
		break;

	default:  /* FT_PIXEL_MODE_LCD_V */
		/* convert vertical RGB into ARGB32 */
		if (!bgr) {
			for (int h=height; h>0; h--, srcLine+=3*src_pitch, dstLine+=pitch) {
				unsigned char* src = srcLine;
				unsigned int* dst = (unsigned int*)dstLine;
				
				for (int x=0; x<width; x++, src+=1) {
					dst[x] = pack(src[src_pitch*2], src[src_pitch], src[0], src[src_pitch]); // repeated values here are not a typo, i checked carefully
				}
			}
		} else {
			for (int h=height; h>0; h--, srcLine+=3*src_pitch, dstLine+=pitch ) {
				unsigned char* src = srcLine;
				unsigned int* dst = (unsigned int*)dstLine;
				
				for (int x=0; x<width; x++, src+=1) {
					dst[x] = pack(src[0], src[src_pitch], src[src_pitch*2], src[src_pitch]);
				}
			}
		}
	}
}

void XftFontLoadGlyphs(XftFont* font, bool need_bitmaps, const FT_UInt* glyphs, int nglyph) {
	FT_Face face = XftLockFace(font);
	if (!face)
		return;
	
	// determine render mode
	FT_Render_Mode mode = FT_RENDER_MODE_MONO;
	if (font->info.color)
		mode = FT_RENDER_MODE_NORMAL;
	if (font->info.antialias) {
		switch (font->info.rgba) {
		case FC_RGBA_RGB:
		case FC_RGBA_BGR:
			mode = FT_RENDER_MODE_LCD;
			break;
		case FC_RGBA_VRGB:
		case FC_RGBA_VBGR:
			mode = FT_RENDER_MODE_LCD_V;
			break;
		default:
			mode = FT_RENDER_MODE_NORMAL;
		}
	}
	
	bool transform = font->info.transform && mode != FT_RENDER_MODE_MONO;
	
	// for each glyph:
	while (nglyph--) {
		FT_UInt glyphindex = *glyphs++;
		XftGlyph* xftg = font->glyphs[glyphindex];
		if (!xftg)
			continue;
		
		if (XftDebug() & XFT_DBG_CACHE)
			_XftFontValidateMemory(font);
		// Check to see if this glyph has just been loaded,
		// this happens when drawing the same glyph twice
		// in a single string
		if (xftg->glyph_memory)
			continue;
		
		FT_Library_SetLcdFilter(ft_library, font->info.lcd_filter);
		
		FT_Error	error = FT_Load_Glyph(face, glyphindex, font->info.load_flags);
		if (error) {
			// If anti-aliasing or transforming glyphs and
			// no outline version exists, fallback to the
			// bitmap and let things look bad instead of
			// missing the glyph
			if (font->info.load_flags & FT_LOAD_NO_BITMAP)
				error = FT_Load_Glyph(face, glyphindex, font->info.load_flags & ~FT_LOAD_NO_BITMAP);
			if (error)
				continue;
		}

#define FLOOR(x)    ((x) & -64)
#define CEIL(x)	    (((x)+63) & -64)
#define TRUNC(x)    ((x) >> 6)
#define ROUND(x)    (((x)+32) & -64)
		
		FT_GlyphSlot glyphslot = face->glyph;
		
		// Embolden if required
		if (font->info.embolden) FT_GlyphSlot_Embolden(glyphslot);
		
		// Compute glyph metrics from FreeType information
		int left, right, top, bottom;
		FT_Vector vector;
		if (transform) {
			// calculate the true width by transforming all four corners.
			left = right = top = bottom = 0;
			for (int xc = 0; xc <= 1; xc ++) {
				for (int yc = 0; yc <= 1; yc++) {
					vector.x = glyphslot->metrics.horiBearingX + xc * glyphslot->metrics.width;
					vector.y = glyphslot->metrics.horiBearingY - yc * glyphslot->metrics.height;
					FT_Vector_Transform(&vector, &font->info.matrix);
					if (XftDebug() & XFT_DBG_GLYPH)
						print("Trans %d %d: %d %d\n", (int) xc, (int) yc,
						       (int) vector.x, (int) vector.y);
					if (xc == 0 && yc == 0) {
						left = right = vector.x;
						top = bottom = vector.y;
					} else {
						if (left > vector.x) left = vector.x;
						if (right < vector.x) right = vector.x;
						if (bottom > vector.y) bottom = vector.y;
						if (top < vector.y) top = vector.y;
					}
				}
			}
			left = FLOOR(left);
			right = CEIL(right);
			bottom = FLOOR(bottom);
			top = CEIL(top);
			// lots of rounding going on here... kinda sus
		} else {
			left = FLOOR( glyphslot->metrics.horiBearingX );
			right = CEIL( glyphslot->metrics.horiBearingX + glyphslot->metrics.width );
			
			top = CEIL( glyphslot->metrics.horiBearingY );
			bottom = FLOOR( glyphslot->metrics.horiBearingY - glyphslot->metrics.height );
		}
		
		int width = TRUNC(right - left);
		int height = TRUNC( top - bottom );
		
		// Clip charcell glyphs to the bounding box
		// XXX transformed?
		if (font->info.spacing >= FC_CHARCELL && !transform) {
			if (font->info.load_flags & FT_LOAD_VERTICAL_LAYOUT) {
				if (TRUNC(bottom) > font->max_advance_width) {
					int adjust = bottom - (font->max_advance_width << 6);
					if (adjust > top)
						adjust = top;
					top -= adjust;
					bottom -= adjust;
					height = font->max_advance_width;
				}
			} else {
				if (TRUNC(right) > font->max_advance_width) {
					int adjust = right - (font->max_advance_width << 6);
					if (adjust > left)
						adjust = left;
					left -= adjust;
					right -= adjust;
					width = font->max_advance_width;
				}
			}
		}
		
		bool glyph_transform = transform;
		if (glyphslot->format != FT_GLYPH_FORMAT_BITMAP) {
			error = FT_Render_Glyph(face->glyph, mode);
			if (error)
				continue;
			glyph_transform = false;
		}
		
		FT_Library_SetLcdFilter(ft_library, FT_LCD_FILTER_NONE);
		
		if (font->info.spacing >= FC_MONO) {
			if (transform) {
				if (font->info.load_flags & FT_LOAD_VERTICAL_LAYOUT) {
					vector.x = 0;
					vector.y = -face->size->metrics.max_advance;
				} else {
					vector.x = face->size->metrics.max_advance;
					vector.y = 0;
				}
				FT_Vector_Transform (&vector, &font->info.matrix);
				xftg->metrics.xOff = vector.x >> 6;
				xftg->metrics.yOff = -(vector.y >> 6);
			} else {
				if (font->info.load_flags & FT_LOAD_VERTICAL_LAYOUT) {
					xftg->metrics.xOff = 0;
					xftg->metrics.yOff = -font->max_advance_width;
				} else {
					xftg->metrics.xOff = font->max_advance_width;
					xftg->metrics.yOff = 0;
				}
			}
		} else {
			xftg->metrics.xOff = TRUNC(ROUND(glyphslot->advance.x));
			xftg->metrics.yOff = -TRUNC(ROUND(glyphslot->advance.y));
		}
		
		// compute the size of the final bitmap
		FT_Bitmap* ftbit = &glyphslot->bitmap;
		
		width = ftbit->width;
		height = ftbit->rows;
		
		if (XftDebug() & XFT_DBG_GLYPH) {
			print("glyph %d:\n", (int) glyphindex);
			print(" xywh (%d %d %d %d), trans (%d %d %d %d) wh (%d %d)\n",
			       (int) glyphslot->metrics.horiBearingX,
			       (int) glyphslot->metrics.horiBearingY,
			       (int) glyphslot->metrics.width,
			       (int) glyphslot->metrics.height,
			       left, right, top, bottom,
			       width, height);
			if (XftDebug() & XFT_DBG_GLYPHV) {
				unsigned char* line = ftbit->buffer;
				if (ftbit->pitch < 0)
					line -= ftbit->pitch*(height-1);
				
				for (int y = 0; y < height; y++) {
					if (font->info.antialias) {
						static const char den[] = {" .:;=+*#"};
						for (int x = 0; x < width; x++)
							print("%c", den[line[x] >> 5]);
					} else {
						for (int x = 0; x < width * 8; x++) {
							print("%c", line[x>>3] & (1 << (x & 7)) ? '#' : ' ');
						}
					}
					print("|\n");
					line += ftbit->pitch;
				}
				print("\n");
			}
		}
		
		FT_Bitmap local;
		int size = _compute_xrender_bitmap_size(&local, glyphslot, mode, glyph_transform ? &font->info.matrix : NULL);
		if (size < 0)
			continue;
		
		xftg->metrics.width  = local.width;
		xftg->metrics.height = local.rows;
		if (0&&transform) {
			vector.x = - glyphslot->bitmap_left;
			vector.y =   glyphslot->bitmap_top;
			
			FT_Vector_Transform(&vector, &font->info.matrix);
			
			xftg->metrics.x = vector.x;
			xftg->metrics.y = vector.y;
		} else {
			xftg->metrics.x = - glyphslot->bitmap_left;
			xftg->metrics.y =   glyphslot->bitmap_top;
		}
		
		// If the glyph is relatively large (> 1% of server memory),
		// don't send it until necessary.
		//if (!need_bitmaps && size>info.max_glyph_memory/100)
		//	continue;
		
		unsigned char bufBitmap[size]; // I hope there's enough stack space owo
		memset(bufBitmap, 0, size);
		
		local.buffer = bufBitmap;
		
		if (mode == FT_RENDER_MODE_NORMAL && glyph_transform)
			_scaled_fill_xrender_bitmap(&local, &glyphslot->bitmap, &font->info.matrix);
		else
			_fill_xrender_bitmap(&local, glyphslot, mode, font->info.rgba==FC_RGBA_BGR || font->info.rgba==FC_RGBA_VBGR);
		
		// Copy or convert into local buffer.
		
		// Use the glyph index as the wire encoding; it
		// might be more efficient for some locales to map
		// these by first usage to smaller values, but that
		// would require persistently storing the map when
		// glyphs were freed.
		Glyph glyph = glyphindex;
		
		xftg->picture = 0;
		xftg->glyph_memory = sizeof(XftGlyph) + size;
		if (!font->glyphset)
			font->glyphset = XRenderCreateGlyphSet(W.d, font->format);
		if (mode == FT_RENDER_MODE_MONO) {
			/* swap bits in each byte */
			if (BitmapBitOrder(W.d) != MSBFirst) {
				unsigned char* line = bufBitmap;
				int i = size;
					
				while (i--) {
					int c = *line;
					// fancy
					c = (c<<1 & 0xAA) | (c>>1 & 0x55);
					c = (c<<2 & 0xCC) | (c>>2 & 0x33);
					c = (c<<4 & 0xF0) | (c>>4 & 0x0F);
					*line++ = c;
				}
			}
		} else if (glyphslot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA || mode != FT_RENDER_MODE_NORMAL) {
			/* invert ARGB <=> BGRA */
			if (ImageByteOrder(W.d) != XftNativeByteOrder())
				XftSwapCARD32((CARD32*)bufBitmap, size/4);
		}
			
		if (glyphslot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
			// all of this is just to take data and turn it into a Picture
			Pixmap pixmap = XCreatePixmap(W.d, DefaultRootWindow(W.d), local.width, local.rows, 32);
			GC gc = XCreateGC(W.d, pixmap, 0, NULL);
			XImage* image = XCreateImage(W.d, W.vis, 32, ZPixmap, 0, (char*)bufBitmap, local.width, local.rows, 32, 0);
			XPutImage(W.d, pixmap, gc, image, 0, 0, 0, 0, local.width, local.rows);
			xftg->picture = XRenderCreatePicture(W.d, pixmap, font->format, 0, NULL);
			image->data = NULL; // this is probably safe...
			XDestroyImage(image);
			XFreeGC(W.d, gc);
			XFreePixmap(W.d, pixmap);
		} else
			XRenderAddGlyphs(W.d, font->glyphset, &glyph, &xftg->metrics, 1, (char*)bufBitmap, size);
		
		font->glyph_memory += xftg->glyph_memory;
		info.glyph_memory += xftg->glyph_memory;
		if (XftDebug() & XFT_DBG_CACHE)
			_XftFontValidateMemory(font);
		if (XftDebug() & XFT_DBG_CACHEV)
			print("Caching glyph 0x%x size %ld\n", glyphindex, xftg->glyph_memory);
	}
	XftUnlockFace(font);
}

void XftFontUnloadGlyphs(XftFont* font, const FT_UInt* glyphs, int nglyph) {
	Glyph	glyphBuf[1024];
	int nused = 0;
	while (nglyph--) {
		FT_UInt glyphindex = *glyphs++;
		XftGlyph* xftg = font->glyphs[glyphindex];
		if (!xftg)
			continue;
		if (xftg->glyph_memory) {
			if (xftg->picture)
				XRenderFreePicture(W.d, xftg->picture);
			else if (font->glyphset) {
				glyphBuf[nused++] = (Glyph)glyphindex;
				if (nused == sizeof(glyphBuf) / sizeof(glyphBuf[0])) {
					XRenderFreeGlyphs(W.d, font->glyphset, glyphBuf, nused);
					nused = 0;
				}
			}
			font->glyph_memory -= xftg->glyph_memory;
			info.glyph_memory -= xftg->glyph_memory;
		}
		free(xftg);
		XftMemFree(XFT_MEM_GLYPH, sizeof(XftGlyph));
		font->glyphs[glyphindex] = NULL;
	}
	if (font->glyphset && nused)
		XRenderFreeGlyphs(W.d, font->glyphset, glyphBuf, nused);
}

bool XftFontCheckGlyph(XftFont* font, bool need_bitmaps, FT_UInt glyph, FT_UInt* missing, int* nmissing) {
	if (glyph >= font->num_glyphs)
		return false;
	XftGlyph* xftg = font->glyphs[glyph];
	if (!xftg || (need_bitmaps && !xftg->glyph_memory)) {
		if (!xftg) {
			xftg = XftMalloc(XFT_MEM_GLYPH, sizeof(XftGlyph));
			if (!xftg)
				return false;
			xftg->glyph_memory = 0;
			font->glyphs[glyph] = xftg;
		}
		int n = *nmissing;
		missing[n++] = glyph;
		if (n == XFT_NMISSING) { //if the results array is out of space we just load the glyphs here right away
			XftFontLoadGlyphs(font, need_bitmaps, missing, n);
			n = 0;
		}
		*nmissing = n;
		return true;
	} else
		return false;
}

bool XftCharExists(XftFont* font, FcChar32 ucs4) {
	if (font->charset)
		return FcCharSetHasChar(font->charset, ucs4);
	return false;
}

FT_UInt XftCharIndex(XftFont* font, FcChar32 ucs4) {
	if (!font->hash_value)
		return 0;
	FcChar32	ent = ucs4 % font->hash_value;
	FcChar32	offset = 0;
	while (font->hash_table[ent].ucs4 != ucs4) {
		if (font->hash_table[ent].ucs4 == (FcChar32)~0) {
			if (!XftCharExists(font, ucs4))
				return 0;
			FT_Face face = XftLockFace(font);
			if (!face)
				return 0;
			font->hash_table[ent].ucs4 = ucs4;
			font->hash_table[ent].glyph = FcFreeTypeCharIndex(face, ucs4);
			XftUnlockFace(font);
			break;
		}
		if (!offset) {
			offset = ucs4 % font->rehash_value;
			if (!offset)
				offset = 1;
		}
		ent += offset;
		if (ent >= font->hash_value)
			ent -= font->hash_value;
	}
	return font->hash_table[ent].glyph;
}

// Pick a random glyph from the font and remove it from the cache
// hey uh this is not a valid function name!!!!
void _XftFontUncacheGlyph(XftFont* font) {
	if (!font->glyph_memory)
		return;
	
	unsigned long glyph_memory;
	glyph_memory = rand() % font->glyph_memory;
	
	if (XftDebug() & XFT_DBG_CACHE)
		_XftFontValidateMemory(font);
	for (FT_UInt glyphindex=0; glyphindex<font->num_glyphs; glyphindex++) {
		XftGlyph* xftg = font->glyphs[glyphindex];
		if (xftg) {
			if (xftg->glyph_memory > glyph_memory) {
				if (XftDebug() & XFT_DBG_CACHEV)
					print("Uncaching glyph 0x%x size %ld\n", glyphindex, xftg->glyph_memory);
				XftFontUnloadGlyphs(font, &glyphindex, 1);
				break;
			}
			glyph_memory -= xftg->glyph_memory;
		}
	}
	if (XftDebug() & XFT_DBG_CACHE)
		_XftFontValidateMemory(font);
}

void _XftFontManageMemory(XftFont* font) {
	if (font->max_glyph_memory) {
		if (XftDebug() & XFT_DBG_CACHE) {
			if (font->glyph_memory > font->max_glyph_memory)
				print("Reduce memory for font 0x%lx from %ld to %ld\n",
					font->glyphset ? font->glyphset : (unsigned long)font,
					font->glyph_memory, font->max_glyph_memory);
		}
		while (font->glyph_memory > font->max_glyph_memory)
			_XftFontUncacheGlyph(font);
	}
	_XftDisplayManageMemory();
}