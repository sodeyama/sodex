#ifndef _USR_PAGER_H
#define _USR_PAGER_H

#include <vi.h>

#define PAGER_MAX_SEARCH 64

struct pager_row {
  int line_index;
  int start_col;
  int end_col;
};

struct pager_document {
  struct vi_buffer buffer;
  struct pager_row *rows;
  int row_count;
  int row_cap;
  int cols;
  int top_row;
  int match_line;
  int match_col;
  char last_search[PAGER_MAX_SEARCH];
  int last_search_direction;
};

int pager_document_init(struct pager_document *document);
void pager_document_free(struct pager_document *document);
int pager_document_load(struct pager_document *document,
                        const char *data, int len, int cols);
int pager_document_relayout(struct pager_document *document, int cols);
const struct pager_row *pager_document_row(const struct pager_document *document,
                                           int row_index);
int pager_document_row_for_position(const struct pager_document *document,
                                    int line_index, int col);
void pager_document_focus_row(struct pager_document *document,
                              int row_index, int page_rows);
void pager_document_page_down(struct pager_document *document, int page_rows);
void pager_document_page_up(struct pager_document *document, int page_rows);
void pager_document_line_down(struct pager_document *document, int rows);
void pager_document_line_up(struct pager_document *document, int rows);
void pager_document_go_top(struct pager_document *document);
void pager_document_go_bottom(struct pager_document *document, int page_rows);
int pager_document_search(struct pager_document *document,
                          const char *needle, int direction, int page_rows);
int pager_document_repeat_search(struct pager_document *document,
                                 int reverse, int page_rows);
int pager_document_percent(const struct pager_document *document, int page_rows);

int pager_command_main(const char *program_name,
                       int argc, char **argv, int less_mode);

#endif /* _USR_PAGER_H */
