#include <malloc.h>
#include <string.h>
#include <pager.h>
#include <utf8.h>
#include <wcwidth.h>

static int pager_effective_cols(int cols)
{
  if (cols > 0)
    return cols;
  return 80;
}

static void pager_copy_text(char *dst, int cap, const char *src)
{
  int i;

  if (dst == 0 || cap <= 0)
    return;
  if (src == 0)
    src = "";
  for (i = 0; i < cap - 1 && src[i] != '\0'; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

static int pager_reserve_rows(struct pager_document *document, int needed)
{
  struct pager_row *rows;
  int next_cap;

  if (document == 0)
    return -1;
  if (needed <= document->row_cap)
    return 0;

  next_cap = document->row_cap > 0 ? document->row_cap : 16;
  while (next_cap < needed)
    next_cap *= 2;

  rows = (struct pager_row *)malloc((size_t)next_cap * sizeof(*rows));
  if (rows == 0)
    return -1;
  memset(rows, 0, (size_t)next_cap * sizeof(*rows));
  if (document->rows != 0) {
    memcpy(rows, document->rows,
           (size_t)document->row_count * sizeof(*rows));
    free(document->rows);
  }
  document->rows = rows;
  document->row_cap = next_cap;
  return 0;
}

static int pager_push_row(struct pager_document *document,
                          int line_index, int start_col, int end_col)
{
  struct pager_row *row;

  if (document == 0)
    return -1;
  if (pager_reserve_rows(document, document->row_count + 1) < 0)
    return -1;

  row = &document->rows[document->row_count++];
  row->line_index = line_index;
  row->start_col = start_col;
  row->end_col = end_col;
  return 0;
}

static int pager_line_wrap_end(const char *data, int len, int start, int cols)
{
  int index = start;
  int used = 0;

  if (data == 0 || len <= 0 || start >= len)
    return len;

  while (index < len) {
    u_int32_t codepoint;
    int consumed;
    int width;

    utf8_decode_one(data + index, len - index, &codepoint, &consumed);
    if (consumed <= 0)
      consumed = 1;
    width = unicode_wcwidth(codepoint);
    if (width <= 0)
      width = 1;
    if (used > 0 && used + width > cols)
      break;
    used += width;
    index += consumed;
  }

  if (index <= start)
    return utf8_next_char_end(data, len, start);
  return index;
}

static int pager_clamp_line_index(const struct pager_document *document, int line_index)
{
  if (document == 0 || document->buffer.line_count <= 0)
    return 0;
  if (line_index < 0)
    return 0;
  if (line_index >= document->buffer.line_count)
    return document->buffer.line_count - 1;
  return line_index;
}

static void pager_clamp_top(struct pager_document *document, int page_rows)
{
  int max_top;

  if (document == 0)
    return;
  if (page_rows <= 0)
    page_rows = 1;
  if (document->row_count <= page_rows)
    max_top = 0;
  else
    max_top = document->row_count - page_rows;
  if (document->top_row < 0)
    document->top_row = 0;
  if (document->top_row > max_top)
    document->top_row = max_top;
}

static int pager_search_start_forward(const struct pager_document *document,
                                      int *line_index, int *col)
{
  const struct pager_row *row;
  const char *data;
  int len;

  if (document == 0 || line_index == 0 || col == 0)
    return -1;

  if (document->match_line >= 0) {
    *line_index = pager_clamp_line_index(document, document->match_line);
    data = vi_buffer_line_data(&document->buffer, *line_index);
    len = vi_buffer_line_length(&document->buffer, *line_index);
    *col = utf8_next_char_end(data, len, document->match_col);
    if (*col >= len) {
      *line_index += 1;
      *col = 0;
    }
    return 0;
  }

  row = pager_document_row(document, document->top_row);
  if (row == 0)
    return -1;
  *line_index = row->line_index;
  *col = row->start_col;
  return 0;
}

static int pager_search_start_backward(const struct pager_document *document,
                                       int *line_index, int *col)
{
  const struct pager_row *row;
  const char *data;
  int len;

  if (document == 0 || line_index == 0 || col == 0)
    return -1;

  if (document->match_line >= 0) {
    *line_index = pager_clamp_line_index(document, document->match_line);
    data = vi_buffer_line_data(&document->buffer, *line_index);
    len = vi_buffer_line_length(&document->buffer, *line_index);
    if (document->match_col > 0) {
      *col = utf8_prev_char_start(data, len, document->match_col);
      return 0;
    }
    *line_index -= 1;
  } else {
    row = pager_document_row(document, document->top_row);
    if (row == 0)
      return -1;
    *line_index = row->line_index;
  }

  if (*line_index < 0)
    return -1;
  *line_index = pager_clamp_line_index(document, *line_index);
  *col = vi_buffer_line_length(&document->buffer, *line_index);
  return 0;
}

int pager_document_init(struct pager_document *document)
{
  if (document == 0)
    return -1;
  memset(document, 0, sizeof(*document));
  document->cols = 80;
  document->match_line = -1;
  document->match_col = -1;
  document->last_search_direction = 1;
  return vi_buffer_init(&document->buffer);
}

void pager_document_free(struct pager_document *document)
{
  if (document == 0)
    return;
  if (document->rows != 0)
    free(document->rows);
  document->rows = 0;
  document->row_count = 0;
  document->row_cap = 0;
  vi_buffer_free(&document->buffer);
}

int pager_document_load(struct pager_document *document,
                        const char *data, int len, int cols)
{
  if (document == 0)
    return -1;

  vi_buffer_free(&document->buffer);
  if (vi_buffer_init(&document->buffer) < 0)
    return -1;
  if (vi_buffer_load(&document->buffer, data != 0 ? data : "", len) < 0)
    return -1;

  document->top_row = 0;
  document->match_line = -1;
  document->match_col = -1;
  document->last_search[0] = '\0';
  document->last_search_direction = 1;
  return pager_document_relayout(document, cols);
}

int pager_document_relayout(struct pager_document *document, int cols)
{
  int line_index;

  if (document == 0)
    return -1;

  document->cols = pager_effective_cols(cols);
  document->row_count = 0;
  for (line_index = 0; line_index < document->buffer.line_count; line_index++) {
    const char *data = vi_buffer_line_data(&document->buffer, line_index);
    int len = vi_buffer_line_length(&document->buffer, line_index);
    int start = 0;

    if (len <= 0) {
      if (pager_push_row(document, line_index, 0, 0) < 0)
        return -1;
      continue;
    }

    while (start < len) {
      int end = pager_line_wrap_end(data, len, start, document->cols);

      if (pager_push_row(document, line_index, start, end) < 0)
        return -1;
      start = end;
    }
  }

  if (document->row_count == 0) {
    if (pager_push_row(document, 0, 0, 0) < 0)
      return -1;
  }
  pager_clamp_top(document, 1);
  return 0;
}

const struct pager_row *pager_document_row(const struct pager_document *document,
                                           int row_index)
{
  if (document == 0 || row_index < 0 || row_index >= document->row_count)
    return 0;
  return &document->rows[row_index];
}

int pager_document_row_for_position(const struct pager_document *document,
                                    int line_index, int col)
{
  int i;
  int last_row = -1;

  if (document == 0 || document->row_count <= 0)
    return 0;

  for (i = 0; i < document->row_count; i++) {
    const struct pager_row *row = &document->rows[i];

    if (row->line_index != line_index)
      continue;
    last_row = i;
    if (row->start_col == row->end_col)
      return i;
    if (col < row->end_col)
      return i;
  }

  if (last_row >= 0)
    return last_row;
  if (line_index <= 0)
    return 0;
  return document->row_count - 1;
}

void pager_document_focus_row(struct pager_document *document,
                              int row_index, int page_rows)
{
  if (document == 0)
    return;
  if (page_rows <= 0)
    page_rows = 1;
  if (row_index < document->top_row)
    document->top_row = row_index;
  else if (row_index >= document->top_row + page_rows)
    document->top_row = row_index - page_rows + 1;
  pager_clamp_top(document, page_rows);
}

void pager_document_page_down(struct pager_document *document, int page_rows)
{
  if (document == 0)
    return;
  if (page_rows <= 0)
    page_rows = 1;
  document->top_row += page_rows;
  pager_clamp_top(document, page_rows);
}

void pager_document_page_up(struct pager_document *document, int page_rows)
{
  if (document == 0)
    return;
  if (page_rows <= 0)
    page_rows = 1;
  document->top_row -= page_rows;
  pager_clamp_top(document, page_rows);
}

void pager_document_line_down(struct pager_document *document, int rows)
{
  if (document == 0)
    return;
  if (rows <= 0)
    rows = 1;
  document->top_row += rows;
  pager_clamp_top(document, 1);
}

void pager_document_line_up(struct pager_document *document, int rows)
{
  if (document == 0)
    return;
  if (rows <= 0)
    rows = 1;
  document->top_row -= rows;
  pager_clamp_top(document, 1);
}

void pager_document_go_top(struct pager_document *document)
{
  if (document == 0)
    return;
  document->top_row = 0;
}

void pager_document_go_bottom(struct pager_document *document, int page_rows)
{
  if (document == 0)
    return;
  document->top_row = document->row_count - 1;
  pager_clamp_top(document, page_rows);
}

int pager_document_search(struct pager_document *document,
                          const char *needle, int direction, int page_rows)
{
  int line_index;
  int col;
  int found_line;
  int found_col;
  int found_row;
  int status;

  if (document == 0 || needle == 0 || needle[0] == '\0')
    return -1;

  if (direction >= 0) {
    if (pager_search_start_forward(document, &line_index, &col) < 0)
      return -1;
    if (line_index >= document->buffer.line_count)
      return -1;
    status = vi_buffer_find_forward(&document->buffer, needle,
                                    line_index, col,
                                    &found_line, &found_col);
  } else {
    if (pager_search_start_backward(document, &line_index, &col) < 0)
      return -1;
    status = vi_buffer_find_backward(&document->buffer, needle,
                                     line_index, col,
                                     &found_line, &found_col);
  }
  if (status < 0)
    return -1;

  document->match_line = found_line;
  document->match_col = found_col;
  pager_copy_text(document->last_search, sizeof(document->last_search), needle);
  document->last_search_direction = direction >= 0 ? 1 : -1;
  found_row = pager_document_row_for_position(document, found_line, found_col);
  pager_document_focus_row(document, found_row, page_rows);
  return 0;
}

int pager_document_repeat_search(struct pager_document *document,
                                 int reverse, int page_rows)
{
  int direction;

  if (document == 0 || document->last_search[0] == '\0')
    return -1;
  direction = document->last_search_direction;
  if (reverse != 0)
    direction = -direction;
  return pager_document_search(document, document->last_search,
                               direction, page_rows);
}

int pager_document_percent(const struct pager_document *document, int page_rows)
{
  int bottom;
  int percent;

  if (document == 0 || document->row_count <= 0)
    return 100;
  if (page_rows <= 0)
    page_rows = 1;
  bottom = document->top_row + page_rows;
  if (bottom > document->row_count)
    bottom = document->row_count;
  percent = (bottom * 100) / document->row_count;
  if (percent > 100)
    percent = 100;
  return percent;
}
