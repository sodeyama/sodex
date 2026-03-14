#ifndef _USR_UTF8_H
#define _USR_UTF8_H

#include <sys/types.h>

#define UTF8_REPLACEMENT_CHAR 0xfffdU

struct utf8_decoder {
  u_int32_t codepoint;
  u_int32_t min_codepoint;
  unsigned char expected;
  unsigned char seen;
};

void utf8_decoder_init(struct utf8_decoder *decoder);
void utf8_decoder_reset(struct utf8_decoder *decoder);
int utf8_decode_byte(struct utf8_decoder *decoder, unsigned char byte,
                     u_int32_t *codepoint);
int utf8_decode_one(const char *data, int len, u_int32_t *codepoint,
                    int *consumed);
int utf8_encode(u_int32_t codepoint, char *out);
int utf8_next_char_end(const char *data, int len, int index);
int utf8_prev_char_start(const char *data, int len, int index);

#endif /* _USR_UTF8_H */
