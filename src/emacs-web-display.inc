/* emacs-web completed display ABI 1, pinned to GNU Emacs 30.2.
   SPDX-License-Identifier: GPL-3.0-or-later
   Included by xdisp.c; see native/README.md in emacs-web. */
#if !defined HAVE_X_WINDOWS || !defined USE_CAIRO
#error "emacs-web display ABI 1 requires X11 and Cairo"
#endif

/* Cache only bytes owned by the immutable completed Cairo pattern.  Emacs
   replaces the pattern when an image is reloaded/transformed/animated; its
   destructor releases this cache.  No Lisp reference lives in Cairo data. */
struct ew_web_png {
  struct ew_web_png *next;
  unsigned char *bytes;
  size_t length;
  int x, y, width, height;
  unsigned long foreground;
};
static const cairo_user_data_key_t ew_web_png_key;
static void
ew_web_free_png (void *data)
{
  struct ew_web_png *entry = data;
  while (entry) {
    struct ew_web_png *next = entry->next;
    xfree (entry->bytes); xfree (entry); entry = next;
  }
}
static cairo_status_t
ew_web_write_png (void *data, const unsigned char *bytes, unsigned int length)
{
  struct ew_web_png *entry = data;
  if (entry->length + length > 16 * 1024 * 1024)
    return CAIRO_STATUS_WRITE_ERROR;
  entry->bytes = xrealloc (entry->bytes, entry->length + length);
  memcpy (entry->bytes + entry->length, bytes, length);
  entry->length += length;
  return CAIRO_STATUS_SUCCESS;
}
static Lisp_Object
ew_web_image (struct frame *f, struct image *image, struct glyph *g)
{
  if (!image)
    error ("Emacs Web: missing completed image resource");
  /* x_draw_image_foreground draws a rectangle when no decoded pattern is
     available.  Preserve that completed display state, not a capture error. */
  if (!image->cr_data)
    return Qnil;
  int x = g->slice.img.x, y = g->slice.img.y;
  int width = g->slice.img.width, height = g->slice.img.height;
  if (width <= 0 || height <= 0 || (int64_t) width * height > 4 * 1024 * 1024)
    error ("Emacs Web: image slice exceeds 4 megapixel resource limit");
  cairo_pattern_t *pattern = image->cr_data;
  struct face *face = FACE_FROM_ID (f, g->face_id);
  unsigned long foreground = face->foreground;
  struct ew_web_png *head = cairo_pattern_get_user_data (pattern, &ew_web_png_key);
  struct ew_web_png *entry;
  int count = 0;
  for (entry = head; entry; entry = entry->next) {
    if (entry->x == x && entry->y == y && entry->width == width
        && entry->height == height && entry->foreground == foreground)
      return make_unibyte_string ((char *) entry->bytes, entry->length);
    ++count;
  }
  /* Bound retained slices during scrolling, not just each PNG allocation. */
  if (count >= 8) {
    cairo_pattern_set_user_data (pattern, &ew_web_png_key, NULL, NULL);
    head = NULL;
  }
  entry = xzalloc (sizeof *entry);
  entry->x = x; entry->y = y; entry->width = width; entry->height = height;
  entry->foreground = foreground;
  cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create (surface);
  cairo_translate (cr, -x, -y);
  cairo_surface_t *source;
  cairo_pattern_get_surface (pattern, &source);
  cairo_format_t format = cairo_image_surface_get_format (source);
  if (format == CAIRO_FORMAT_A8 || format == CAIRO_FORMAT_A1) {
    XColor color = { .pixel = foreground };
    x_query_colors (f, &color, 1);
    cairo_set_source_rgb (cr, color.red / 65535.0, color.green / 65535.0,
                         color.blue / 65535.0);
    cairo_mask (cr, pattern);
  } else {
    cairo_set_source (cr, pattern);
    cairo_paint (cr);
  }
  cairo_status_t status = cairo_status (cr);
  if (status == CAIRO_STATUS_SUCCESS)
    status = cairo_surface_write_to_png_stream (surface, ew_web_write_png, entry);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
  if (status != CAIRO_STATUS_SUCCESS) {
    ew_web_free_png (entry);
    error ("Emacs Web: cannot encode image resource");
  }
  /* Replace the list without invoking its old destructor: Cairo destroys
     old user data on replacement, so detach ownership before reattaching. */
  if (head) {
    /* Store one stable list owner; adding nodes must not replace user data. */
    entry->next = head->next;
    head->next = entry;
  } else if (cairo_pattern_set_user_data (pattern, &ew_web_png_key, entry,
                                         ew_web_free_png) != CAIRO_STATUS_SUCCESS) {
    ew_web_free_png (entry);
    error ("Emacs Web: cannot retain image resource");
  }
  return make_unibyte_string ((char *) entry->bytes, entry->length);
}

