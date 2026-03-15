#include <fs.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

#define MEMGROW_CHUNK_SIZE  (512 * 1024)
#define MEMGROW_CHUNK_COUNT 32
#define MEMGROW_TOUCH_STEP  4096

static int append_text(char *buf, int offset, const char *text)
{
  while (*text != '\0') {
    buf[offset] = *text;
    offset++;
    text++;
  }
  return offset;
}

static int append_hex32(char *buf, int offset, u_int32_t value)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  buf[offset++] = '0';
  buf[offset++] = 'x';
  for (shift = 28; shift >= 0; shift -= 4) {
    buf[offset++] = hex[(value >> shift) & 0x0f];
  }
  return offset;
}

static int write_report(const char *path, const char *status,
                        u_int32_t alloc_before, u_int32_t alloc_after,
                        u_int32_t touched_bytes)
{
  char report[256];
  int fd;
  int len = 0;

  fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0)
    return -1;

  len = append_text(report, len, "status=");
  len = append_text(report, len, status);
  len = append_text(report, len, "\nalloc_before=");
  len = append_hex32(report, len, alloc_before);
  len = append_text(report, len, "\nalloc_after=");
  len = append_hex32(report, len, alloc_after);
  len = append_text(report, len, "\ntouched_bytes=");
  len = append_hex32(report, len, touched_bytes);
  len = append_text(report, len, "\n");

  write(fd, report, len);
  close(fd);
  return 0;
}

static void touch_chunk(unsigned char *ptr, unsigned char tag)
{
  u_int32_t offset;

  for (offset = 0; offset < MEMGROW_CHUNK_SIZE; offset += MEMGROW_TOUCH_STEP) {
    ptr[offset] = tag;
  }
  ptr[MEMGROW_CHUNK_SIZE - 1] = (unsigned char)(tag ^ 0x5a);
}

static int verify_chunk(unsigned char *ptr, unsigned char tag)
{
  u_int32_t offset;

  for (offset = 0; offset < MEMGROW_CHUNK_SIZE; offset += MEMGROW_TOUCH_STEP) {
    if (ptr[offset] != tag)
      return -1;
  }
  if (ptr[MEMGROW_CHUNK_SIZE - 1] != (unsigned char)(tag ^ 0x5a))
    return -1;
  return 0;
}

int main(int argc, char **argv)
{
  const char *report_path = "memgrow.txt";
  unsigned char *chunks[MEMGROW_CHUNK_COUNT];
  struct task_struct *task = (struct task_struct*)getpstat();
  u_int32_t alloc_before = task->allocpoint;
  u_int32_t alloc_after;
  u_int32_t touched_bytes = 0;
  int i;

  if (argc >= 2)
    report_path = argv[1];

  for (i = 0; i < MEMGROW_CHUNK_COUNT; i++)
    chunks[i] = NULL;

  for (i = 0; i < MEMGROW_CHUNK_COUNT; i++) {
    chunks[i] = (unsigned char*)malloc(MEMGROW_CHUNK_SIZE);
    if (chunks[i] == NULL) {
      alloc_after = ((struct task_struct*)getpstat())->allocpoint;
      write_report(report_path, "alloc_fail", alloc_before, alloc_after,
                   touched_bytes);
      printf("memgrow: malloc failed\n");
      exit(1);
      return 1;
    }
    touch_chunk(chunks[i], (unsigned char)(i + 1));
    touched_bytes += MEMGROW_CHUNK_SIZE;
  }

  for (i = 0; i < MEMGROW_CHUNK_COUNT; i++) {
    if (verify_chunk(chunks[i], (unsigned char)(i + 1)) != 0) {
      alloc_after = ((struct task_struct*)getpstat())->allocpoint;
      write_report(report_path, "verify_fail", alloc_before, alloc_after,
                   touched_bytes);
      printf("memgrow: verify failed\n");
      exit(1);
      return 1;
    }
  }

  alloc_after = ((struct task_struct*)getpstat())->allocpoint;
  if (alloc_after <= alloc_before) {
    write_report(report_path, "allocpoint_fail", alloc_before, alloc_after,
                 touched_bytes);
    printf("memgrow: allocpoint did not grow\n");
    exit(1);
    return 1;
  }

  if (write_report(report_path, "ok", alloc_before, alloc_after,
                   touched_bytes) < 0) {
    printf("memgrow: report write failed\n");
    exit(1);
    return 1;
  }

  printf("memgrow: ok\n");
  exit(0);
  return 0;
}
