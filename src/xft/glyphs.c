#include "xftint.h"
#include <ft2build.h>
#include FT_OUTLINE_H
#include FT_LCD_FILTER_H

#include FT_SYNTHESIS_H

#include FT_GLYPH_H

static int native_byte_order(void) {
	int whichbyte = 1;
	if (*((char*)&whichbyte))
		return LSBFirst;
	return MSBFirst;
}
static inline uint32_t pack(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
	return a | b<<8 | c<<16 | d<<24;
}
static void swap_card32(uint32_t* data, int u) {
	while (u--) {
		*data = pack(*data>>24 & 0xFF, *data>>16 & 0xFF, *data>>8 & 0xFF, *data & 0xFF);
		data++;
	}
}

static int dot6_to_int(FT_F26Dot6 x) {
	return x>>6;
}
static int dot6_round(FT_F26Dot6 x) {
	return x+32 & ~63;
}

bool load_glyph(XftFont* font, Char chr, GlyphData* out) {
	FT_Face face = xft_lock_face(font);
	if (!face)
		return false;
	
	// determine render mode
	//FT_Render_Mode mode = FT_LOAD_TARGET_MODE(font->info.load_flags);
	
	FT_Render_Mode mode = FT_RENDER_MODE_MONO;
	FT_Int load_flags = font->info.load_flags;
	if (font->info.color) {
		// set render mode to NORMAL when loading color fonts, otherwise the width will be stretched 3× for subpixel rendering (if in LCD mode), which we can't handle (yet) TODO: support this? it's difficult to composite though..
		mode = FT_RENDER_MODE_NORMAL;
		load_flags = load_flags & ~FT_LOAD_TARGET_(-1) | mode;
	} else {
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
				mode = FT_RENDER_MODE_LIGHT;
			}
		}
	}
	
	bool transform = font->info.transform && mode != FT_RENDER_MODE_MONO;
	
	// lookup glyph
	FT_UInt glyphindex = FcFreeTypeCharIndex(face, chr);
	
	FT_Library_SetLcdFilter(ft_library, font->info.lcd_filter);
	
	FT_Error	error = FT_Load_Glyph(face, glyphindex, load_flags);
	if (error) {
		// If anti-aliasing or transforming glyphs and
		// no outline version exists, fallback to the
		// bitmap and let things look bad instead of
		// missing the glyph
		if (load_flags & FT_LOAD_NO_BITMAP) {
			load_flags ^= FT_LOAD_NO_BITMAP;
			error = FT_Load_Glyph(face, glyphindex, load_flags);
		}
		if (error)
			return false;
	}
	
	FT_GlyphSlot glyphslot = face->glyph;
		
	// Embolden if required
	if (font->info.embolden)
		FT_GlyphSlot_Embolden(glyphslot);
	
	bool glyph_transform = transform;
	if (glyphslot->format != FT_GLYPH_FORMAT_BITMAP) {
		error = FT_Render_Glyph(face->glyph, mode);
		if (error) {
			// uh oh
			return false;
		}
		glyph_transform = false;
	}
	
	FT_Library_SetLcdFilter(ft_library, FT_LCD_FILTER_NONE);
	
	int x_off, y_off;
	if (font->info.spacing >= FC_MONO) {
		if (transform) {
			FT_Vector vector = {
				.x = face->size->metrics.max_advance,
				.y = 0,
			};
			FT_Vector_Transform(&vector, &font->info.matrix);
			x_off = vector.x >> 6;
			y_off = -(vector.y >> 6);
		} else {
			x_off = font->max_advance_width;
			y_off = 0;
		}
	} else {
		x_off = dot6_to_int(dot6_round(glyphslot->advance.x));
		y_off = -dot6_to_int(dot6_round(glyphslot->advance.y));
	}
	out->metrics.xOff = x_off;
	out->metrics.yOff = y_off;
	// TODO: remember that sub-pixel positioning exists?
	
	FT_Bitmap local;
	if (glyphslot->format != FT_GLYPH_FORMAT_BITMAP)
		return false;
	int size = compute_xrender_bitmap_size(&local, &glyphslot->bitmap, mode, glyph_transform ? &font->info.matrix : NULL);
	if (size < 0)
		return false;
	
	out->metrics.width  = local.width;
	out->metrics.height = local.rows;
	if (0&&transform) {
		// this is broken
		/*
		  FT_Vector vector;
		vector.x = - glyphslot->bitmap_left;
		vector.y =   glyphslot->bitmap_top;
			
		FT_Vector_Transform(&vector, &font->info.matrix);
			
		out->metrics.x = vector.x;
		out->metrics.y = vector.y;*/
	} else {
		out->metrics.x = -glyphslot->bitmap_left;
		out->metrics.y =  glyphslot->bitmap_top;
	}
	
	uint8_t bufBitmap[size]; // I hope there's enough stack space owo
	//memset(bufBitmap, 0, size);
		
	local.buffer = bufBitmap;
		
	if (mode==FT_RENDER_MODE_NORMAL && glyph_transform)
		fill_xrender_bitmap(&local, &glyphslot->bitmap, mode, false, &font->info.matrix);
	else
		fill_xrender_bitmap(&local, &glyphslot->bitmap, mode, font->info.rgba==FC_RGBA_BGR || font->info.rgba==FC_RGBA_VBGR, NULL);
		
	// Copy or convert into local buffer.
	out->picture = 0;
	if (mode == FT_RENDER_MODE_MONO) {
		/* swap bits in each byte */
		if (BitmapBitOrder(W.d) != MSBFirst) {
			FOR (i, size) {
				int c = bufBitmap[i];
				// fancy
				c = (c<<1 & 0xAA) | (c>>1 & 0x55);
				c = (c<<2 & 0xCC) | (c>>2 & 0x33);
				c = (c<<4 & 0xF0) | (c>>4 & 0x0F);
				bufBitmap[i] = c;
			}
		}
	} else if (glyphslot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA || mode != FT_RENDER_MODE_NORMAL) {
		/* invert ARGB <=> BGRA */
		if (ImageByteOrder(W.d) != native_byte_order())
			swap_card32((uint32_t*)bufBitmap, size/4);
	}
	
	XftFormat* format = &xft_formats[font->format];
	
	if (glyphslot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
		//print("rendering image, %d×%d\n", );
		// all of this is just to take data and turn it into a Picture
		Pixmap pixmap = XCreatePixmap(W.d, DefaultRootWindow(W.d), local.width, local.rows, 32);
		// do we need to create a gc each time here
		GC gc = XCreateGC(W.d, pixmap, 0, NULL);
		XImage* image = XCreateImage(W.d, W.vis, 32, ZPixmap, 0, (char*)bufBitmap, local.width, local.rows, 32, 0);
		XPutImage(W.d, pixmap, gc, image, 0, 0, 0, 0, local.width, local.rows);
		out->picture = XRenderCreatePicture(W.d, pixmap, format->format, 0, NULL);
		image->data = NULL; // this is probably safe...
		XDestroyImage(image);
		XFreeGC(W.d, gc);
		XFreePixmap(W.d, pixmap);
		out->type = 2;
	} else {
		int id = format->next_glyph++;
		out->id = id;
		XRenderAddGlyphs(W.d, format->glyphset, (Glyph[]){id}, &out->metrics, 1, (char*)bufBitmap, size);
		out->type = 1;
	}
	out->format = font->format;
	
	return true;
}
