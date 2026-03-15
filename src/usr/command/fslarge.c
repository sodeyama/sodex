#include <fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FSLARGE_BLOCK_SIZE 4096
#define FSLARGE_SAMPLE_COUNT 6
#define FSLARGE_PREFIX_LEN 12

static int append_text(char *buf, int offset, const char *text)
{
  while (*text != '\0') {
    buf[offset++] = *text++;
  }
  return offset;
}

static int append_hex32(char *buf, int offset, u_int32_t value)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  buf[offset++] = '0';
  buf[offset++] = 'x';
  for (shift = 28; shift >= 0; shift -= 4)
    buf[offset++] = hex[(value >> shift) & 0x0f];
  return offset;
}

static void make_expected_prefix(char *buf, u_int32_t block_index)
{
  static const char hex[] = "0123456789abcdef";
  int shift;
  int pos = 0;

  buf[pos++] = 'B';
  buf[pos++] = 'L';
  buf[pos++] = 'K';
  buf[pos++] = ':';
  for (shift = 28; shift >= 0; shift -= 4)
    buf[pos++] = hex[(block_index >> shift) & 0x0f];
}

static int write_report(const char *path, const char *status,
                        u_int32_t size, u_int32_t block_index)
{
  char report[128];
  int fd;
  int len = 0;

  fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0)
    return -1;

  len = append_text(report, len, "status=");
  len = append_text(report, len, status);
  len = append_text(report, len, "\nsize=");
  len = append_hex32(report, len, size);
  len = append_text(report, len, "\nblock=");
  len = append_hex32(report, len, block_index);
  len = append_text(report, len, "\n");

  write(fd, report, len);
  close(fd);
  return 0;
}

static int verify_block(int fd, u_int32_t block_index)
{
  char expected[FSLARGE_PREFIX_LEN];
  char actual[FSLARGE_PREFIX_LEN];
  off_t offset = (off_t)block_index * FSLARGE_BLOCK_SIZE;
  int i;

  if (lseek(fd, offset, SEEK_SET) < 0)
    return -1;
  if (read(fd, actual, sizeof(actual)) != sizeof(actual))
    return -1;
  make_expected_prefix(expected, block_index);
  for (i = 0; i < FSLARGE_PREFIX_LEN; i++) {
    if (actual[i] != expected[i])
      return -1;
  }
  return 0;
}

int main(int argc, char **argv)
{
  const char *path = "/usr/bin/largefile.bin";
  const char *report_path = "/large_report.txt";
  u_int32_t expected_blocks = 1280;
  u_int32_t sample_blocks[FSLARGE_SAMPLE_COUNT];
  off_t size;
  int fd;
  int i;

  if (argc >= 2)
    path = argv[1];
  if (argc >= 3)
    report_path = argv[2];
  if (argc >= 4)
    expected_blocks = (u_int32_t)atoi(argv[3]);

  fd = open(path, O_RDONLY, 0);
  if (fd < 0) {
    write_report(report_path, "open_fail", 0, 0);
    exit(1);
    return 1;
  }

  size = lseek(fd, 0, SEEK_END);
  if (size < 0) {
    write_report(report_path, "size_fail", 0, 0);
    close(fd);
    exit(1);
    return 1;
  }
  if ((u_int32_t)size != expected_blocks * FSLARGE_BLOCK_SIZE) {
    write_report(report_path, "size_mismatch", (u_int32_t)size, expected_blocks);
    close(fd);
    exit(1);
    return 1;
  }

  sample_blocks[0] = 0;
  sample_blocks[1] = 11;
  sample_blocks[2] = 12;
  sample_blocks[3] = 1035;
  sample_blocks[4] = 1036;
  sample_blocks[5] = expected_blocks - 1;

  for (i = 0; i < FSLARGE_SAMPLE_COUNT; i++) {
    if (verify_block(fd, sample_blocks[i]) != 0) {
      write_report(report_path, "verify_fail", (u_int32_t)size, sample_blocks[i]);
      close(fd);
      exit(1);
      return 1;
    }
  }

  close(fd);
  if (write_report(report_path, "ok", (u_int32_t)size, sample_blocks[FSLARGE_SAMPLE_COUNT - 1]) < 0) {
    exit(1);
    return 1;
  }

  exit(0);
  return 0;
}