static Lisp_Object
ew_web_copy_vectors (Lisp_Object value)
{
  if (!VECTORP (value)) return value;
  Lisp_Object copy = Fcopy_sequence (value);
  for (ptrdiff_t i = 0; i < ASIZE (copy); ++i)
    ASET (copy, i, ew_web_copy_vectors (AREF (copy, i)));
  return copy;
}
/* No Lisp evaluation, redisplay, font lookup, or GC safe point is allowed
   while traversing native storage.  Allocations alone do not run Emacs GC.
   The returned graph roots every source/font; no C pointer or cache ID escapes.
   Consumers must serialize synchronously before yielding to the command loop. */
static Lisp_Object
ew_web_window (Lisp_Object window)
{
  struct window *w = XWINDOW (window);
  struct frame *f = WINDOW_XFRAME (w);
  struct glyph_matrix *matrix = w->current_matrix;
  Lisp_Object rows = make_vector (matrix->nrows, Qnil);
  Lisp_Object faces = make_vector (FRAME_FACE_CACHE (f)->used, Qnil);
  Lisp_Object compositions = Qnil;
  for (int r = 0; r < matrix->nrows; ++r)
    {
      struct glyph_row *row = MATRIX_ROW (matrix, r);
      /* Header/tab rows legitimately have visible_height == 0: their
         native rectangle is row->height, not the body-row clipping field. */
      if (!row->enabled_p || (!row->mode_line_p && row->visible_height <= 0)) continue;
      Lisp_Object areas = make_vector (LAST_AREA, Qnil);
      for (int a = 0; a < LAST_AREA; ++a)
        {
          Lisp_Object glyphs = make_vector (row->used[a], Qnil);
          for (int i = 0; i < row->used[a]; ++i)
            {
              struct glyph *g = row->glyphs[a] + i;
              Lisp_Object payload = Qnil;
              Lisp_Object slice = Qnil;
              ptrdiff_t source_length = 1;
              if (g->type == CHAR_GLYPH) payload = make_fixnum (g->u.ch);
              else if (g->type == COMPOSITE_GLYPH)
                {
                  Lisp_Object key = make_fixnum (g->u.cmp.id * 2 + g->u.cmp.automatic);
                  Lisp_Object cached = Fassq (key, compositions);
                  if (CONSP (cached)) payload = XCDR (cached);
                  else {
                  if (g->u.cmp.automatic)
                    {
                      payload = ew_web_copy_vectors (composition_gstring_from_id (g->u.cmp.id));
                      /* Cache IDs are not stable resource identities. */
                      ASET (payload, 1, Qnil);
                    }
                  else
                    {
                      struct composition *cmp = composition_table[g->u.cmp.id];
                      Lisp_Object offsets = make_vector (cmp->glyph_len * 2, Qnil);
                      for (int k = 0; k < cmp->glyph_len * 2; ++k)
                        ASET (offsets, k, make_fixnum (cmp->offsets[k]));
                      Lisp_Object data[] = {make_fixnum (cmp->method),
                        ew_web_copy_vectors (cmp->key), offsets};
                      payload = Fvector (ARRAYELTS (data), data);
                    }
                    compositions = Fcons (Fcons (key, payload), compositions);
                  }
                  if (g->u.cmp.automatic) {
                    Lisp_Object first = LGSTRING_GLYPH (payload, g->slice.cmp.from);
                    Lisp_Object last = LGSTRING_GLYPH (payload, g->slice.cmp.to);
                    source_length = LGLYPH_TO (last) - LGLYPH_FROM (first) + 1;
                  } else if (STRINGP (g->object) || BUFFERP (g->object)) {
                    Lisp_Object prop = Fget_text_property (make_fixnum (g->charpos),
                                                          Qcomposition, g->object);
                    if (CONSP (prop)) source_length = COMPOSITION_LENGTH (prop);
                  }
                  slice = list3 (g->u.cmp.automatic ? Qt : Qnil,
                                 make_fixnum (g->slice.cmp.from),
                                 make_fixnum (g->slice.cmp.to));
                }
              else if (g->type == IMAGE_GLYPH)
                {
                  struct image *image = IMAGE_OPT_FROM_ID (f, g->u.img_id);
                  if (!image) error ("Emacs Web: missing completed image resource");
                  Lisp_Object image_data[] = {image ? image->spec : Qnil,
                    ew_web_image (f, image, g), make_fixnum (image->hmargin),
                    make_fixnum (image->vmargin), make_fixnum (image->relief)};
                  payload = Fvector (ARRAYELTS (image_data), image_data);
                  slice = list4 (make_fixnum (g->slice.img.x), make_fixnum (g->slice.img.y),
                                 make_fixnum (g->slice.img.width), make_fixnum (g->slice.img.height));
                }
              else if (g->type == GLYPHLESS_GLYPH) {
                Lisp_Object text = Qnil;
                if (g->u.glyphless.method == GLYPHLESS_DISPLAY_HEX_CODE) {
                  char hex[7];
                  sprintf (hex, "%0*X", g->u.glyphless.ch < 0x10000 ? 4 : 6,
                           g->u.glyphless.ch);
                  text = build_string (hex);
                } else if (g->u.glyphless.method == GLYPHLESS_DISPLAY_ACRONYM
                           && CHAR_TABLE_P (Vglyphless_char_display)
                           && CHAR_TABLE_EXTRA_SLOTS (XCHAR_TABLE (Vglyphless_char_display)) >= 1) {
                  Lisp_Object acronym = g->u.glyphless.for_no_font
                    ? XCHAR_TABLE (Vglyphless_char_display)->extras[0]
                    : CHAR_TABLE_REF (Vglyphless_char_display, g->u.glyphless.ch);
                  if (CONSP (acronym)) acronym = XCAR (acronym);
                  if (STRINGP (acronym))
                    text = make_string (SSDATA (acronym), min (SBYTES (acronym), g->u.glyphless.len));
                }
                payload = list5 (make_fixnum (g->u.glyphless.ch), make_fixnum (g->u.glyphless.method),
                                 make_fixnum (g->u.glyphless.len), g->u.glyphless.for_no_font ? Qt : Qnil, text);
                slice = list4 (make_fixnum (g->slice.glyphless.upper_xoff),
                               make_fixnum (g->slice.glyphless.upper_yoff),
                               make_fixnum (g->slice.glyphless.lower_xoff),
                               make_fixnum (g->slice.glyphless.lower_yoff));
              }
              else if (g->type == STRETCH_GLYPH)
                payload = Fcons (make_fixnum (g->u.stretch.height), make_fixnum (g->u.stretch.ascent));
              Lisp_Object values[] = {make_fixnum (g->type), g->object, make_fixnum (g->charpos),
                make_fixnum (g->pixel_width), make_fixnum (g->face_id), make_fixnum (g->ascent),
                make_fixnum (g->descent), make_fixnum (g->voffset), make_fixnum (g->resolved_level),
                make_fixnum (g->padding_p | g->avoid_cursor_p << 1 | g->left_box_line_p << 2
                             | g->right_box_line_p << 3), payload, slice, make_fixnum (source_length)};
              ASET (glyphs, i, Fvector (ARRAYELTS (values), values));
              if (g->face_id < ASIZE (faces) && NILP (AREF (faces, g->face_id)))
                {
                  struct face *face = FACE_FROM_ID (f, g->face_id);
                  if (face)
                    {
                      Lisp_Object font = Qnil;
                      if (face->font) XSETFONT (font, face->font);
                      ASET (faces, g->face_id,
                            Fcons (Fvector (LFACE_VECTOR_SIZE, face->lface), font));
                    }
                }
            }
          ASET (areas, a, glyphs);
        }
      Lisp_Object values[] = {make_fixnum (row->x), make_fixnum (row->y),
        make_fixnum (row->pixel_width), make_fixnum (row->height), make_fixnum (row->ascent),
        make_fixnum (row->visible_height),
        make_fixnum (row->mode_line_p | row->reversed_p << 1 | row->continued_p << 2
                     | row->truncated_on_left_p << 3 | row->truncated_on_right_p << 4
                     | row->tab_line_p << 5),
        make_fixnum (row->start.pos.charpos), make_fixnum (row->end.pos.charpos), areas,
        list4 (make_fixnum (row->start.pos.charpos), make_fixnum (row->start.overlay_string_index),
               make_fixnum (row->start.string_pos.charpos), make_fixnum (row->start.dpvec_index)),
        list4 (make_fixnum (row->end.pos.charpos), make_fixnum (row->end.overlay_string_index),
               make_fixnum (row->end.string_pos.charpos), make_fixnum (row->end.dpvec_index))};
      ASET (rows, r, Fvector (ARRAYELTS (values), values));
    }
  Lisp_Object cursor_data[] = {make_fixnum (w->phys_cursor.x), make_fixnum (w->phys_cursor.y),
    make_fixnum (w->phys_cursor.hpos), make_fixnum (w->phys_cursor.vpos),
    make_fixnum (w->phys_cursor_type), make_fixnum (w->phys_cursor_width),
    make_fixnum (w->phys_cursor_ascent), make_fixnum (w->phys_cursor_height)};
  Lisp_Object cursor = Fvector (ARRAYELTS (cursor_data), cursor_data);
  Lisp_Object boxes = make_vector (LAST_AREA, Qnil);
  for (int area = 0; area < LAST_AREA; ++area)
    ASET (boxes, area, Fcons (make_fixnum (window_box_left_offset (w, area)),
                             make_fixnum (window_box_width (w, area))));
  Lisp_Object result[] = {window, rows, faces, cursor, boxes};
  return Fvector (ARRAYELTS (result), result);
}

