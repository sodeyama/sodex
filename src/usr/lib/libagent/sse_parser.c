/*
 * sse_parser.c - Server-Sent Events parser
 *
 * Implements W3C SSE protocol with TCP fragmentation handling.
 * Line buffer accumulates data across recv() boundaries.
 * Supports event:, data:, comment (:), and blank line dispatch.
 */

#include <sse_parser.h>
#include <string.h>

#ifndef TEST_BUILD
#include <debug.h>
#else
static void debug_printf(const char *fmt, ...) { (void)fmt; }
#endif

/* ---- Helpers ---- */

static int min_int(int a, int b) { return a < b ? a : b; }

static void reset_pending(struct sse_parser *p)
{
    p->pending.event_name[0] = '\0';
    p->pending.data[0] = '\0';
    p->pending.data_len = 0;
    p->has_event_name = 0;
    p->has_data = 0;
}

/* Process one complete line (without line terminator).
 * Returns 1 if event dispatched, 0 otherwise. */
static int process_line(struct sse_parser *p, const char *line, int len,
                        struct sse_event *out)
{
    /* Empty line -> dispatch event */
    if (len == 0) {
        if (p->has_data) {
            /* Copy pending event to output */
            memcpy(out->event_name, p->pending.event_name,
                   sizeof(out->event_name));
            /* Remove trailing \n from data if present */
            if (p->pending.data_len > 0 &&
                p->pending.data[p->pending.data_len - 1] == '\n') {
                p->pending.data_len--;
            }
            memcpy(out->data, p->pending.data, p->pending.data_len);
            out->data[p->pending.data_len] = '\0';
            out->data_len = p->pending.data_len;
            /* If no event name was set, default to "message" */
            if (!p->has_event_name) {
                strncpy(out->event_name, "message",
                        sizeof(out->event_name) - 1);
                out->event_name[sizeof(out->event_name) - 1] = '\0';
            }
            reset_pending(p);
            return 1;  /* Event dispatched */
        }
        reset_pending(p);
        return 0;
    }

    /* Comment line (starts with ':') -> ignore */
    if (line[0] == ':')
        return 0;

    /* Find ':' separator */
    {
        const char *colon = (const char *)0;
        const char *value;
        int name_len, value_len;
        int i;

        for (i = 0; i < len; i++) {
            if (line[i] == ':') {
                colon = line + i;
                break;
            }
        }

        if (colon) {
            name_len = colon - line;
            value = colon + 1;
            /* Skip one leading space after colon */
            if (value < line + len && *value == ' ')
                value++;
            value_len = (line + len) - value;
        } else {
            /* No colon: entire line is field name, value is empty */
            name_len = len;
            value = "";
            value_len = 0;
        }

        /* Process known fields (case-sensitive per spec) */
        if (name_len == 5 && memcmp(line, "event", 5) == 0) {
            int copy = min_int(value_len, SSE_MAX_EVENT_NAME - 1);
            memcpy(p->pending.event_name, value, copy);
            p->pending.event_name[copy] = '\0';
            p->has_event_name = 1;
        } else if (name_len == 4 && memcmp(line, "data", 4) == 0) {
            /* Append value + \n to data buffer */
            if (p->pending.data_len + value_len + 1 < SSE_MAX_DATA_LEN) {
                memcpy(p->pending.data + p->pending.data_len, value, value_len);
                p->pending.data_len += value_len;
                p->pending.data[p->pending.data_len++] = '\n';
                p->pending.data[p->pending.data_len] = '\0';
                p->has_data = 1;
            } else {
                /* Data overflow */
                debug_printf("[SSE] data overflow: %d + %d >= %d\n",
                            p->pending.data_len, value_len, SSE_MAX_DATA_LEN);
            }
        }
        /* "id" and "retry" fields: not needed for Claude API, ignore */
    }

    return 0;
}

/* ---- Public API ---- */

void sse_parser_init(struct sse_parser *p)
{
    memset(p, 0, sizeof(*p));
    reset_pending(p);
}

int sse_feed(struct sse_parser *p,
             const char *chunk, int chunk_len,
             struct sse_event *out)
{
    int i;

    if (!p || !out)
        return SSE_EVENT_ERROR;

    if (!chunk || chunk_len <= 0) {
        p->consumed = 0;
        return SSE_EVENT_NEED_MORE;
    }

    /* Scan chunk for line terminators (\n, \r\n, \r) */
    for (i = p->consumed; i < chunk_len; i++) {
        char c = chunk[i];

        if (c == '\n' || c == '\r') {
            /* Handle \r\n as single terminator */
            int next = i + 1;
            if (c == '\r' && next < chunk_len && chunk[next] == '\n')
                next++;

            /* We have a complete line in line_buf */
            if (process_line(p, p->line_buf, p->line_len, out)) {
                /* Event dispatched - advance consumed past this line */
                p->consumed = next;
                p->line_len = 0;
                return SSE_EVENT_DATA;
            }

            p->line_len = 0;
            p->consumed = next;
            i = next - 1;  /* loop will i++ */
        } else {
            /* Accumulate into line buffer */
            if (p->line_len < SSE_MAX_DATA_LEN - 1) {
                p->line_buf[p->line_len++] = c;
            } else {
                /* Line too long */
                debug_printf("[SSE] line buffer overflow\n");
                p->consumed = chunk_len;
                return SSE_EVENT_ERROR;
            }
        }
    }

    /* All input consumed, no complete event */
    p->consumed = chunk_len;
    return SSE_EVENT_NEED_MORE;
}

int sse_consumed(const struct sse_parser *p)
{
    return p ? p->consumed : 0;
}

void sse_parser_reset(struct sse_parser *p)
{
    sse_parser_init(p);
}
