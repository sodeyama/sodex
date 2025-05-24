#ifndef _ELFLOADER_H
#define _ELFLOADER_H

#include <sodex/const.h>
#include <sys/types.h>
#include <process.h>

typedef struct elf_header {
  char		magic[4];
  char		class;
  char		byteorder;
  char		hversion;
  char		pad[9];
  short		filetype;
  short		archtype;
  int		fversion;
  u_int32_t entry;
  u_int32_t phdrpos;
  u_int32_t shdrpos;
  int       flags;
  short     hdrsize;
  short     phdrent;
  short     phdrcnt;
  short     shdrent;
  short     shdrcnt;
  short     strsec;
} elf_header;

typedef struct elf_section_header {
  int   sh_name;
  int   sh_type;
  int   sh_flags;
  int   sh_addr;
  int   sh_offset;
  int   sh_size;
  int   sh_link;
  int   sh_info;
  int   sh_align;
  int   sh_entsize;
} elf_section_header;

typedef struct elf_symbol_table {
  int   name;
  int   value;
  int   size;
  char  type:4;
  char  bind:4;
  char  other;
  short sect;
} elf_symbol_table;

typedef struct elf_program_header {
  int       type;
  int       offset;
  u_int32_t virtaddr;
  u_int32_t physaddr;
  int       filesize;
  int       memsize;
  int       flags;
  int       align;
} elf_program_header;

#define ELF_PROGHEADER_FLAG_EXE     1
#define ELF_PROGHEADER_FLAG_WRITE   2
#define ELF_PROGHEADER_FLAG_READ    4

#define ELF_SUCCESS 1
#define ELF_FAIL    0

#define PATH_ENV_MAX 8

PUBLIC int elf_loader(char *filename, u_int32_t *entrypoint, void *loadaddr,
                      u_int32_t *pg_dir, struct task_struct* task,
                      u_int32_t *allocation_point);
PUBLIC int open_env(char* filename, int flags, mode_t mode);

#endif