Lisp_Object Femacs_web_native_display (Lisp_Object frame);
DEFUN ("emacs-web-native-display", Femacs_web_native_display,
       Semacs_web_native_display, 1, 1, 0,
       doc: /* Copy FRAME's completed tiled display without running layout.
The symbol `version' returns the integer ABI version (1).
A frame returns a vector of [WINDOW ROWS FACES CURSOR] packets.
Sources and fonts retain Lisp identity; consume synchronously, without yielding.
Signal `emacs-web-display-not-ready' instead of returning stale matrices. */)
  (Lisp_Object frame)
{
  if (EQ (frame, intern ("version"))) return make_fixnum (1);
  CHECK_LIVE_FRAME (frame);
  struct frame *f = XFRAME (frame);
  if (!FRAME_WINDOW_P (f)) error ("Emacs Web requires a graphical frame");
  /* Nonlocal exits restore interrupt_input_blocked in unwind_to_catch. */
  block_input ();
  Lisp_Object windows = Fwindow_list (frame, Qt, Qnil);
  Lisp_Object tail;
  for (tail = windows; CONSP (tail); tail = XCDR (tail))
    {
      struct window *w = XWINDOW (XCAR (tail));
      struct glyph_matrix *m = w->current_matrix;
      struct buffer *b = XBUFFER (w->contents);
      bool echo = MINI_WINDOW_P (w) && (minibuf_level == 0 || !EQ (XCAR (tail), minibuf_window));
      if (FRAME_GARBAGED_P (f) || f->redisplay || !m
          || w->must_be_updated_p || w->redisplay || w->force_start
          || (!echo && (!w->window_end_valid || window_outdated (w)
                       || m->buffer != b || m->begv != BUF_BEGV (b)
                       || m->zv != BUF_ZV (b)
                       || w->last_point != (EQ (XCAR (tail), selected_window)
                                            ? BUF_PT (b) : marker_position (w->pointm)))))
        {
          Lisp_Object state[] = {make_fixnum (FRAME_GARBAGED_P (f)),
            make_fixnum (f->redisplay), make_fixnum (w->must_be_updated_p),
            make_fixnum (w->redisplay), make_fixnum (w->force_start),
            make_fixnum (w->window_end_valid), make_fixnum (window_outdated (w)),
            make_fixnum (echo), make_fixnum (w->last_point),
            make_fixnum (EQ (XCAR (tail), selected_window) ? BUF_PT (b) : marker_position (w->pointm))};
          xsignal2 (intern ("emacs-web-display-not-ready"), XCAR (tail),
                    Fvector (ARRAYELTS (state), state));
        }
    }
  Lisp_Object result = make_vector (list_length (windows), Qnil);
  ptrdiff_t i = 0;
  for (tail = windows; CONSP (tail); tail = XCDR (tail))
    ASET (result, i++, ew_web_window (XCAR (tail)));
  unblock_input ();
  return result;
}
