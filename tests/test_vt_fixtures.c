#include "test_framework.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <terminal_surface.h>
#include <vt_parser.h>

#define FIXTURE_MAX_ROWS 32
#define FIXTURE_MAX_TEXT 128
#define FIXTURE_MAX_INPUT 1024

struct vt_fixture {
    int cols;
    int rows;
    int cursor_col;
    int cursor_row;
    int text_count;
    int fg_count;
    int bg_count;
    int attr_count;
    char input[FIXTURE_MAX_INPUT];
    char *text[FIXTURE_MAX_ROWS];
    char *fg[FIXTURE_MAX_ROWS];
    char *bg[FIXTURE_MAX_ROWS];
    char *attr[FIXTURE_MAX_ROWS];
};

static void fixture_init(struct vt_fixture *fixture);
static void fixture_free(struct vt_fixture *fixture);
static int fixture_load(struct vt_fixture *fixture, const char *path);
static int fixture_run(const char *path);
static int fixture_decode_input(char *dst, size_t dst_size, const char *src);
static int fixture_decode_text(char *dst, size_t dst_size, const char *src);
static char *fixture_dup(const char *src);
static int fixture_parse_hex(char ch);
static void fixture_trim(char *line);

static void fixture_init(struct vt_fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->cursor_col = -1;
    fixture->cursor_row = -1;
}

static void fixture_free(struct vt_fixture *fixture)
{
    int i;

    for (i = 0; i < fixture->text_count; i++)
        free(fixture->text[i]);
    for (i = 0; i < fixture->fg_count; i++)
        free(fixture->fg[i]);
    for (i = 0; i < fixture->bg_count; i++)
        free(fixture->bg[i]);
    for (i = 0; i < fixture->attr_count; i++)
        free(fixture->attr[i]);
}

static char *fixture_dup(const char *src)
{
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);

    if (copy == NULL)
        return NULL;
    memcpy(copy, src, len + 1);
    return copy;
}

static void fixture_trim(char *line)
{
    size_t len = strlen(line);

    while (len > 0 &&
           (line[len - 1] == '\n' || line[len - 1] == '\r' ||
            isspace((unsigned char)line[len - 1]))) {
        line[len - 1] = '\0';
        len--;
    }
}

static int fixture_decode_input(char *dst, size_t dst_size, const char *src)
{
    size_t i = 0;
    size_t out = 0;

    while (src[i] != '\0') {
        char ch = src[i++];

        if (out + 1 >= dst_size)
            return -1;
        if (ch != '\\') {
            dst[out++] = ch;
            continue;
        }

        ch = src[i++];
        if (ch == 'n') {
            dst[out++] = '\n';
        } else if (ch == 'r') {
            dst[out++] = '\r';
        } else if (ch == 't') {
            dst[out++] = '\t';
        } else if (ch == 'b') {
            dst[out++] = '\b';
        } else if (ch == 'e') {
            dst[out++] = 0x1b;
        } else if (ch == 'x') {
            int hi = fixture_parse_hex(src[i++]);
            int lo = fixture_parse_hex(src[i++]);

            if (hi < 0 || lo < 0)
                return -1;
            dst[out++] = (char)((hi << 4) | lo);
        } else if (ch == '\\') {
            dst[out++] = '\\';
        } else {
            dst[out++] = ch;
        }
    }

    dst[out] = '\0';
    return (int)out;
}

static int fixture_decode_text(char *dst, size_t dst_size, const char *src)
{
    size_t i = 0;
    size_t out = 0;

    while (src[i] != '\0') {
        char ch = src[i++];

        if (out + 1 >= dst_size)
            return -1;
        if (ch == '.') {
            dst[out++] = ' ';
            continue;
        }
        if (ch == '\\') {
            ch = src[i++];
            if (ch == '.')
                dst[out++] = '.';
            else if (ch == '\\')
                dst[out++] = '\\';
            else
                dst[out++] = ch;
            continue;
        }
        dst[out++] = ch;
    }

    dst[out] = '\0';
    return (int)out;
}

