#include <sodex/const.h>
#include <vt_parser.h>
#include <wcwidth.h>

static void vt_parser_reset_params(struct vt_parser *parser);
static int vt_parser_param(const struct vt_parser *parser, int index, int fallback);
static struct term_cell vt_parser_blank(const struct vt_parser *parser);
static unsigned char vt_parser_ansi_to_vga(int color);
static void vt_parser_apply_sgr(struct vt_parser *parser);
static void vt_parser_erase_display(struct vt_parser *parser, int mode);
static void vt_parser_erase_line(struct vt_parser *parser, int mode);
static void vt_parser_dispatch_csi(struct vt_parser *parser, char final);
static void vt_parser_emit_codepoint(struct vt_parser *parser, u_int32_t codepoint);
static void vt_parser_flush_partial_utf8(struct vt_parser *parser);
static void vt_parser_feed_ground(struct vt_parser *parser, unsigned char ch);

enum {
  VT_STATE_GROUND = 0,
  VT_STATE_ESCAPE = 1,
  VT_STATE_CSI = 2
};

static void vt_parser_reset_params(struct vt_parser *parser)
{
  int i;

  parser->param_count = 0;
  parser->param_active = 0;
  for (i = 0; i < VT_PARSER_MAX_PARAMS; i++) {
    parser->params[i] = -1;
  }
}

static int vt_parser_param(const struct vt_parser *parser, int index, int fallback)
{
  if (index < 0 || index >= parser->param_count || parser->params[index] < 0)
    return fallback;
  return parser->params[index];
}

static struct term_cell vt_parser_blank(const struct vt_parser *parser)
{
  struct term_cell blank = parser->pen;
  blank.ch = ' ';
  return blank;
}

static unsigned char vt_parser_ansi_to_vga(int color)
{
  static const unsigned char table[8] = {
    TERM_COLOR_BLACK,
    TERM_COLOR_RED,
    TERM_COLOR_GREEN,
    TERM_COLOR_BROWN,
    TERM_COLOR_BLUE,
    TERM_COLOR_MAGENTA,
    TERM_COLOR_CYAN,
    TERM_COLOR_LIGHT_GRAY
  };

  if (color < 0 || color > 7)
    return TERM_COLOR_LIGHT_GRAY;
  return table[color];
}

static void vt_parser_apply_sgr(struct vt_parser *parser)
{
  int i;
  int count = parser->param_count;

  if (count == 0) {
    parser->pen = parser->default_pen;
    return;
  }

  for (i = 0; i < count; i++) {
    int code = vt_parser_param(parser, i, 0);

    if (code == 0) {
      parser->pen = parser->default_pen;
    } else if (code == 1) {
      parser->pen.attr |= TERM_ATTR_BOLD;
    } else if (code == 22) {
      parser->pen.attr &= (unsigned char)(~TERM_ATTR_BOLD);
    } else if (code == 7) {
      parser->pen.attr |= TERM_ATTR_REVERSE;
    } else if (code == 27) {
      parser->pen.attr &= (unsigned char)(~TERM_ATTR_REVERSE);
    } else if (code >= 30 && code <= 37) {
      parser->pen.fg = vt_parser_ansi_to_vga(code - 30);
    } else if (code == 39) {
      parser->pen.fg = parser->default_pen.fg;
    } else if (code >= 40 && code <= 47) {
      parser->pen.bg = vt_parser_ansi_to_vga(code - 40);
    } else if (code == 49) {
      parser->pen.bg = parser->default_pen.bg;
    } else if (code >= 90 && code <= 97) {
      parser->pen.fg = (unsigned char)(vt_parser_ansi_to_vga(code - 90) + 8);
    } else if (code >= 100 && code <= 107) {
      parser->pen.bg = (unsigned char)(vt_parser_ansi_to_vga(code - 100) + 8);
    }
  }
}

