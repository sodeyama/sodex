/*
 * sse_parser.h - Server-Sent Events parser for streaming responses
 *
 * Handles TCP fragmentation by buffering incomplete lines.
 * Implements W3C SSE protocol (event:, data:, comment, blank dispatch).
 */

#ifndef _SSE_PARSER_H
#define _SSE_PARSER_H

#define SSE_MAX_EVENT_NAME   64
#define SSE_MAX_DATA_LEN   8192

/* Return codes from sse_feed() */
enum sse_event_type {
    SSE_EVENT_NONE = 0,
    SSE_EVENT_DATA,         /* Complete event ready in out */
    SSE_EVENT_NEED_MORE,    /* Input consumed, need more data */
    SSE_EVENT_ERROR,        /* Parse error (e.g., buffer overflow) */
    SSE_EVENT_DONE,         /* Stream ended */
};

struct sse_event {
    char event_name[SSE_MAX_EVENT_NAME];   /* "message_start" etc. */
    char data[SSE_MAX_DATA_LEN];           /* Concatenated data fields */
    int  data_len;
};

struct sse_parser {
    /* Line buffer for incomplete lines across recv boundaries */
    char line_buf[SSE_MAX_DATA_LEN];
    int  line_len;

    /* Event being built */
    struct sse_event pending;
    int has_event_name;
    int has_data;

    /* Feed state: offset into current chunk */
    int consumed;
};

/* Initialize parser state */
void sse_parser_init(struct sse_parser *p);

/*
 * Feed received data to the parser.
 * chunk/chunk_len: raw data from recv().
 * out: filled when a complete event is ready.
 *
 * Returns:
 *   SSE_EVENT_DATA      - out contains a complete event
 *   SSE_EVENT_NEED_MORE - all input consumed, need more data
 *   SSE_EVENT_ERROR     - parse error
 *
 * Call repeatedly with the same chunk until SSE_EVENT_NEED_MORE,
 * since one chunk may contain multiple events.
 */
int sse_feed(struct sse_parser *p,
             const char *chunk, int chunk_len,
             struct sse_event *out);

/* Get number of bytes consumed from the last chunk */
int sse_consumed(const struct sse_parser *p);

/* Reset parser to initial state */
void sse_parser_reset(struct sse_parser *p);

#endif /* _SSE_PARSER_H */
