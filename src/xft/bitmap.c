#include "xftint.h"
#include <ft2build.h>
#include FT_OUTLINE_H
#include FT_LCD_FILTER_H

#include FT_SYNTHESIS_H

#include FT_GLYPH_H

// messy
static inline uint32_t pack(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
	return a | b<<8 | c<<16 | d<<24;
}
static int round_up(int n, int to) {
	n += to-1;
	n /= to;
	n *= to;
	return n;
}

/* 
we sometimes need to convert the glyph bitmap
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
int compute_xrender_bitmap_size(
	FT_Bitmap* target, // target bitmap descriptor. The function will set its 'width', 'rows' and 'pitch' fields, and only these
	const FT_Bitmap* ftbit, // source bitmap.
	FT_Render_Mode mode, // the requested final rendering mode. supported values are MONO, NORMAL (i.e. gray), LCD and LCD_V
	const FT_Matrix* matrix
) {
	// compute the size of the final bitmap
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
this function converts the glyph bitmap
into a different format while scaling by applying the given matrix
(see compute_xrender_bitmap_size)

you should call this function after compute_xrender_bitmap_size
*/
//  vcectors ...
void scaled_fill_xrender_bitmap(
	FT_Bitmap* target, // target bitmap descriptor. Note that its 'buffer' pointer must point to memory allocated by the caller
	const FT_Bitmap* source, // the source bitmap descriptor
	FT_Render_Mode mode, // unused
	bool bgr, // unused
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
  this function converts the glyph bitmap
  into a different format (see compute_xrender_bitmap_size)
 
  you should call this function after compute_xrender_bitmap_size
*/
// gosh how much of this is really necessary though?
void fill_xrender_bitmap(
	FT_Bitmap* target, // target bitmap descriptor. Note that its 'buffer' pointer must point to memory allocated by the caller
	const FT_Bitmap* ftbit, // source bitmap
	FT_Render_Mode mode, // the requested final rendering mode
	bool bgr, // boolean, set if BGR or VBGR pixel ordering is needed
	const FT_Matrix* matrix // unused
) {
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