static void vt_parser_erase_display(struct vt_parser *parser, int mode)
{
  struct terminal_surface *surface = parser->surface;
  struct term_cell blank = vt_parser_blank(parser);

  if (surface == NULL)
    return;

  if (mode == 0) {
    terminal_surface_clear_region(surface,
                                  surface->cursor_col,
                                  surface->cursor_row,
                                  surface->cols - 1,
                                  surface->cursor_row,
                                  &blank);
    if (surface->cursor_row + 1 < surface->rows) {
      terminal_surface_clear_region(surface,
                                    0,
                                    surface->cursor_row + 1,
                                    surface->cols - 1,
                                    surface->rows - 1,
                                    &blank);
    }
  } else if (mode == 1) {
    if (surface->cursor_row > 0) {
      terminal_surface_clear_region(surface,
                                    0,
                                    0,
                                    surface->cols - 1,
                                    surface->cursor_row - 1,
                                    &blank);
    }
    terminal_surface_clear_region(surface,
                                  0,
                                  surface->cursor_row,
                                  surface->cursor_col,
                                  surface->cursor_row,
                                  &blank);
  } else if (mode == 2) {
    terminal_surface_clear(surface, &blank);
  }
}

static void vt_parser_erase_line(struct vt_parser *parser, int mode)
{
  struct terminal_surface *surface = parser->surface;
  struct term_cell blank = vt_parser_blank(parser);

  if (surface == NULL)
    return;

  if (mode == 0) {
    terminal_surface_clear_region(surface,
                                  surface->cursor_col,
                                  surface->cursor_row,
                                  surface->cols - 1,
                                  surface->cursor_row,
                                  &blank);
  } else if (mode == 1) {
    terminal_surface_clear_region(surface,
                                  0,
                                  surface->cursor_row,
                                  surface->cursor_col,
                                  surface->cursor_row,
                                  &blank);
  } else if (mode == 2) {
    terminal_surface_clear_region(surface,
                                  0,
                                  surface->cursor_row,
                                  surface->cols - 1,
                                  surface->cursor_row,
                                  &blank);
  }
}

static void vt_parser_dispatch_csi(struct vt_parser *parser, char final)
{
  struct terminal_surface *surface = parser->surface;
  int row;
  int col;

  if (parser->param_active != 0 || parser->param_count > 0) {
    if (parser->param_count < VT_PARSER_MAX_PARAMS && parser->param_active == 0) {
      parser->params[parser->param_count++] = -1;
    } else if (parser->param_active != 0 && parser->param_count < VT_PARSER_MAX_PARAMS) {
      parser->param_count++;
    }
  }

  switch (final) {
  case 'A':
    terminal_surface_move_cursor(surface, 0, -vt_parser_param(parser, 0, 1));
    break;
  case 'B':
    terminal_surface_move_cursor(surface, 0, vt_parser_param(parser, 0, 1));
    break;
  case 'C':
    terminal_surface_move_cursor(surface, vt_parser_param(parser, 0, 1), 0);
    break;
  case 'D':
    terminal_surface_move_cursor(surface, -vt_parser_param(parser, 0, 1), 0);
    break;
  case 'H':
  case 'f':
    row = vt_parser_param(parser, 0, 1) - 1;
    col = vt_parser_param(parser, 1, 1) - 1;
    terminal_surface_set_cursor(surface, col, row);
    break;
  case 'J':
    vt_parser_erase_display(parser, vt_parser_param(parser, 0, 0));
    break;
  case 'K':
    vt_parser_erase_line(parser, vt_parser_param(parser, 0, 0));
    break;
  case 'm':
    vt_parser_apply_sgr(parser);
    break;
  case 's':
    terminal_surface_save_cursor(surface);
    break;
  case 'u':
    terminal_surface_restore_cursor(surface);
    break;
  default:
    break;
  }
}

static void vt_parser_emit_codepoint(struct vt_parser *parser, u_int32_t codepoint)
{
  int width;

  if (codepoint == 0)
    return;

  width = unicode_wcwidth(codepoint);
  if (width < 0)
    return;
  if (width == 0)
    return;

  terminal_surface_write_codepoint(parser->surface, codepoint, width, &parser->pen);
}

static void vt_parser_flush_partial_utf8(struct vt_parser *parser)
{
  if (parser == 0)
    return;
  if (parser->decoder.expected == 0)
    return;

  utf8_decoder_reset(&parser->decoder);
  vt_parser_emit_codepoint(parser, UTF8_REPLACEMENT_CHAR);
}

