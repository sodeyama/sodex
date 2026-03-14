#include <utf8.h>

static int utf8_is_valid_codepoint(u_int32_t codepoint);
static int utf8_is_continuation(unsigned char byte);

static int utf8_is_valid_codepoint(u_int32_t codepoint)
{
  if (codepoint > 0x10ffffU)
    return 0;
  if (codepoint >= 0xd800U && codepoint <= 0xdfffU)
    return 0;
  return 1;
}

static int utf8_is_continuation(unsigned char byte)
{
  return (byte & 0xc0U) == 0x80U;
}

void utf8_decoder_init(struct utf8_decoder *decoder)
{
  if (decoder == 0)
    return;
  decoder->codepoint = 0;
  decoder->min_codepoint = 0;
  decoder->expected = 0;
  decoder->seen = 0;
}

void utf8_decoder_reset(struct utf8_decoder *decoder)
{
  utf8_decoder_init(decoder);
}

int utf8_decode_byte(struct utf8_decoder *decoder, unsigned char byte,
                     u_int32_t *codepoint)
{
  if (decoder == 0 || codepoint == 0)
    return -1;

  if (decoder->expected == 0) {
    if (byte < 0x80U) {
      *codepoint = (u_int32_t)byte;
      return 1;
    }
    if ((byte & 0xe0U) == 0xc0U) {
      decoder->codepoint = (u_int32_t)(byte & 0x1fU);
      decoder->min_codepoint = 0x80U;
      decoder->expected = 2;
      decoder->seen = 1;
      return 0;
    }
    if ((byte & 0xf0U) == 0xe0U) {
      decoder->codepoint = (u_int32_t)(byte & 0x0fU);
      decoder->min_codepoint = 0x800U;
      decoder->expected = 3;
      decoder->seen = 1;
      return 0;
    }
    if ((byte & 0xf8U) == 0xf0U) {
      decoder->codepoint = (u_int32_t)(byte & 0x07U);
      decoder->min_codepoint = 0x10000U;
      decoder->expected = 4;
      decoder->seen = 1;
      return 0;
    }
    return -1;
  }

  if (utf8_is_continuation(byte) == 0) {
    utf8_decoder_reset(decoder);
    return -1;
  }

  decoder->codepoint = (decoder->codepoint << 6) | (u_int32_t)(byte & 0x3fU);
  decoder->seen++;
  if (decoder->seen < decoder->expected)
    return 0;

  *codepoint = decoder->codepoint;
  if (*codepoint < decoder->min_codepoint ||
      utf8_is_valid_codepoint(*codepoint) == 0) {
    utf8_decoder_reset(decoder);
    return -1;
  }

  utf8_decoder_reset(decoder);
  return 1;
}

int utf8_decode_one(const char *data, int len, u_int32_t *codepoint,
                    int *consumed)
{
  struct utf8_decoder decoder;
  int i;
  int result;

  if (data == 0 || len <= 0 || codepoint == 0 || consumed == 0)
    return -1;

  utf8_decoder_init(&decoder);
  for (i = 0; i < len && i < 4; i++) {
    result = utf8_decode_byte(&decoder, (unsigned char)data[i], codepoint);
    if (result > 0) {
      *consumed = i + 1;
      return 1;
    }
    if (result < 0) {
      *codepoint = UTF8_REPLACEMENT_CHAR;
      *consumed = 1;
      return -1;
    }
  }

  *codepoint = UTF8_REPLACEMENT_CHAR;
  *consumed = 1;
  return -1;
}

int utf8_encode(u_int32_t codepoint, char *out)
{
  if (out == 0)
    return -1;
  if (utf8_is_valid_codepoint(codepoint) == 0)
    codepoint = UTF8_REPLACEMENT_CHAR;

  if (codepoint < 0x80U) {
    out[0] = (char)codepoint;
    return 1;
  }
  if (codepoint < 0x800U) {
    out[0] = (char)(0xc0U | (codepoint >> 6));
    out[1] = (char)(0x80U | (codepoint & 0x3fU));
    return 2;
  }
  if (codepoint < 0x10000U) {
    out[0] = (char)(0xe0U | (codepoint >> 12));
    out[1] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
    out[2] = (char)(0x80U | (codepoint & 0x3fU));
    return 3;
  }

  out[0] = (char)(0xf0U | (codepoint >> 18));
  out[1] = (char)(0x80U | ((codepoint >> 12) & 0x3fU));
  out[2] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
  out[3] = (char)(0x80U | (codepoint & 0x3fU));
  return 4;
}

int utf8_next_char_end(const char *data, int len, int index)
{
  u_int32_t codepoint;
  int consumed;

  if (data == 0)
    return index;
  if (index < 0)
    index = 0;
  if (index >= len)
    return len;

  utf8_decode_one(data + index, len - index, &codepoint, &consumed);
  return index + consumed;
}

int utf8_prev_char_start(const char *data, int len, int index)
{
  int i;

  if (data == 0)
    return index;
  if (index <= 0)
    return 0;
  if (index > len)
    index = len;

  i = index - 1;
  while (i > 0 && utf8_is_continuation((unsigned char)data[i]))
    i--;
  return i;
}
