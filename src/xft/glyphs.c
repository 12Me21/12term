#include "xftint.h"
#include <ft2build.h>
#include FT_OUTLINE_H
#include FT_LCD_FILTER_H

#include FT_SYNTHESIS_H

#include FT_GLYPH_H

// messy
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
static int round_up(int n, int to) {
	n += to-1;
	n /= to;
	n *= to;
	return n;
}

/* 
we sometimes need to convert the glyph bitmap in a FT_GlyphSlot
into a different format. For example, we want to convert a
FT_PIXEL_MODE_LCD or FT_PIXEL_MODE_LCD_V bitmap into a 32-bit
ARGB or ABGR bitmap.

this function prepares a target descriptor for this operation.

the function returns the size in bytes of the corresponding buffer,
it's up to the caller to allocate the corresponding memory block
before calling _fill_xrender_bitmap

it also returns -1 in case of error (e.g. incompatible arguments,
like trying to convert a gray bitmap into a monochrome one)
*/
static int _compute_xrender_bitmap_size(
	FT_Bitmap* target, // target bitmap descriptor. The function will set its 'width', 'rows' and 'pitch' fields, and only these
	FT_GlyphSlot slot, // the glyph slot containing the source bitmap. this function assumes that slot->format == FT_GLYPH_FORMAT_BITMAP
	FT_Render_Mode mode, // the requested final rendering mode. supported values are MONO, NORMAL (i.e. gray), LCD and LCD_V
	FT_Matrix* matrix
) {
	if (slot->format != FT_GLYPH_FORMAT_BITMAP)
		return -1;
	
	// compute the size of the final bitmap
	FT_Bitmap* ftbit = &slot->bitmap;
	int width = ftbit->width;
	int height = ftbit->rows;
	
	if (matrix && mode == FT_RENDER_MODE_NORMAL) {
		FT_Vector vector = {
			.x = ftbit->width,
			.y = ftbit->rows,
		};
		FT_Vector_Transform(&vector, matrix);
		width = vector.x;
		height = vector.y;
	}
	int pitch;
	
	switch (ftbit->pixel_mode) {
	case FT_PIXEL_MODE_MONO:
		if (mode == FT_RENDER_MODE_MONO) {
			pitch = round_up(width, 32) / 8;
			break;
		}
		/* fall-through */
	case FT_PIXEL_MODE_GRAY:
		if (mode == FT_RENDER_MODE_LCD || mode == FT_RENDER_MODE_LCD_V) {
			// each pixel is replicated into a 32-bit ARGB value
			pitch = width * 4;
		} else {
			pitch = round_up(width, 4);
		}
		break;
	case FT_PIXEL_MODE_BGRA:
		pitch = width * 4;
		break;
	case FT_PIXEL_MODE_LCD:
		if (mode != FT_RENDER_MODE_LCD)
			return -1;
		// horz pixel triplets are packed into 32-bit ARGB values
		width /= 3;
		pitch = width * 4;
		break;
	case FT_PIXEL_MODE_LCD_V:
		if (mode != FT_RENDER_MODE_LCD_V)
			return -1;
		// vert pixel triplets are packed into 32-bit ARGB values
		height /= 3;
		pitch = width * 4;
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

/*
this function converts the glyph bitmap found in a FT_GlyphSlot
into a different format while scaling by applying the given matrix
(see _compute_xrender_bitmap_size)

you should call this function after _compute_xrender_bitmap_size
*/
//  vcectors ...
static void _scaled_fill_xrender_bitmap(
	FT_Bitmap* target, // target bitmap descriptor. Note that its 'buffer' pointer must point to memory allocated by the caller
	FT_Bitmap* source, // the source bitmap descriptor
	const FT_Matrix* matrix // the scaling matrix to apply
) {
	uint8_t* src_buf = source->buffer;
	int src_pitch = source->pitch;
	if (src_pitch<0)
		src_buf -= src_pitch*(source->rows-1);
	
	FT_Matrix inverse = *matrix;
	FT_Matrix_Invert(&inverse);
	
	// compute how many source pixels a target pixel spans
	FT_Vector vector = {.x=1, .y=1};
	FT_Vector_Transform(&vector, &inverse);
	int sampling_width = vector.x / 2;
	int sampling_height = vector.y / 2;
	int sample_count = (2*sampling_width + 1) * (2*sampling_height + 1);
	
	int width = target->width;
	int height = target->rows;
	int pitch = target->pitch;
	uint8_t* dst_line = target->buffer;
	FOR (y, height) {
		FOR (x, width) {
			// compute target pixel location in source space
			vector.x = x*0x10000 + 0x10000/2;
			vector.y = y*0x10000 + 0x10000/2;
			FT_Vector_Transform(&vector, &inverse);
			vector.x = limit(FT_RoundFix(vector.x)/0x10000, 0, source->width - 1);
			vector.y = limit(FT_RoundFix(vector.y)/0x10000, 0, source->rows  - 1);
			
			uint8_t* src;
			switch (source->pixel_mode) {
				// convert mono to 8-bit gray, scale using nearest pixel
			case FT_PIXEL_MODE_MONO: 
				src = src_buf + (vector.y*src_pitch);
				if (src[vector.x>>3] & (0x80 >> (vector.x & 7)) )
					dst_line[x] = 0xFF;
				break;
				// scale using nearest pixel
			case FT_PIXEL_MODE_GRAY: 
				src = src_buf + (vector.y * src_pitch);
				dst_line[x] = src[vector.x];
				break;
				// scale by averaging all relevant source pixels, keep BGRA format
			case FT_PIXEL_MODE_BGRA: {
				int bgra[4] = {0};
				for (int sample_y = -sampling_height; sample_y < sampling_height + 1; ++sample_y) {
					int src_y = limit(vector.y + sample_y, 0, source->rows - 1);
					src = src_buf + (src_y * src_pitch);
					for (int sample_x = -sampling_width; sample_x < sampling_width + 1; ++sample_x) {
						int src_x = limit(vector.x + sample_x, 0, source->width - 1);
						FOR (i, 4) {
							bgra[i] += src[src_x*4+i];
						}
					}
				}
				FOR (i, 4) {
					dst_line[4*x+i] = bgra[i]/sample_count;
				}
				break;
			}
			}
		}
		dst_line+=pitch;
	}
}

/* 
  this function converts the glyph bitmap found in a FT_GlyphSlot
  into a different format (see _compute_xrender_bitmap_size)
 
  you should call this function after _compute_xrender_bitmap_size
*/
// gosh how much of this is really necessary though?
static void _fill_xrender_bitmap(
	FT_Bitmap* target, // target bitmap descriptor. Note that its 'buffer' pointer must point to memory allocated by the caller
	FT_GlyphSlot slot, // the glyph slot containing the source bitmap
	FT_Render_Mode mode, // the requested final rendering mode
	const int bgr // boolean, set if BGR or VBGR pixel ordering is needed
) {
	const FT_Bitmap* ftbit = &slot->bitmap;
	const int src_pitch = ftbit->pitch;
	uint8_t*	srcLine = ftbit->buffer;
	if (src_pitch < 0)
		srcLine -= src_pitch*(ftbit->rows-1);
	
	const int width = target->width;
	const int height = target->rows;
	const int pitch = target->pitch;
	uint8_t* dstLine = target->buffer;
	const int subpixel = (mode==FT_RENDER_MODE_LCD || mode==FT_RENDER_MODE_LCD_V );
	// the compiler should optimize this by moving the for loop inside the switch block
	FOR (y, height) {
		uint32_t* const dst = (uint32_t*)dstLine;
		switch (ftbit->pixel_mode) {
		case FT_PIXEL_MODE_MONO:
			// convert mono to ARGB32 values
			if (subpixel) { 
				FOR (x, width) {
					if (srcLine[x/8] & (0x80 >> x%8))
						dst[x] = 0xffffffffU;
				}
				// convert mono to 8-bit gray
			} else if (mode == FT_RENDER_MODE_NORMAL) {
				FOR (x, width) {
					if (srcLine[x/8] & (0x80 >> x%8))
						dstLine[x] = 0xff;
				}
				// copy mono to mono
			} else {
				memcpy(dstLine, srcLine, (width+7)/8);
			}
			break;
		case FT_PIXEL_MODE_GRAY:
			// convert gray to ARGB32 values
			if (subpixel) {
				FOR (x, width) {
					dst[x] = pack(srcLine[x], srcLine[x], srcLine[x], srcLine[x]);
				}
				// copy gray into gray
			} else {
				memcpy(dstLine, srcLine, width);
			}
			break;
		case FT_PIXEL_MODE_BGRA: 
			// Preserve BGRA format
			memcpy(dstLine, srcLine, width*4);
			break;
		case FT_PIXEL_MODE_LCD:
			FOR (x, width) {
			// convert horizontal RGB into ARGB32
				if (!bgr)
					dst[x] = pack(srcLine[x*3+2], srcLine[x*3+1], srcLine[x*3], srcLine[x*3+1]); // is this supposed to be 3?
				else
					dst[x] = pack(srcLine[x*3], srcLine[x*3+1], srcLine[x*3+2], srcLine[x*3+1]);
			}
			break;
		case FT_PIXEL_MODE_LCD_V:
			FOR (x, width) {
				if (!bgr)
					dst[x] = pack(srcLine[x+src_pitch*2], srcLine[x+src_pitch], srcLine[x], srcLine[x+src_pitch]); // repeated values here are not a typo, i checked carefully
				else
					dst[x] = pack(srcLine[x], srcLine[x+src_pitch], srcLine[x+src_pitch*2], srcLine[x+src_pitch]);
			}
			srcLine += (3-1)*src_pitch; // adjust for vertical pixels
		}
		srcLine += src_pitch;
		dstLine += pitch;
	}
}

static int dot6_to_int(FT_F26Dot6 x) {
	return x>>6;
}
static int dot6_floor(FT_F26Dot6 x) {
	return x & ~63;
}
static int dot6_round(FT_F26Dot6 x) {
	return x+32 & ~63;
}
static int dot6_ceil(FT_F26Dot6 x) {
	return x+63 & ~63;
}

bool load_glyph(XftFont* font, Char chr, GlyphData* out) {
	FT_Face face = xft_lock_face(font);
	if (!face)
		return false;
	
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
	
	// lookup glyph
	FT_UInt glyphindex = FcFreeTypeCharIndex(face, chr);
	
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
			return false;
	}
	
	FT_GlyphSlot glyphslot = face->glyph;
		
	// Embolden if required
	if (font->info.embolden)
		FT_GlyphSlot_Embolden(glyphslot);
	
	// Compute glyph metrics from FreeType information
	FT_F26Dot6 left, right, top, bottom;
	FT_Glyph_Metrics* metrics = &glyphslot->metrics;
	if (transform) {
		// calculate the true width by transforming all four corners.
		FOR (xc, 2) {
			FOR (yc, 2) {
				FT_Vector vector = {
					.x = metrics->horiBearingX + xc * metrics->width,
					.y = metrics->horiBearingY - yc * metrics->height,
				};
				FT_Vector_Transform(&vector, &font->info.matrix);
				if (XftDebug() & XFT_DBG_GLYPH)
					print("Trans %d %d: %d %d\n", (int) xc, (int) yc,
					      (int) vector.x, (int) vector.y);
				if (xc == 0 && yc == 0) {
					left = right = vector.x;
					top = bottom = vector.y;
				} else {
					if (left > vector.x) left = vector.x;
					if (top < vector.y) top = vector.y;
					if (right < vector.x) right = vector.x;
					if (bottom > vector.y) bottom = vector.y;
				}
			}
		}
		// lots of rounding going on here... kinda sus
	} else {
		left = metrics->horiBearingX;
		top = metrics->horiBearingY;
		right = metrics->horiBearingX + metrics->width;
		bottom = metrics->horiBearingY - metrics->height;
	}
	left = dot6_floor(left);
	top = dot6_ceil(top);
	bottom = dot6_floor(bottom);
	right = dot6_ceil(right);
	
	//int width = dot6_to_int(right - left);
	//int height = dot6_to_int(top - bottom);
	
	// Clip charcell glyphs to the bounding box
	// XXX transformed?
	if (font->info.spacing >= FC_CHARCELL && !transform) {
		if (dot6_to_int(right) > font->max_advance_width) {
			int adjust = right - (font->max_advance_width << 6);
			if (adjust > left) adjust = left;
			left -= adjust;
			right -= adjust;
			//width = font->max_advance_width;
		}
	}
	
	// ok I'm confused.
	// when is this width value ever used?
	
	bool glyph_transform = transform;
	if (glyphslot->format != FT_GLYPH_FORMAT_BITMAP) {
		error = FT_Render_Glyph(face->glyph, mode);
		if (error) {
			// uh oh
			return false;
			//die("error rendering glyph\n");
		}
		glyph_transform = false;
	}
	
	FT_Library_SetLcdFilter(ft_library, FT_LCD_FILTER_NONE);
	
	if (font->info.spacing >= FC_MONO) {
		if (transform) {
			FT_Vector vector = {
				.x = face->size->metrics.max_advance,
				.y = 0,
			};
			FT_Vector_Transform(&vector, &font->info.matrix);
			out->metrics.xOff = vector.x >> 6;
			out->metrics.yOff = -(vector.y >> 6);
		} else {
			out->metrics.xOff = font->max_advance_width;
			out->metrics.yOff = 0;
		}
	} else {
		out->metrics.xOff = dot6_to_int(dot6_round(glyphslot->advance.x));
		out->metrics.yOff = -dot6_to_int(dot6_round(glyphslot->advance.y));
	}
	
	// compute the size of the final bitmap
	FT_Bitmap* ftbit = &glyphslot->bitmap;
	
	int width = ftbit->width;
	int height = ftbit->rows;
	
	if (XftDebug() & XFT_DBG_GLYPH) {
		print("glyph %d:\n", (int) glyphindex);
		print(" xywh (%d %d %d %d), trans (%ld %ld %ld %ld) wh (%d %d)\n",
		      (int) metrics->horiBearingX,
		      (int) metrics->horiBearingY,
		      (int) metrics->width,
		      (int) metrics->height,
		      left, right, top, bottom,
		      width, height);
		if (XftDebug() & XFT_DBG_GLYPHV) {
			uint8_t* line = ftbit->buffer;
			if (ftbit->pitch < 0)
				line -= ftbit->pitch*(height-1);
				
			FOR (y, height) {
				if (font->info.antialias) {
					const utf8* den = " .:;=+*#";
					FOR (x, width) {
						print("%c", den[line[x] >> 5]);
					}
				} else {
					FOR (x, width*8) {
						print("%c", line[x/8] & (1<<x%8) ? '#' : ' ');
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
		out->metrics.x = - glyphslot->bitmap_left;
		out->metrics.y =   glyphslot->bitmap_top;
	}
	
	uint8_t bufBitmap[size]; // I hope there's enough stack space owo
	memset(bufBitmap, 0, size);
		
	local.buffer = bufBitmap;
		
	if (mode == FT_RENDER_MODE_NORMAL && glyph_transform)
		_scaled_fill_xrender_bitmap(&local, &glyphslot->bitmap, &font->info.matrix);
	else
		_fill_xrender_bitmap(&local, glyphslot, mode, font->info.rgba==FC_RGBA_BGR || font->info.rgba==FC_RGBA_VBGR);
		
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
		// all of this is just to take data and turn it into a Picture
		Pixmap pixmap = XCreatePixmap(W.d, DefaultRootWindow(W.d), local.width, local.rows, 32);
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