static void vt_parser_feed_ground(struct vt_parser *parser, unsigned char ch)
{
  u_int32_t codepoint;
  int decoded;

  if (ch == '\r') {
    vt_parser_flush_partial_utf8(parser);
    terminal_surface_carriage_return(parser->surface);
    return;
  }
  if (ch == '\n') {
    vt_parser_flush_partial_utf8(parser);
    terminal_surface_newline(parser->surface, &parser->pen);
    return;
  }
  if (ch == '\b') {
    vt_parser_flush_partial_utf8(parser);
    terminal_surface_backspace(parser->surface);
    return;
  }
  if (ch == '\t') {
    vt_parser_flush_partial_utf8(parser);
    terminal_surface_tab(parser->surface, &parser->pen);
    return;
  }
  if (ch < 0x20 || ch == 0x7f)
    return;

  decoded = utf8_decode_byte(&parser->decoder, ch, &codepoint);
  if (decoded > 0) {
    vt_parser_emit_codepoint(parser, codepoint);
    return;
  }
  if (decoded < 0) {
    vt_parser_emit_codepoint(parser, UTF8_REPLACEMENT_CHAR);
    if (ch >= 0x20 && ch < 0x80)
      vt_parser_emit_codepoint(parser, ch);
  }
}

void vt_parser_init(struct vt_parser *parser, struct terminal_surface *surface)
{
  if (parser == NULL)
    return;

  parser->surface = surface;
  parser->default_pen.ch = ' ';
  parser->default_pen.fg = TERM_COLOR_LIGHT_GRAY;
  parser->default_pen.bg = TERM_COLOR_BLACK;
  parser->default_pen.attr = 0;
  parser->default_pen.width = 1;
  vt_parser_reset(parser);
}

void vt_parser_reset(struct vt_parser *parser)
{
  if (parser == NULL)
    return;

  parser->state = VT_STATE_GROUND;
  parser->pen = parser->default_pen;
  utf8_decoder_reset(&parser->decoder);
  vt_parser_reset_params(parser);
}

void vt_parser_feed(struct vt_parser *parser, const char *data, size_t len)
{
  size_t i;

  if (parser == NULL || data == NULL)
    return;

  for (i = 0; i < len; i++) {
    unsigned char ch = (unsigned char)data[i];

    if (parser->state == VT_STATE_GROUND) {
      if (ch == 0x1b) {
        vt_parser_flush_partial_utf8(parser);
        parser->state = VT_STATE_ESCAPE;
      } else {
        vt_parser_feed_ground(parser, ch);
      }
      continue;
    }

    if (parser->state == VT_STATE_ESCAPE) {
      if (ch == '[') {
        parser->state = VT_STATE_CSI;
        vt_parser_reset_params(parser);
      } else if (ch == '7') {
        terminal_surface_save_cursor(parser->surface);
        parser->state = VT_STATE_GROUND;
      } else if (ch == '8') {
        terminal_surface_restore_cursor(parser->surface);
        parser->state = VT_STATE_GROUND;
      } else {
        parser->state = VT_STATE_GROUND;
      }
      continue;
    }

    if (parser->state == VT_STATE_CSI) {
      if (ch >= '0' && ch <= '9') {
        if (parser->param_active == 0) {
          if (parser->param_count >= VT_PARSER_MAX_PARAMS) {
            parser->state = VT_STATE_GROUND;
            continue;
          }
          parser->params[parser->param_count] = 0;
          parser->param_active = 1;
        }
        parser->params[parser->param_count] =
          parser->params[parser->param_count] * 10 + (ch - '0');
      } else if (ch == ';') {
        if (parser->param_count < VT_PARSER_MAX_PARAMS) {
          if (parser->param_active == 0) {
            parser->params[parser->param_count] = -1;
          }
          parser->param_count++;
          parser->param_active = 0;
        } else {
          parser->state = VT_STATE_GROUND;
        }
      } else if (ch >= 0x40 && ch <= 0x7e) {
        vt_parser_dispatch_csi(parser, ch);
        parser->state = VT_STATE_GROUND;
        vt_parser_reset_params(parser);
      } else {
        parser->state = VT_STATE_GROUND;
      }
    }
  }
}
