// PART 1: HASH TABLE CACHE TABLE

// 1: map of {character, style} -> {glyph}

typedef struct Xft2Glyph {
	XGlyphInfo metrics;
	void* bitmap;
	size_t bitmap_size;
	Picture picture;
} Xft2Glyph;

typedef struct Xft2Font {
	Xft2Glyph** glyphs;
	int num_glyphs;
	
	GlyphSet	glyphset;
	XRenderPictFormat* format;
} Xft2Font;

// this assumes the glyph has been loaded
void render_glyph(Display* dpy, Picture dst, int x, int y, Picture src, Xft2Font* font, unsigned int id) {
	if (id>=font->num_glyphs || !font->glyphs[id])
		id = 0;
	
	Xft2Glyph* glyph = font->glyphs[id];
	if (glyph->picture) {
		XRenderComposite(dpy, PictOpOver, glyph->picture, None, dst, 0, 0, 0, 0, x-glyph->metrics.x, y-glyph->metrics.y, glyph->metrics.width, glyph->metrics.height);
	} else {
		XRenderCompositeString32(dpy, PictOpOver, src, dst, font->format, font->glyphset, srcx, srcy, x, y, (unsigned int[]){id}, 1);
	}
}

FT_Face lock_file(XftFtFile* f) {
	++f->lock;
	if (!f->face) {
		if (XftDebug() & XFT_DBG_REF)
			printf ("Loading file %s/%d\n", f->file, f->id);
		if (FT_New_Face(_XftFTlibrary, f->file, f->id, &f->face))
			--f->lock;
		
		f->xsize = 0;
		f->ysize = 0;
		f->matrix.xx = f->matrix.xy = f->matrix.yx = f->matrix.yy = 0;
		_XftUncacheFiles();
	}
	return f->face;
}


FT_Face lock_face(Xft2Font* font) {
	XftFontInfo* fi = &font->info;
	FT_Face face = _XftLockFile(fi->file);
	// Make sure the face is usable at the requested size
	if (face && !_XftSetFace(fi->file, fi->xsize, fi->ysize, &fi->matrix)) {
		_XftUnlockFile(fi->file);
		face = NULL;
	}
	return face;
}

