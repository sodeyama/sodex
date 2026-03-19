#include "test_framework.h"
#include <pager.h>
#include <stdio.h>
#include <string.h>

TEST(layout_wraps_ascii_lines) {
    struct pager_document document;
    const struct pager_row *row;

    ASSERT_EQ(pager_document_init(&document), 0);
    ASSERT_EQ(pager_document_load(&document, "alpha\nabcdef", 12, 3), 0);
    ASSERT_EQ(document.row_count, 4);

    row = pager_document_row(&document, 0);
    ASSERT_EQ(row->line_index, 0);
    ASSERT_EQ(row->start_col, 0);
    ASSERT_EQ(row->end_col, 3);

    row = pager_document_row(&document, 1);
    ASSERT_EQ(row->line_index, 0);
    ASSERT_EQ(row->start_col, 3);
    ASSERT_EQ(row->end_col, 5);

    row = pager_document_row(&document, 2);
    ASSERT_EQ(row->line_index, 1);
    ASSERT_EQ(row->start_col, 0);
    ASSERT_EQ(row->end_col, 3);

    row = pager_document_row(&document, 3);
    ASSERT_EQ(row->line_index, 1);
    ASSERT_EQ(row->start_col, 3);
    ASSERT_EQ(row->end_col, 6);

    pager_document_free(&document);
}

TEST(layout_wraps_wide_utf8_lines) {
    struct pager_document document;
    const struct pager_row *row;
    const char *text = "あい";

    ASSERT_EQ(pager_document_init(&document), 0);
    ASSERT_EQ(pager_document_load(&document, text, (int)strlen(text), 3), 0);
    ASSERT_EQ(document.row_count, 2);

    row = pager_document_row(&document, 0);
    ASSERT_EQ(row->start_col, 0);
    ASSERT_EQ(row->end_col, 3);

    row = pager_document_row(&document, 1);
    ASSERT_EQ(row->start_col, 3);
    ASSERT_EQ(row->end_col, 6);

    pager_document_free(&document);
}

TEST(search_moves_view_to_match) {
    struct pager_document document;

    ASSERT_EQ(pager_document_init(&document), 0);
    ASSERT_EQ(pager_document_load(&document, "l1\nl2\ntarget\nl4", 15, 80), 0);
    ASSERT_EQ(pager_document_search(&document, "target", 1, 2), 0);
    ASSERT_EQ(document.match_line, 2);
    ASSERT_EQ(document.match_col, 0);
    ASSERT_EQ(document.top_row, 1);

    pager_document_free(&document);
}

TEST(repeat_search_tracks_direction) {
    struct pager_document document;

    ASSERT_EQ(pager_document_init(&document), 0);
    ASSERT_EQ(pager_document_load(&document, "alpha\nbeta\nalpha", 16, 80), 0);
    ASSERT_EQ(pager_document_search(&document, "alpha", 1, 2), 0);
    ASSERT_EQ(document.match_line, 0);
    ASSERT_EQ(pager_document_repeat_search(&document, 0, 2), 0);
    ASSERT_EQ(document.match_line, 2);
    ASSERT_EQ(document.top_row, 1);
    ASSERT_EQ(pager_document_repeat_search(&document, 1, 2), 0);
    ASSERT_EQ(document.match_line, 0);
    ASSERT_EQ(document.top_row, 0);

    pager_document_free(&document);
}

int main(void)
{
    printf("=== pager tests ===\n");

    RUN_TEST(layout_wraps_ascii_lines);
    RUN_TEST(layout_wraps_wide_utf8_lines);
    RUN_TEST(search_moves_view_to_match);
    RUN_TEST(repeat_search_tracks_direction);

    TEST_REPORT();
}
