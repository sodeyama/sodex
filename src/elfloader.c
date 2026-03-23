/*
 *  @File        elfloader.c
 *  @Brief       elf loader for creating a new process
 *
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/07/24  update: 2007/07/24
 *
 *  Copyright (C) 2007 Sodex
 */

#include <elfloader.h>
#include <vga.h>
#include <memory.h>
#include <ext3fs.h>
#include <fs.h>
#include <page.h>
#include <lib.h>
#include <ihandlers.h>
#include <rs232c.h>

PRIVATE char* __create_filepath(const char* filename, const char* path);

PRIVATE char* path_usrbin = "/usr/bin";

PUBLIC int elf_loader(const char *filename, u_int32_t *entrypoint, void *loadaddr,
                      u_int32_t *pg_dir, struct task_struct* task,
                      u_int32_t* allocation_point)
{
  // This condition is for initializing kernel
  if (current == NULL)
    current = task;
  int fd = open_env(filename, O_RDWR, 0);
  if (fd == FS_OPEN_FAIL) {
    /* 相対コマンドの未検出は shell 側で整形して表示する。 */
    if (filename != NULL && filename[0] == '/')
      com1_printf("AUDIT elf_open_fail file=%s\r\n", filename);
    return ELF_FAIL;
  }
  ext3_inode* inode = FD_TOINODE(fd, current);
  com1_printf("AUDIT elf_open_ok file=%s size=%x\r\n", filename, inode->i_size);

  char *elf_buf = kalloc(inode->i_size);
  if (elf_buf == NULL) {
    com1_printf("AUDIT elf_buf_alloc_fail size=%x\r\n", inode->i_size);
    return ELF_FAIL;
  }
  memset(elf_buf, 0, inode->i_size);

#ifdef DEBUG
  _kprintf("i_size:%x\n", inode->i_size);
#endif
  ext3_read(fd, elf_buf, inode->i_size);

  elf_header *header = kalloc(sizeof(elf_header));
  if (header == NULL) {
    com1_printf("AUDIT elf_header_alloc_fail\r\n");
    return ELF_FAIL;
  }
  memcpy(header, elf_buf, sizeof(elf_header));

  if (strncmp(header->magic, "\177ELF", 4) != 0) {
    com1_printf("AUDIT elf_magic_fail fd=%x file=%s magic=%x%x%x%x\r\n",
                fd, filename,
                (u_int32_t)(u_int8_t)header->magic[0],
                (u_int32_t)(u_int8_t)header->magic[1],
                (u_int32_t)(u_int8_t)header->magic[2],
                (u_int32_t)(u_int8_t)header->magic[3]);
    return ELF_FAIL;
  }
  com1_printf("AUDIT elf_header_ok entry=%x phnum=%x shnum=%x\r\n",
              header->entry, (u_int32_t)header->phdrcnt,
              (u_int32_t)header->shdrcnt);

  elf_program_header *prg_header =
    kalloc(header->phdrent*header->phdrcnt);
  if (prg_header == NULL) {
    com1_printf("AUDIT elf_ph_alloc_fail bytes=%x\r\n",
                header->phdrent * header->phdrcnt);
    return ELF_FAIL;
  }
  memcpy(prg_header, elf_buf+header->phdrpos, header->phdrent*header->phdrcnt);

  elf_section_header *sect_header =
    kalloc(header->shdrent*header->shdrcnt);
  if (sect_header == NULL) {
    com1_printf("AUDIT elf_sh_alloc_fail bytes=%x\r\n",
                header->shdrent * header->shdrcnt);
    return ELF_FAIL;
  }
  memcpy(sect_header, elf_buf+header->shdrpos,
         header->shdrent*header->shdrcnt);

  u_int32_t prog_allsize = 0;
  u_int32_t old_cr3 = pg_get_cr3() + __PAGE_OFFSET;
  size_t last_prg_size;
  u_int32_t last_virtaddr;
  int i;
  for (i = 0; i < header->phdrcnt; i++) {
    size_t prg_size = prg_header[i].memsize;
    u_int32_t virtaddr = prg_header[i].virtaddr;
    void *mapped;
    if (prg_size == 0)
      continue;
    last_prg_size = prg_size;
    last_virtaddr = virtaddr;
    prog_allsize += prg_size;
    com1_printf("AUDIT elf_map_seg idx=%x va=%x mem=%x file=%x\r\n",
                (u_int32_t)i, virtaddr, prg_size, (u_int32_t)prg_header[i].filesize);
    mapped = set_process_page(pg_dir, virtaddr, prg_size);
    if (mapped == NULL) {
      com1_printf("AUDIT elf_map_fail idx=%x va=%x mem=%x\r\n",
                  (u_int32_t)i, virtaddr, prg_size);
      return ELF_FAIL;
    }
  }
  *allocation_point = CEIL(last_virtaddr+last_prg_size, BLOCK_SIZE);
  //_kprintf("allocpoint:%x\n", *allocation_point);

  pg_load_cr3(pg_dir);
  for (i = 0; i < header->phdrcnt; i++) {
    size_t prg_size = prg_header[i].memsize;
    int file_size = prg_header[i].filesize;
    u_int32_t pde;
    u_int32_t pte = 0;
    if (prg_size == 0)
      continue;
    pde = pg_dir[(prg_header[i].virtaddr >> 22) & 0x3ff];
    if (pde & PAGE_PRESENT) {
      u_int32_t *pg_tbl =
          (u_int32_t *)((pde & ~(BLOCK_SIZE - 1)) + __PAGE_OFFSET);
      pte = pg_tbl[(prg_header[i].virtaddr >> BLOCK_BITS) & 0x3ff];
    }
    com1_printf("AUDIT elf_copy_seg idx=%x va=%x pde=%x pte=%x\r\n",
                (u_int32_t)i, prg_header[i].virtaddr, pde, pte);
    if (file_size > 0)
      memcpy(prg_header[i].virtaddr, elf_buf + prg_header[i].offset, file_size);
    if (prg_size > file_size)
      memset(prg_header[i].virtaddr + file_size, 0, prg_size - file_size);
  }
  pg_load_cr3(old_cr3);

  *entrypoint = header->entry;
  /*
   * 実行中イメージの fd は子 task 側に残しておく。
   * ここで close すると、exec 直後の user task が不安定になる。
   */
  kfree(elf_buf);

  return ELF_SUCCESS;
}