void load_glyph(Display* dpy, Xft2Font* font, FcBool need_bitmaps, const FT_UInt* glyphs, int nglyph) {
	XftDisplayInfo* info = _XftDisplayInfoGet(dpy, True);
	if (!info)
		return;
	
	FT_Error	error;
	FT_UInt glyphindex;
	FT_GlyphSlot glyphslot;
	XftGlyph* xftg;
	Glyph	glyph;
	unsigned char bufLocal[4096];
	unsigned char* bufBitmap = bufLocal;
	int bufSize = sizeof(bufLocal);
	int size;
	int width;
	int height;
	int left, right, top, bottom;
	FT_Bitmap* ftbit;
	FT_Bitmap local;
	FT_Vector vector;
	
	FT_Render_Mode mode = FT_RENDER_MODE_MONO;
	FcBool transform;
	FcBool glyph_transform;
	
	FT_Face face = XftLockFace(font->public);
	
	if (!face)
		return;
	
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

	transform = font->info.transform && mode != FT_RENDER_MODE_MONO;

	while (nglyph--) {
		glyphindex = *glyphs++;
		xftg = font->glyphs[glyphindex];
		if (!xftg)
			continue;

		if (XftDebug() & XFT_DBG_CACHE)
			_XftFontValidateMemory (dpy, pub);
		/*
		 * Check to see if this glyph has just been loaded,
		 * this happens when drawing the same glyph twice
		 * in a single string
		 */
		if (xftg->glyph_memory)
			continue;

		FT_Library_SetLcdFilter( _XftFTlibrary, font->info.lcd_filter);

		error = FT_Load_Glyph (face, glyphindex, font->info.load_flags);
		if (error) {
			/*
			 * If anti-aliasing or transforming glyphs and
			 * no outline version exists, fallback to the
			 * bitmap and let things look bad instead of
			 * missing the glyph
			 */
			if (font->info.load_flags & FT_LOAD_NO_BITMAP)
				error = FT_Load_Glyph (face, glyphindex,
				                       font->info.load_flags & ~FT_LOAD_NO_BITMAP);
			if (error)
				continue;
		}

#define FLOOR(x)    ((x) & -64)
#define CEIL(x)	    (((x)+63) & -64)
#define TRUNC(x)    ((x) >> 6)
#define ROUND(x)    (((x)+32) & -64)

		glyphslot = face->glyph;

		/*
		 * Embolden if required
		 */
		if (font->info.embolden) FT_GlyphSlot_Embolden(glyphslot);

		/*
		 * Compute glyph metrics from FreeType information
		 */
		if (transform) {
			/*
			 * calculate the true width by transforming all four corners.
			 */
			int xc, yc;
			left = right = top = bottom = 0;
			for(xc = 0; xc <= 1; xc ++) {
				for(yc = 0; yc <= 1; yc++) {
					vector.x = glyphslot->metrics.horiBearingX + xc * glyphslot->metrics.width;
					vector.y = glyphslot->metrics.horiBearingY - yc * glyphslot->metrics.height;
					FT_Vector_Transform(&vector, &font->info.matrix);
					if (XftDebug() & XFT_DBG_GLYPH)
						printf("Trans %d %d: %d %d\n", (int) xc, (int) yc,
						       (int) vector.x, (int) vector.y);
					if(xc == 0 && yc == 0) {
						left = right = vector.x;
						top = bottom = vector.y;
					} else {
						if(left > vector.x) left = vector.x;
						if(right < vector.x) right = vector.x;
						if(bottom > vector.y) bottom = vector.y;
						if(top < vector.y) top = vector.y;
					}

				}
			}
			left = FLOOR(left);
			right = CEIL(right);
			bottom = FLOOR(bottom);
			top = CEIL(top);

		} else {
			left  = FLOOR( glyphslot->metrics.horiBearingX );
			right = CEIL( glyphslot->metrics.horiBearingX + glyphslot->metrics.width );

			top    = CEIL( glyphslot->metrics.horiBearingY );
			bottom = FLOOR( glyphslot->metrics.horiBearingY - glyphslot->metrics.height );
		}

		width = TRUNC(right - left);
		height = TRUNC( top - bottom );

		/*
		 * Clip charcell glyphs to the bounding box
		 * XXX transformed?
		 */
		if (font->info.spacing >= FC_CHARCELL && !transform) {
			if (font->info.load_flags & FT_LOAD_VERTICAL_LAYOUT) {
				if (TRUNC(bottom) > font->public.max_advance_width) {
					int adjust;

					adjust = bottom - (font->public.max_advance_width << 6);
					if (adjust > top)
						adjust = top;
					top -= adjust;
					bottom -= adjust;
					height = font->public.max_advance_width;
				}
			}
			else {
				if (TRUNC(right) > font->public.max_advance_width) {
					int adjust;

					adjust = right - (font->public.max_advance_width << 6);
					if (adjust > left)
						adjust = left;
					left -= adjust;
					right -= adjust;
					width = font->public.max_advance_width;
				}
			}
		}

		glyph_transform = transform;
		if ( glyphslot->format != FT_GLYPH_FORMAT_BITMAP ) {
			error = FT_Render_Glyph( face->glyph, mode );
			if (error)
				continue;
			glyph_transform = False;
		}
		
		FT_Library_SetLcdFilter( _XftFTlibrary, FT_LCD_FILTER_NONE );
		
		if (font->info.spacing >= FC_MONO) {
			if (transform) {
				if (font->info.load_flags & FT_LOAD_VERTICAL_LAYOUT) {
					vector.x = 0;
					vector.y = -face->size->metrics.max_advance;
				}
				else {
					vector.x = face->size->metrics.max_advance;
					vector.y = 0;
				}
				FT_Vector_Transform (&vector, &font->info.matrix);
				xftg->metrics.xOff = vector.x >> 6;
				xftg->metrics.yOff = -(vector.y >> 6);
			}
			else {
				if (font->info.load_flags & FT_LOAD_VERTICAL_LAYOUT) {
					xftg->metrics.xOff = 0;
					xftg->metrics.yOff = -font->public.max_advance_width;
				}
				else {
					xftg->metrics.xOff = font->public.max_advance_width;
					xftg->metrics.yOff = 0;
				}
			}
		}
		else {
p			xftg->metrics.xOff = TRUNC(ROUND(glyphslot->advance.x));
			xftg->metrics.yOff = -TRUNC(ROUND(glyphslot->advance.y));
		}

		// compute the size of the final bitmap
		ftbit = &glyphslot->bitmap;

		width = ftbit->width;
		height = ftbit->rows;

		if (XftDebug() & XFT_DBG_GLYPH) {
			printf ("glyph %d:\n", (int) glyphindex);
			printf (" xywh (%d %d %d %d), trans (%d %d %d %d) wh (%d %d)\n",
			        (int) glyphslot->metrics.horiBearingX,
			        (int) glyphslot->metrics.horiBearingY,
			        (int) glyphslot->metrics.width,
			        (int) glyphslot->metrics.height,
			        left, right, top, bottom,
			        width, height);
			if (XftDebug() & XFT_DBG_GLYPHV) {
				int		x, y;
				unsigned char	*line;

				line = ftbit->buffer;
				if (ftbit->pitch < 0)
					line -= ftbit->pitch*(height-1);

				for (y = 0; y < height; y++) {
					if (font->info.antialias) {
						static const char    den[] = { " .:;=+*#" };
						for (x = 0; x < width; x++)
							printf ("%c", den[line[x] >> 5]);
					}
					else {
						for (x = 0; x < width * 8; x++) {
							printf ("%c", line[x>>3] & (1 << (x & 7)) ? '#' : ' ');
						}
					}
					printf ("|\n");
					line += ftbit->pitch;
				}
				printf ("\n");
			}
		}

		size = _compute_xrender_bitmap_size( &local, glyphslot, mode, glyph_transform ? &font->info.matrix : NULL );
		if ( size < 0 )
			continue;

		xftg->metrics.width  = local.width;
		xftg->metrics.height = local.rows;
		if (transform) {
			vector.x = - glyphslot->bitmap_left;
			vector.y =   glyphslot->bitmap_top;

			FT_Vector_Transform(&vector, &font->info.matrix);

			xftg->metrics.x = vector.x;
			xftg->metrics.y = vector.y;
		}
		else {
			xftg->metrics.x = - glyphslot->bitmap_left;
			xftg->metrics.y =   glyphslot->bitmap_top;
		}

		/*
		 * If the glyph is relatively large (> 1% of server memory),
		 * don't send it until necessary.
		 */
		if (!need_bitmaps && size > info->max_glyph_memory / 100)
			continue;

		/*
		 * Make sure there is enough buffer space for the glyph.
		 */
		if (size > bufSize) {
			if (bufBitmap != bufLocal)
				free (bufBitmap);
			bufBitmap = (unsigned char *) malloc (size);
			if (!bufBitmap)
				continue;
			bufSize = size;
		}
		memset (bufBitmap, 0, size);

		local.buffer = bufBitmap;

		if (mode == FT_RENDER_MODE_NORMAL && glyph_transform)
			_scaled_fill_xrender_bitmap(&local, &glyphslot->bitmap, &font->info.matrix);
		else
			_fill_xrender_bitmap( &local, glyphslot, mode,
			                      (font->info.rgba == FC_RGBA_BGR ||
			                       font->info.rgba == FC_RGBA_VBGR ) );

		/*
		 * Copy or convert into local buffer.
		 */

		/*
		 * Use the glyph index as the wire encoding; it
		 * might be more efficient for some locales to map
		 * these by first usage to smaller values, but that
		 * would require persistently storing the map when
		 * glyphs were freed.
		 */
		glyph = (Glyph) glyphindex;

		xftg->picture = 0;
		xftg->glyph_memory = size + sizeof (XftGlyph);
		if (font->format) {
			if (!font->glyphset)
				font->glyphset = XRenderCreateGlyphSet (dpy, font->format);
			if ( mode == FT_RENDER_MODE_MONO ) {
				/* swap bits in each byte */
				if (BitmapBitOrder (dpy) != MSBFirst) {
					unsigned char   *line = (unsigned char*)bufBitmap;
					int		    i = size;

					while (i--) {
						int c = *line;
						c = ((c << 1) & 0xaa) | ((c >> 1) & 0x55);
						c = ((c << 2) & 0xcc) | ((c >> 2) & 0x33);
						c = ((c << 4) & 0xf0) | ((c >> 4) & 0x0f);
						*line++ = c;
					}
				}
			}
			else if (glyphslot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA || mode != FT_RENDER_MODE_NORMAL) {
				/* invert ARGB <=> BGRA */
				if (ImageByteOrder (dpy) != XftNativeByteOrder ())
					XftSwapCARD32 ((CARD32 *) bufBitmap, size >> 2);
			}

			if (glyphslot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
				Pixmap pixmap = XCreatePixmap(dpy, DefaultRootWindow(dpy), local.width, local.rows, 32);
				GC gc = XCreateGC(dpy, pixmap, 0, NULL);
				XImage image = {
					local.width, local.rows, 0, ZPixmap, (char *)bufBitmap,
					dpy->byte_order, dpy->bitmap_unit, dpy->bitmap_bit_order, 32,
					32, local.width * 4 - local.pitch, 32,
					0, 0, 0
				};

				XInitImage(&image);
				XPutImage(dpy, pixmap, gc, &image, 0, 0, 0, 0, local.width, local.rows);
				xftg->picture = XRenderCreatePicture(dpy, pixmap, font->format, 0, NULL);

				XFreeGC(dpy, gc);
				XFreePixmap(dpy, pixmap);
			}
			else
				XRenderAddGlyphs (dpy, font->glyphset, &glyph,
				                  &xftg->metrics, 1,
				                  (char *) bufBitmap, size);
		}
		else {
			if (size) {
				xftg->bitmap = malloc (size);
				if (xftg->bitmap)
					memcpy (xftg->bitmap, bufBitmap, size);
			}
			else
				xftg->bitmap = NULL;
		}

		font->glyph_memory += xftg->glyph_memory;
		info->glyph_memory += xftg->glyph_memory;
		if (XftDebug() & XFT_DBG_CACHE)
			_XftFontValidateMemory (dpy, pub);
		if (XftDebug() & XFT_DBG_CACHEV)
			printf ("Caching glyph 0x%x size %ld\n", glyphindex,
			        xftg->glyph_memory);
	}
	if (bufBitmap != bufLocal)
		free (bufBitmap);
	XftUnlockFace (&font->public);
}

void get_glyph(Char chr, int style) {
	// 1: look up in hash table
}
