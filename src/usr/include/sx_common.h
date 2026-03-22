#ifndef _USR_SX_COMMON_H
#define _USR_SX_COMMON_H

#include <sys/types.h>

#define SX_MAX_TOKENS 256
#define SX_MAX_STATEMENTS 64
#define SX_MAX_BLOCKS 32
#define SX_MAX_FUNCTIONS 8
#define SX_MAX_BINDINGS 64
#define SX_MAX_SCOPE_DEPTH 16
#define SX_MAX_CALL_DEPTH 16
#define SX_NAME_MAX 32
#define SX_TEXT_MAX 256
#define SX_DIAG_MESSAGE_MAX 128

struct sx_source_span {
  int offset;
  int length;
  int line;
  int column;
};

struct sx_diagnostic {
  struct sx_source_span span;
  char message[SX_DIAG_MESSAGE_MAX];
};

static inline void sx_clear_diagnostic(struct sx_diagnostic *diag)
{
  int i;

  if (diag == 0)
    return;
  diag->span.offset = 0;
  diag->span.length = 0;
  diag->span.line = 0;
  diag->span.column = 0;
  for (i = 0; i < SX_DIAG_MESSAGE_MAX; i++)
    diag->message[i] = '\0';
}

static inline int sx_copy_text(char *dst, int cap, const char *src)
{
  int i;

  if (dst == 0 || cap <= 0)
    return -1;
  if (src == 0)
    src = "";
  for (i = 0; i < cap - 1 && src[i] != '\0'; i++)
    dst[i] = src[i];
  dst[i] = '\0';
  return src[i] == '\0' ? 0 : -1;
}

static inline int sx_copy_slice(char *dst, int cap, const char *src, int len)
{
  int i;

  if (dst == 0 || cap <= 0 || src == 0 || len < 0)
    return -1;
  if (len >= cap)
    len = cap - 1;
  for (i = 0; i < len; i++)
    dst[i] = src[i];
  dst[len] = '\0';
  return 0;
}

static inline void sx_set_diagnostic(struct sx_diagnostic *diag,
                                     int offset, int length,
                                     int line, int column,
                                     const char *message)
{
  if (diag == 0)
    return;
  diag->span.offset = offset;
  diag->span.length = length;
  diag->span.line = line;
  diag->span.column = column;
  sx_copy_text(diag->message, sizeof(diag->message), message);
}

#endif