static int fixture_parse_hex(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static int fixture_load(struct vt_fixture *fixture, const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[512];

    if (fp == NULL) {
        printf("    failed to open fixture: %s\n", path);
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *value;

        fixture_trim(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;

        value = strchr(line, '=');
        if (value == NULL)
            continue;
        *value++ = '\0';

        if (strcmp(line, "cols") == 0) {
            fixture->cols = atoi(value);
        } else if (strcmp(line, "rows") == 0) {
            fixture->rows = atoi(value);
        } else if (strcmp(line, "cursor") == 0) {
            if (sscanf(value, "%d,%d", &fixture->cursor_col, &fixture->cursor_row) != 2) {
                fclose(fp);
                return -1;
            }
        } else if (strcmp(line, "input") == 0) {
            if (fixture_decode_input(fixture->input, sizeof(fixture->input), value) < 0) {
                fclose(fp);
                return -1;
            }
        } else if (strcmp(line, "text") == 0) {
            if (fixture->text_count >= FIXTURE_MAX_ROWS) {
                fclose(fp);
                return -1;
            }
            fixture->text[fixture->text_count++] = fixture_dup(value);
        } else if (strcmp(line, "fg") == 0) {
            if (fixture->fg_count >= FIXTURE_MAX_ROWS) {
                fclose(fp);
                return -1;
            }
            fixture->fg[fixture->fg_count++] = fixture_dup(value);
        } else if (strcmp(line, "bg") == 0) {
            if (fixture->bg_count >= FIXTURE_MAX_ROWS) {
                fclose(fp);
                return -1;
            }
            fixture->bg[fixture->bg_count++] = fixture_dup(value);
        } else if (strcmp(line, "attr") == 0) {
            if (fixture->attr_count >= FIXTURE_MAX_ROWS) {
                fclose(fp);
                return -1;
            }
            fixture->attr[fixture->attr_count++] = fixture_dup(value);
        }
    }

    fclose(fp);
    if (fixture->cols <= 0 || fixture->rows <= 0)
        return -1;
    if (fixture->cursor_col < 0 || fixture->cursor_row < 0)
        return -1;
    if (fixture->text_count != fixture->rows ||
        fixture->fg_count != fixture->rows ||
        fixture->bg_count != fixture->rows ||
        fixture->attr_count != fixture->rows)
        return -1;
    return 0;
}

static int fixture_run(const char *path)
{
    struct vt_fixture fixture;
    struct terminal_surface surface;
    struct vt_parser parser;
    int status = -1;
    int row;

    fixture_init(&fixture);
    if (fixture_load(&fixture, path) < 0)
        goto out;
    if (terminal_surface_init(&surface, fixture.cols, fixture.rows) < 0)
        goto out;

    vt_parser_init(&parser, &surface);
    vt_parser_feed(&parser, fixture.input, strlen(fixture.input));

    if (surface.cursor_col != fixture.cursor_col ||
        surface.cursor_row != fixture.cursor_row) {
        printf("    cursor mismatch in %s: got (%d,%d), expected (%d,%d)\n",
               path,
               surface.cursor_col, surface.cursor_row,
               fixture.cursor_col, fixture.cursor_row);
        terminal_surface_free(&surface);
        goto out;
    }

    for (row = 0; row < fixture.rows; row++) {
        char expected_text[FIXTURE_MAX_TEXT];
        int col;

        if (fixture_decode_text(expected_text, sizeof(expected_text), fixture.text[row]) != fixture.cols) {
            printf("    text width mismatch in %s row %d\n", path, row);
            terminal_surface_free(&surface);
            goto out;
        }
        if ((int)strlen(fixture.fg[row]) != fixture.cols ||
            (int)strlen(fixture.bg[row]) != fixture.cols ||
            (int)strlen(fixture.attr[row]) != fixture.cols) {
            printf("    color width mismatch in %s row %d\n", path, row);
            terminal_surface_free(&surface);
            goto out;
        }

        for (col = 0; col < fixture.cols; col++) {
            const struct term_cell *cell = terminal_surface_cell(&surface, col, row);
            int fg = fixture_parse_hex(fixture.fg[row][col]);
            int bg = fixture_parse_hex(fixture.bg[row][col]);
            int attr = fixture_parse_hex(fixture.attr[row][col]);

            if (cell == NULL || fg < 0 || bg < 0 || attr < 0) {
                printf("    invalid row data in %s row %d col %d\n", path, row, col);
                terminal_surface_free(&surface);
                goto out;
            }
            if (cell->ch != expected_text[col] ||
                cell->fg != fg ||
                cell->bg != bg ||
                cell->attr != attr) {
                printf("    cell mismatch in %s row %d col %d: got ch=%d fg=%u bg=%u attr=%u\n",
                       path, row, col,
                       (int)(unsigned char)cell->ch,
                       cell->fg, cell->bg, cell->attr);
                terminal_surface_free(&surface);
                goto out;
            }
        }
    }

    terminal_surface_free(&surface);
    status = 0;

out:
    fixture_free(&fixture);
    return status;
}

TEST(fixture_basic_text) {
    ASSERT_EQ(fixture_run("fixtures/vt/basic_text.fixture"), 0);
}

TEST(fixture_color_attrs) {
    ASSERT_EQ(fixture_run("fixtures/vt/color_attrs.fixture"), 0);
}

TEST(fixture_cursor_erase) {
    ASSERT_EQ(fixture_run("fixtures/vt/cursor_erase.fixture"), 0);
}

TEST(fixture_clear_home) {
    ASSERT_EQ(fixture_run("fixtures/vt/clear_home.fixture"), 0);
}

int main(void)
{
    printf("=== vt fixture tests ===\n");

    RUN_TEST(fixture_basic_text);
    RUN_TEST(fixture_color_attrs);
    RUN_TEST(fixture_cursor_erase);
    RUN_TEST(fixture_clear_home);

    TEST_REPORT();
}