PUBLIC int open_env(const char* filename, int flags, mode_t mode)
{
  if (filename == NULL || filename[0] == '\0')
    return FS_OPEN_FAIL;

  if (strchr(filename, '/') != NULL) {
    return ext3_open(filename, flags, mode);
  }

  char *PATH_ENV[PATH_ENV_MAX];
  memset(PATH_ENV, 0, PATH_ENV_MAX*4);
  PATH_ENV[0] = path_usrbin;

  int i;
  for (i=0; PATH_ENV[i] != NULL; i++) {
    char* newfilename = __create_filepath(filename, PATH_ENV[i]);
    int fd;
    int err;
    if (newfilename == NULL)
      continue;
    fd = ext3_open(newfilename, flags, mode);
    err = kfree(newfilename);
    if (err)
      _kprintf("%s:kfree error:%x\n", __func__, err);
    if (fd != FS_OPEN_FAIL)
      return fd;
  }
  return FS_OPEN_FAIL;
}

PRIVATE char* __create_filepath(const char* filename, const char* path)
{
  size_t path_len;
  size_t name_len;
  char* newfilename = kalloc(PATHNAME_MAX);
  if (newfilename == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset(newfilename, 0, PATHNAME_MAX);
  path_len = strlen(path);
  name_len = strlen(filename);
  memcpy(newfilename, path, path_len);
  memcpy(newfilename + path_len, "/", 1);
  memcpy(newfilename + path_len + 1, filename, name_len);
  newfilename[path_len + name_len + 1] = '\0';

  return newfilename;
}
