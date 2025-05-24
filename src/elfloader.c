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

PRIVATE char* __create_filepath(char* filename, char* path);

PRIVATE char* path_usrbin = "/usr/bin";

PUBLIC int elf_loader(char *filename, u_int32_t *entrypoint, void *loadaddr,
                      u_int32_t *pg_dir, struct task_struct* task,
                      u_int32_t* allocation_point)
{
  // This condition is for initializing kernel 
  if (current == NULL)
    current = task;
  int fd = open_env(filename, O_RDWR, 0);
  if (fd == 0) {
    //_kprintf("%s file open error\n", __func__);
    return ELF_FAIL;
  }
  ext3_inode* inode = FD_TOINODE(fd, current);

  char *elf_buf = kalloc(inode->i_size);
  if (elf_buf == NULL) {
    _kprintf("%s: kalloc error\n", __func__);
    return ELF_FAIL;
  }
  memset(elf_buf, 0, inode->i_size);

#ifdef DEBUG
  _kprintf("i_size:%x\n", inode->i_size);
#endif
  ext3_read(fd, elf_buf, inode->i_size);
  disable_pic_interrupt(IRQ_TIMER);

  elf_header *header = kalloc(sizeof(elf_header));
  if (header == NULL) {
    _kprintf("%s: elf_header kalloc error\n", __func__);
    return ELF_FAIL;
  }
  memcpy(header, elf_buf, sizeof(elf_header));

  if (strncmp(header->magic, "\177ELF", 4) != 0) {
    _kprintf("%s: The file of filedescriptor %x is not ELF format.\n"
             "The filename is %s\n", __func__, fd, filename);
    return ELF_FAIL;
  }

  elf_program_header *prg_header =
    kalloc(header->phdrent*header->phdrcnt);
  if (prg_header == NULL) {
    _kprintf("%s: prg_header kalloc error\n", __func__);
    return ELF_FAIL;
  }
  memcpy(prg_header, elf_buf+header->phdrpos, header->phdrent*header->phdrcnt);

  elf_section_header *sect_header =
    kalloc(header->shdrent*header->shdrcnt);
  if (sect_header == NULL) {
    _kprintf("%s: sect_header kalloc error\n", __func__);
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
    if (prg_size == 0)
      continue;
    last_prg_size = prg_size;
    last_virtaddr = virtaddr;
    prog_allsize += prg_size;
    set_process_page(pg_dir, virtaddr, prg_size);
  }
  *allocation_point = CEIL(last_virtaddr+last_prg_size, BLOCK_SIZE);
  //_kprintf("allocpoint:%x\n", *allocation_point);

  pg_load_cr3(pg_dir);
  for (i = 0; i < header->phdrcnt; i++) {
    size_t prg_size = prg_header[i].memsize;
    if (prg_size == 0)
      continue;
    //_kprintf("virtaddr:%x prg_size:%x offset:%x\n",
    //         prg_header[i].virtaddr, prg_size, prg_header[i].offset);
    if (prg_header[i].flags & ELF_PROGHEADER_FLAG_EXE)
      memcpy(prg_header[i].virtaddr, elf_buf + prg_header[i].offset, prg_size);
    else
      memset(prg_header[i].virtaddr, 0, prg_size);
  }
  pg_load_cr3(old_cr3);

  *entrypoint = header->entry;

  task->files->fs_fd[fd] = kalloc(sizeof(struct file));
  if (task->files->fs_fd[fd] == NULL) {
    _kprintf("%s: elf_header kalloc error\n", __func__);
    return ELF_FAIL;
  }

  memset(task->files->fs_fd[fd], 0, sizeof(struct file));
  task->files->fs_freefd++;

  /*
  struct file* pfile = task->files->fs_fd[fd];
  pfile->f_dentry = FD_TODENTRY(fd, current);
  pfile->f_dentry->d_elfhdr = header;
  pfile->f_dentry->d_elfscthdr = (void**)sect_header;
  pfile->f_dentry->d_elfprghdr = (void**)prg_header;
  */

  kfree(elf_buf);

  return ELF_SUCCESS;
}

PUBLIC int open_env(char* filename, int flags, mode_t mode)
{
  if (filename[0] == '/') {
    return ext3_open(filename, flags, mode);
  }

  char *PATH_ENV[PATH_ENV_MAX];
  memset(PATH_ENV, 0, PATH_ENV_MAX*4);
  PATH_ENV[0] = path_usrbin;

  int i;
  for (i=0; PATH_ENV[i] != NULL; i++) {
    char* newfilename = __create_filepath(filename, PATH_ENV[i]);
    int fd = ext3_open(newfilename, flags, mode);
    if (fd != 0)
      return fd;
  }
  return 0;
}

PRIVATE char* __create_filepath(char* filename, char* path)
{
  char* newfilename = kalloc(PATHNAME_MAX);
  if (newfilename == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memcpy(newfilename, path, strlen(path));
  memcpy(newfilename+strlen(path), "/", 1);
  memcpy(newfilename+strlen(path)+1, filename, strlen(filename));

  return newfilename;
}
