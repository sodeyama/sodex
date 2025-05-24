/*
 *  @File        ext3fs.c
 *  @Brief       ext3 file system
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/05/28  update: 2007/05/28
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <ext3fs.h>
#include <fs.h>
#include <process.h>
#include <memory.h>
#include <floppy.h>
#include <scsi.h>
#include <string.h>
#include <vga.h>
#include <lib.h>

PRIVATE ext3_super_block super_block;
PRIVATE ext3_group_desc group_descs[GROUP_MAX];
PRIVATE ext3_inode inodes[INODE_MAX];
PRIVATE u_int8_t inode_bitmap[BLOCK_SIZE];
PRIVATE u_int8_t block_bitmap[BLOCK_SIZE];

PRIVATE inline int IS_DIR(const char* pathname);
PRIVATE inline int IS_FILE(const char* pathname);
PRIVATE inline ext3_inode* ext3_get_inode(u_int32_t ino);
PRIVATE inline void ext3_set_inode(u_int32_t ino, ext3_inode* inode);

PRIVATE int __get_free_bitmap(u_int8_t* bitmap);
PRIVATE void __set_bitmap(u_int8_t* bitmap, u_int32_t bit);
PRIVATE int __alloc_ino();
PRIVATE int __alloc_block();
PRIVATE void init_dentry_lists(ext3_dentry* dentry);
PRIVATE void ext3_read_superblock(ext3_super_block *super);
PRIVATE void ext3_read_groupdesc(ext3_group_desc *group);
PRIVATE void ext3_read_inodes(ext3_inode *inode);
PRIVATE void ext3_read_inodebitmap(u_int8_t *ino_bitmap);
PRIVATE void ext3_read_blockbitmap(u_int8_t *blk_bitmap);
PRIVATE void ext3_init_dirty();
PRIVATE void __read_dentry(ext3_inode* inode, ext3_dentry* parent);
PRIVATE ext3_dentry* __read_rootdir(u_int32_t ino);
PRIVATE ext3_dentry* __create_file(const char* pathname,
                                   int flags, mode_t mode);
PRIVATE void __change_parentdir(ext3_dentry* parent, ext3_dentry* child);
PRIVATE ext3_dentry* __get_dentry(const char* dirname, ext3_dentry* dentry);
PRIVATE ext3_dentry* __dir_walk(const char* pathname,
                                ext3_dentry* search_dentry);
PRIVATE int __does_exist(ext3_inode* inode, int lblock, char** iblock_buf,
                         int* real_block);
PRIVATE int __insert_dir_data(char* datablock, int fileino, u_int8_t file_type,
                              char *name, int last);
PRIVATE void ext3_read_1block(ext3_dentry* dentry, void* buf, int lblock,
                              off_t first_pos, off_t end_pos);
PRIVATE void ext3_write_1block(ext3_dentry* dentry, void* buf, int lblock,
                               off_t first_pos, off_t end_pos);

PUBLIC void init_ext3fs()
{

  ext3_read_superblock(&super_block);
  //_kprintf("end of read superbloc\n");
  ext3_read_groupdesc(group_descs);
  //_kprintf("end of read groupdesc\n");
  ext3_read_inodes(inodes);
  //_kprintf("end of read inodes\n");
  ext3_read_inodebitmap(inode_bitmap);
  //_kprintf("end of read inodebitmap\n");
  ext3_read_blockbitmap(block_bitmap);
  //_kprintf("end of read blockbitmap\n");
  ext3_init_dirty();
  //_kprintf("end of read init dirty\n");

  rootdir = __read_rootdir(SODEX_ROOT_INO);
  //_kprintf("end of read rootdir\n");
}

PRIVATE void init_dentry_lists(ext3_dentry* dentry)
{
  init_dlist_set(&(dentry->d_child));
  init_dlist_set(&(dentry->d_subdirs));
  init_dlist_set(&(dentry->d_alias));
}

PRIVATE void ext3_read_superblock(ext3_super_block *super)
{
  void *buf = kalloc(S_SUPER_BLOCK);
  if (buf == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return;
  }
  rawdev.raw_read(P_SUPER_BLOCK/FDC_SECTOR_SIZE, S_SUPER_BLOCK/FDC_SECTOR_SIZE, buf);
  memcpy((char*)super, (char*)buf, S_SUPER_BLOCK);
  int err = kfree(buf);
  if (err)
    _kprintf("%s:kfree error:%x\n", __func__, err);
}

PRIVATE void ext3_read_groupdesc(ext3_group_desc *group)
{
  void *buf = kalloc(S_GROUP_DESC);
  if (buf == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return;
  }
  rawdev.raw_read(P_GROUP_DESC/FDC_SECTOR_SIZE, S_GROUP_DESC/FDC_SECTOR_SIZE, buf);
  memcpy((char*)group, (char*)buf, S_GROUP_DESC);
  int err = kfree(buf);
  if (err)
    _kprintf("%s:kfree error:%x\n", __func__, err);
}

PRIVATE void ext3_read_inodes(ext3_inode *inode)
{
  char *buf = kalloc(S_INODE_BLOCK);
  if (buf == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return;
  }
  rawdev.raw_read(P_INODE_BLOCK/FDC_SECTOR_SIZE, S_INODE_BLOCK/FDC_SECTOR_SIZE, buf);
  memcpy((char*)inode, buf, S_INODE_BLOCK);
  int err = kfree(buf);
  if (err)
    _kprintf("%s:kfree error:%x\n", __func__, err);
}

PRIVATE void ext3_read_inodebitmap(u_int8_t *ino_bitmap)
{
  void *buf = kalloc(S_INODE_BITMAP);
  if (buf == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return;
  }
  rawdev.raw_read(P_INODE_BITMAP/FDC_SECTOR_SIZE, S_INODE_BITMAP/FDC_SECTOR_SIZE,
           buf);
  memcpy((char*)ino_bitmap, (char*)buf, S_INODE_BITMAP);
  int err = kfree(buf);
  if (err)
    _kprintf("%s:kfree error:%x\n", __func__, err);
}

PRIVATE void ext3_read_blockbitmap(u_int8_t *blk_bitmap)
{
  void *buf = kalloc(S_BLOCK_BITMAP);
  if (buf == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return;
  }
  rawdev.raw_read(P_BLOCK_BITMAP/FDC_SECTOR_SIZE, S_BLOCK_BITMAP/FDC_SECTOR_SIZE,
           buf);
  memcpy((char*)blk_bitmap, (char*)buf, S_BLOCK_BITMAP);
  int err = kfree(buf);
  if (err)
    _kprintf("%s:kfree error:%x\n", __func__, err);
}

PRIVATE void ext3_init_dirty()
{
  rootdirty = kalloc(sizeof(ext3_dirty));
  if (rootdirty == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return;
  }
  memset((char*)rootdirty, 0, sizeof(ext3_dirty));
  init_dlist_set(&(rootdirty->d_inodirty.list));
  init_dlist_set(&(rootdirty->d_blkdirty.list));
}

PRIVATE int __alloc_ino()
{
  int newino;

  super_block.s_free_inodes_count--;
  group_descs[0].bg_free_inodes_count--;
  newino = __get_free_bitmap(inode_bitmap);
  if (newino == -1)
    _kprintf("%s:inode bitmap is full\n", __func__);
  super_block.s_first_ino = newino;
  __set_bitmap(inode_bitmap, newino);

#ifdef DIRTY
  // dirty set
  rootdirty->d_super = TRUE;
  rootdirty->d_group = TRUE;
  rootdirty->d_inobmp = TRUE;
#endif
  return newino;
}

PRIVATE int __alloc_block()
{
  int newblock = 0;

  super_block.s_free_blocks_count--;
  group_descs[0].bg_free_blocks_count--;
  newblock = __get_free_bitmap(block_bitmap);
  if (newblock == -1)
    _kprintf("%s:block bitmap is full\n", __func__);
  __set_bitmap(block_bitmap, newblock);

#ifdef DIRTY
  // dirty set
  rootdirty->d_super = TRUE;
  rootdirty->d_group = TRUE;
  rootdirty->d_blkbmp = TRUE;
#endif
  return newblock;
}

PRIVATE int __get_free_bitmap(u_int8_t* bitmap)
{
  int free = 0;
  int i, j;
  for (i = 0; i < BLOCK_SIZE; i++) {
    if ((0xff & ~(bitmap[i])) == 0) {
      free += 8;
      continue;
    }
    for (j = 0; j < 8; j++) {
      if (~(bitmap[i]) & (u_int8_t)pow(2, j)) {
        free += j;
        break;
      }
    }
    break;
  }
  if (i == BLOCK_SIZE)
    return -1;
  return free;
}

PRIVATE void __set_bitmap(u_int8_t* bitmap, u_int32_t bit)
{
  bitmap[bit/8] |= (1<<bit%8);
}

PRIVATE inline ext3_inode* ext3_get_inode(u_int32_t ino)
{
  if (ino == 0)
    return NULL;
  return &inodes[ino-1];
}

PRIVATE inline void ext3_set_inode(u_int32_t ino, ext3_inode* inode)
{
  inodes[ino] = *inode;
}

PRIVATE ext3_dentry* __read_rootdir(u_int32_t ino)
{
  ext3_inode* inode = ext3_get_inode(ino);
  ext3_dentry* dentry = (ext3_dentry*)kalloc(sizeof(ext3_dentry));
  if (dentry == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }

  dentry->d_inode = inode;
  dentry->d_inonum = ino;
  dentry->d_reclen = BLOCK_SIZE;
  dentry->d_namelen = 1;
  dentry->d_filetype = FTYPE_DIR;
  dentry->d_name = kalloc(2);
  if (dentry->d_name == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset(dentry->d_name, "/", 1);
  memset(dentry->d_name+1, 0, 1);
  dentry->d_flags = inode->i_flags;
  dentry->d_parent = NULL;
  init_dentry_lists(dentry);
  
  __read_dentry(inode, dentry);

  return dentry;
}

/*
 * If the ino is the one of directory, this function get the dentry and
 *  the inode recursively.
 */
PRIVATE void __read_dentry(ext3_inode* inode, ext3_dentry* parent)
{
  int kind = (inode->i_mode & SODEX_S_IFMT);
  switch (kind) {
  case SODEX_S_IFDIR:
    {
      char* blockbuf = kalloc(BLOCK_SIZE*(inode->i_block[0]));
      if (blockbuf == NULL) {
        _kprintf("%s kalloc error\n", __func__);
        return;
      }
      char* p = blockbuf;
      rawdev.raw_read(BLOCK_SIZE*(inode->i_block[0])/FDC_SECTOR_SIZE,
               BLOCK_SIZE/FDC_SECTOR_SIZE, blockbuf);
      parent->d_dirblock = p;
      u_int16_t len, sum_len = 0;
      int count = 0;
      while (TRUE) {
        ext3_dentry* dentry = (ext3_dentry*)kalloc(sizeof(ext3_dentry));
        if (dentry == NULL) {
          _kprintf("%s kalloc error\n", __func__);
          return;
        }
        len = (u_int16_t)((u_int16_t*)(p+4))[0];
        dentry->d_inonum = (u_int32_t)((u_int32_t*)p)[0];
        dentry->d_inode = ext3_get_inode(dentry->d_inonum);
        dentry->d_reclen = len;
        dentry->d_namelen = p[6];
        dentry->d_filetype = p[7];
        dentry->d_name = (char*)kalloc(EXT3_NAME_LEN);
        if (dentry->d_name == NULL) {
          _kprintf("%s kalloc error\n", __func__);
          return;
        }
        memcpy(dentry->d_name, p+8, (p[6]%4 == 0 ? p[6] : p[6]+4-p[6]%4));
        dentry->d_flags = dentry->d_inode->i_flags;
        dentry->d_parent = parent;
        init_dentry_lists(dentry);

        // chain between the parent and childrens
        dlist_insert_after(&(dentry->d_child), &(parent->d_subdirs));
        if (strcmp(dentry->d_name, ".") != 0 &&
            strcmp(dentry->d_name, "..") != 0) {
          // get the dentry recursively
          __read_dentry(dentry->d_inode, dentry);
        }
        p += len;
        sum_len += len;
        if (sum_len == BLOCK_SIZE)
          break;
      }
    }
    break;

  case SODEX_S_IFREG:
    // we don't do anything, because at the process of parent dir
    // we already set the dentry and the dentry's chain.
    break;
  }
}

PRIVATE inline int IS_DIR(const char* pathname)
{
  char lastchar = pathname[strlen(pathname)-1];
  if (lastchar == '/')
    return TRUE;
  else
    return FALSE;
}

PRIVATE inline int IS_FILE(const char* pathname)
{
  char lastchar = pathname[strlen(pathname)-1];
  if (lastchar != '/')
    return TRUE;
  else
    return FALSE;
}

PRIVATE ext3_dentry* __create_file(const char* pathname,
                                   int flags, mode_t mode)
{
  ext3_dentry* parentdir;
  char* filename;
  char* namebuf = kalloc(PATHNAME_MAX);
  if (namebuf == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset(namebuf, 0, PATHNAME_MAX);
  memcpy(namebuf, (char*)pathname, strlen(pathname));
  char* p = strrchr(namebuf, '/');
  if (p != namebuf) { // not root
    *p = 0;
    filename = p + 1;
    parentdir = __dir_walk(namebuf, rootdir);
  } else { // root
    filename = p + 1;
    parentdir = rootdir;
  }

  if (parentdir == NULL) {
    _kprintf("parentdir is null\n");
    return NULL;
  }

  // If the file we want to open already exist,
  //  return NULL
  if (__get_dentry(filename, parentdir) != NULL) {
    _kprintf("%s file already exist.\n", pathname);
    return NULL;
  }
  
  int newino = __alloc_ino();
  if (newino == -1)
    return NULL;
    
  ext3_inode* inode = kalloc(sizeof(ext3_inode));
  if (inode == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset(inode, 0, sizeof(ext3_inode));
  inode->i_mode = mode;
  inode->i_flags = (flags&O_DIRECTORY ? SODEX_S_IFDIR : SODEX_S_IFREG);
  ext3_set_inode(newino, inode);

  ext3_dentry* dentry = kalloc(sizeof(ext3_dentry));
  if (dentry == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset((char*)dentry, 0, sizeof(ext3_dentry));
  dentry->d_inode = inode;
  dentry->d_inonum = newino;
  dentry->d_namelen = strlen(filename);
  dentry->d_filetype = FTYPE_FILE;
  dentry->d_name = kalloc(EXT3_NAME_LEN);
  if (dentry->d_name == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memcpy(dentry->d_name, filename, strlen(filename));
  dentry->d_flags = flags;
  init_dentry_lists(dentry);

  dentry->d_parent = parentdir;
  dlist_insert_after(&(dentry->d_child), &(parentdir->d_subdirs));

  // change the parent directory block
  __change_parentdir(parentdir, dentry);

#ifdef DIRTY
  // inode dirty set
  ext3_inode_dirty* idirty = kalloc(sizeof(ext3_inode_dirty));
  if (idirty == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset((char*)idirty, 0, sizeof(ext3_inode_dirty));
  idirty->ino = dentry->d_inonum;
  idirty->inode = dentry->d_inode;
  dlist_insert_after(&(idirty->list), &(rootdirty->d_inodirty.list));
  // block dirty set
  ext3_block_dirty* bdirty = kalloc(sizeof(ext3_block_dirty));
  if (bdirty == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset((char*)bdirty, 0, sizeof(ext3_block_dirty));
  bdirty->iblock = dentry->d_parent->d_inode->i_block[0];
  bdirty->pblock = dentry->d_parent->d_dirblock;
  dlist_insert_after(&(bdirty->list), &(rootdirty->d_blkdirty.list));
#endif

  int err = kfree(namebuf);
  if (err)
    _kprintf("%s:kfree erro:%s\n", __func__, err);

  return dentry;
}

PRIVATE void __change_parentdir(ext3_dentry* parent, ext3_dentry* child)
{
  char* p = parent->d_dirblock;
  char* prev = p, *firstp = p;
  u_int16_t len, sumlen;
  len = sumlen = 0;

  while (sumlen < BLOCK_SIZE) {
    prev = p;
    len = ((u_int16_t*)(p+4))[0];
    p += len;
    sumlen += len;
  }
  u_int16_t prev_reclen = 8 + prev[6]; // 8 + namelen
  memcpy(prev+4, &prev_reclen, 2);

  char* newdirp = prev + prev_reclen;
  int namelen = child->d_namelen;
  if (namelen%4 != 0)
    namelen += 4 - namelen%4;
  int reclen = BLOCK_SIZE - (newdirp - firstp);

  memcpy(newdirp, &(child->d_inonum), 4);
  memcpy(newdirp+4, &reclen, 2);
  memcpy(newdirp+6, &(child->d_namelen), 1);
  memcpy(newdirp+7, &(child->d_filetype), 1);
  memcpy(newdirp+8, child->d_name, namelen);
}

PRIVATE ext3_dentry* __get_dentry(const char* filename, ext3_dentry* dentry)
{
  ext3_dentry* pdentry;
  struct dlist_set* p;
  dlist_for_each(p, &(dentry->d_subdirs)) {
    pdentry = dlist_entry(p, ext3_dentry, d_child);
    if (strcmp(filename, pdentry->d_name) == 0) {
      return pdentry;
    }
  }
  return NULL;
}

PUBLIC ext3_dentry* get_dentry_from_current(const char* filename)
{
  ext3_dentry* dentry = current->dentry;
  return __dir_walk(filename, dentry);
}

PUBLIC ext3_dentry* get_dentry_absolutely(const char* filename)
{
  return __dir_walk(filename, rootdir);
}

/*
 * example. pathname is "/foo/bar/hoge" (not "foo/bar/hoge")
 */
PRIVATE ext3_dentry* __dir_walk(const char* pathname,
                                ext3_dentry* search_dentry)
{
  ext3_dentry* dentry;
  char* p = strchr(pathname, '/');
  if (p == NULL) { // end of search
    return __get_dentry(pathname, search_dentry);
  } else if (pathname[0] == '/') { // pathname is /xxx
    dentry = search_dentry;
    return __dir_walk(p+1, dentry);
  } else {
    *p = 0;
    dentry = __get_dentry(pathname, search_dentry);
    return __dir_walk(p+1, dentry);
  }
}

PUBLIC int ext3_open(const char* pathname, int flags, mode_t mode)
{
  int fd = 0;
  ext3_dentry* dentry;
  char* org_buf = kalloc(PATHNAME_MAX);
  if (org_buf == NULL) {
    _kprintf("%s org_buf kalloc error\n", __func__);
    return 0;
  }
  char* buf = org_buf;
  
  int pathlen = strlen(pathname);
  strcpy(buf, pathname);
  buf[pathlen] = 0;
  if ( buf[pathlen-1] == '/' )
    buf[pathlen-1] = 0;

  // search the file from rootdir hierarchically
  dentry = __dir_walk(buf, rootdir);

  if (flags & O_CREAT) {
    if (dentry == NULL) {
      dentry = __create_file(pathname, flags, mode);
      if (dentry == NULL) {
        _kprintf("create file:fail\n");
        return FS_OPEN_FAIL;
      }
    }
  } else {
    if (dentry == NULL)
      return 0;
  }

  struct file* file = kalloc(sizeof(struct file));
  if (file == NULL) {
    _kprintf("%s struct file kalloc error\n", __func__);
    return 0;
  }

  memset((char*)file, 0, sizeof(struct file));
  file->f_mode = mode;
  file->f_flags = flags;
  file->f_dentry = dentry;
  file->f_stdioflag = FLAG_FILE;
  file->f_pos = 0;

  /*
  fd = gtask.fs_freefd++; // test process
  gtask.fs_fd[fd] = file; // test process
  */
  fd = current->files->fs_freefd++;
  current->files->fs_fd[fd] = file;

  int err = kfree(org_buf);
  if (err)
    _kprintf("%s:kfree error:%x\n", __func__, err);

  return fd;
}

PRIVATE void ext3_read_1block(ext3_dentry* dentry, void* buf, int lblock,
                              off_t first_pos, off_t end_pos)
{
  ext3_inode* inode = dentry->d_inode;

  int real_block;
  if (lblock <= BLOCK_ZERO_STAGE) {
    real_block = inode->i_block[lblock];
  } else if (lblock <= BLOCK_FIRST_STAGE) {
    int first_stage = inode->i_block[BLOCK_ZERO_STAGE+1];
    u_int32_t* temp = kalloc(BLOCK_SIZE);
    if (temp == NULL) {
      _kprintf("%s kalloc error\n", __func__);
      return;
    }
    rawdev.raw_read(first_stage*BLOCK_SIZE/FDC_SECTOR_SIZE,
             BLOCK_SIZE/FDC_SECTOR_SIZE, temp);
    real_block = temp[lblock - BLOCK_ZERO_STAGE - 1];
    int err = kfree(temp);
    if (err)
      _kprintf("%s:kfree error:%x\n", __func__, err);
  } else if (lblock <= BLOCK_SECOND_STAGE) {
    _kputs("We don't implement the second stage of i_blocks");
  } else if (lblock <= BLOCK_THIRD_STAGE) {
    _kputs("We don't implement the third stage of i_blocks");
  } else {
    _kputs("The size of data is too large blocks.");
  }

  char* readbuf = kalloc(BLOCK_SIZE);
  if (readbuf == NULL) {
    _kprintf("%s readbuf kalloc error\n", __func__);
    return;
  }
  rawdev.raw_read(real_block*BLOCK_SIZE/FDC_SECTOR_SIZE,
           BLOCK_SIZE/FDC_SECTOR_SIZE, readbuf);
  memcpy(buf, readbuf, end_pos - first_pos + 1);
  kfree(readbuf);
}

PUBLIC ssize_t ext3_read(int fd, void* buf, size_t count)
{
  ext3_dentry* dentry = FD_TODENTRY(fd, current);
  struct file* file = FD_TOFILE(fd, current);
  off_t fpos = file->f_pos;
  off_t i = fpos;
  while (i < fpos + count) {
    int lblock = i/BLOCK_SIZE;
    off_t end = ((i + BLOCK_SIZE > fpos + count) ?
                 fpos + count :
                 (lblock+1)*BLOCK_SIZE);
#ifdef DEBUG
    _kprintf("i:%x end:%x\n", i, end);
#endif
    ext3_read_1block(dentry, (char*)buf+i, lblock, i, end);
    i += (i == fpos ? BLOCK_SIZE-fpos : BLOCK_SIZE);
  }
  return (ssize_t)count;
}

PRIVATE int __does_exist(ext3_inode* inode, int lblock, char** iblock_buf,
                         int* real_block)
{
  if (lblock <= BLOCK_ZERO_STAGE) {
    *real_block = inode->i_block[lblock];
    if (*real_block == 0)
      return FALSE;
    else
      return TRUE;
  } else if (lblock <= BLOCK_FIRST_STAGE) {
    int first_stage = inode->i_block[BLOCK_ZERO_STAGE+1];
    rawdev.raw_read(first_stage*BLOCK_SIZE/FDC_SECTOR_SIZE,
             BLOCK_SIZE/FDC_SECTOR_SIZE, *iblock_buf);
    *real_block = (*iblock_buf)[lblock - BLOCK_ZERO_STAGE - 1];
    if (*real_block == 0)
      return FALSE;
    else
      return TRUE;
  } else if (lblock <= BLOCK_SECOND_STAGE) {
    _kputs("We don't implement the second stage of i_blocks");
  } else if (lblock <= BLOCK_THIRD_STAGE) {
    _kputs("We don't implement the third stage of i_blocks");
  } else {
    _kputs("The size of data is too large blocks.");
  }
  return FALSE;
}

PRIVATE void ext3_write_1block(ext3_dentry* dentry, void* buf, int lblock,
                               off_t first_pos, off_t end_pos)
{
  ext3_inode* inode = dentry->d_inode;
  char* readbuf = kalloc(BLOCK_SIZE);
  if (readbuf == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return;
  }
  memset(readbuf, 0, BLOCK_SIZE);
  int realblock;

  int exist = __does_exist(inode, lblock, &readbuf, &realblock);
  if (exist == TRUE) {
    char* block_buf = kalloc(BLOCK_SIZE);
    if (block_buf == NULL) {
      _kprintf("%s kalloc error\n", __func__);
      return;
    }
    rawdev.raw_read(realblock*BLOCK_SIZE/FDC_SECTOR_SIZE,
             BLOCK_SIZE/FDC_SECTOR_SIZE, block_buf);
    memcpy(block_buf + first_pos, buf, end_pos - first_pos + 1);

#ifdef DIRTY
    // dirty set
    ext3_block_dirty* bdirty = kalloc(sizeof(ext3_block_dirty));
    if (bdirty == NULL) {
      _kprintf("%s kalloc error\n", __func__);
      return;
    }
    memset((char*)bdirty, 0, sizeof(ext3_block_dirty));
    bdirty->iblock = realblock;
    bdirty->pblock = block_buf;
    dlist_insert_after(&(bdirty->list), &(rootdirty->d_blkdirty.list));
#endif
  } else {
    int newblock = __alloc_block();
    char* block_buf = kalloc(BLOCK_SIZE);
    if (block_buf == NULL) {
      _kprintf("%s kalloc error\n", __func__);
      return;
    }
    memset(block_buf, 0, BLOCK_SIZE);
    memcpy(block_buf + first_pos, buf, end_pos - first_pos);
#ifdef DIRTY
    // dirty set
    ext3_block_dirty* bdirty = kalloc(sizeof(ext3_block_dirty));
    if (bdirty == NULL) {
      _kprintf("%s kalloc error\n", __func__);
      return;
    }
    memset((char*)bdirty, 0, sizeof(ext3_block_dirty));
    bdirty->iblock = newblock;
    bdirty->pblock = block_buf;
    dlist_insert_after(&(bdirty->list), &(rootdirty->d_blkdirty.list));
#endif

    if (lblock <= BLOCK_ZERO_STAGE) {
      inode->i_block[lblock] = newblock;
#ifdef DIRTY
      // dirty set
      ext3_inode_dirty* idirty = kalloc(sizeof(ext3_inode_dirty));
      if (idirty == NULL) {
        _kprintf("%s kalloc error\n", __func__);
        return;
      }
      memset((char*)idirty, 0, sizeof(ext3_inode_dirty));
      idirty->ino = dentry->d_inonum;
      idirty->inode = dentry->d_inode;
      dlist_insert_after(&(idirty->list), &(rootdirty->d_inodirty.list));
#endif
    } else if (lblock <= BLOCK_FIRST_STAGE) {
      readbuf[lblock - BLOCK_ZERO_STAGE - 1] = newblock;

#ifdef DIRTY
      // dirty set
      ext3_block_dirty* in_bdirty = kalloc(sizeof(ext3_block_dirty));
      if (in_bdirty == NULL) {
        _kprintf("%s kalloc error\n", __func__);
        return;
      }
      memset((char*)in_bdirty, 0, sizeof(ext3_block_dirty));
      in_bdirty->iblock = inode->i_block[BLOCK_ZERO_STAGE+1];
      in_bdirty->pblock = readbuf;
      dlist_insert_after(&(in_bdirty->list), &(rootdirty->d_blkdirty.list));
#endif
    } else if (lblock <= BLOCK_SECOND_STAGE) {
      _kputs("We don't implement the second stage of i_blocks");
    } else if (lblock <= BLOCK_THIRD_STAGE) {
      _kputs("We don't implement the third stage of i_blocks");
    } else {
      _kputs("The size of data is too large blocks.");
    }

    super_block.s_free_blocks_count--;
    group_descs[0].bg_free_blocks_count--;
#ifdef DIRTY
    // dirty set
    rootdirty->d_super = TRUE;
    rootdirty->d_group = TRUE;
    rootdirty->d_inobmp = TRUE;
#endif
  }
}

PUBLIC ssize_t ext3_write(int fd, void* buf, size_t count)
{
  ext3_dentry* dentry = FD_TODENTRY(fd, current);
  struct file* file = FD_TOFILE(fd, current);
  off_t fpos = file->f_pos;
  off_t i = fpos;
  while (i < fpos + count) {
    int lblock = i/BLOCK_SIZE;
    off_t end = ((i + BLOCK_SIZE > fpos + count) ?
                 fpos + count :
                 (lblock+1)*BLOCK_SIZE);
    ext3_write_1block(dentry, (char*)buf, lblock, i, end);
    i += (i == fpos ? BLOCK_SIZE-fpos : BLOCK_SIZE);
  }
  ext3_flush();
  return count;
}

PRIVATE int __insert_dir_data(char* datablock, int fileino, u_int8_t file_type,
                              char *name, int last)
{
  u_int16_t rec_len;
  u_int8_t  pure_name_len, name_len;

  name_len = strlen(name);
  pure_name_len = name_len;
  if (name_len%4 != 0)
    name_len += 4 - name_len%4;
  
  char* name_buf = kalloc(name_len);
  if (name_buf == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return 0;
  }
  memset(name_buf, 0, name_len);
  memcpy(name_buf, name, strlen(name));
  rec_len = 8 + name_len;

  // rec_len check and search null point
  u_int16_t len, sum_len = 0;
  char *p = datablock;
  while (TRUE) {
    if (p[4] == 0 && p[5] == 0)
      break;
    else {
      len = ((u_int16_t*)(p+4))[0];
      p += len;
      sum_len += len;
    }
  }

  if (last)
    rec_len = BLOCK_SIZE - sum_len;

  memcpy(p, &fileino, 4);
  memcpy(p+4, &rec_len, 2);
  memcpy(p+6, &pure_name_len, 1);
  memcpy(p+7, &file_type, 1);
  memcpy(p+8, name_buf, name_len);

  int err = kfree(name_buf);
  if (err)
    _kprintf("%s:kfree error:%s\n", __func__, err);

  return rec_len;
}

PUBLIC int ext3_mkdir(const char* pathname, mode_t mode)
{
  ext3_dentry* dentry = __create_file(pathname, O_CREAT|O_DIRECTORY, mode);

  ext3_dentry* parentdir = dentry->d_parent;

  int iblock = __alloc_block();
  ext3_inode* inode = dentry->d_inode;

  inode->i_block[0] = iblock;
  char* datablock = kalloc(BLOCK_SIZE);
  if (datablock == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return 0;
  }
  memset(datablock, 0, BLOCK_SIZE);

  __insert_dir_data(datablock, dentry->d_inonum, FTYPE_DIR, ".", FALSE);
  __insert_dir_data(datablock, parentdir->d_inonum, FTYPE_DIR, "..", TRUE);

  super_block.s_free_blocks_count--;
  group_descs[0].bg_free_blocks_count--;

#ifdef DIRTY
  rootdirty->d_super = TRUE;
  rootdirty->d_group = TRUE;
  rootdirty->d_inobmp = TRUE;
  ext3_block_dirty* bdirty = kalloc(sizeof(ext3_block_dirty));
  if (bdirty == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return 0;
  }  
  memset((char*)bdirty, 0, sizeof(ext3_block_dirty));
  bdirty->iblock = iblock;
  bdirty->pblock = datablock;
  dlist_insert_after(&(bdirty->list), &(rootdirty->d_blkdirty.list));
#endif
  ext3_flush();
  return MKDIR_SUCCESS;
}

PUBLIC void ext3_ls(ext3_dentry* dentry)
{
  ext3_dentry* pdentry;
  struct dlist_set* plist;
  char buf[EXT3_NAME_LEN+1];
  memset(buf, 0, EXT3_NAME_LEN+1);

  dlist_for_each_prev(plist, &(dentry->d_subdirs)) {
    pdentry = dlist_entry(plist, ext3_dentry, d_child);
    memcpy(buf, pdentry->d_name, pdentry->d_namelen);
    buf[pdentry->d_namelen] = '\0';
    _kprintf("pdentry:%x %x %s\n", pdentry, pdentry->d_inonum, buf);
  }
}

PUBLIC void ext3_flush()
{
  if (rootdirty->d_super)
    rawdev.raw_write(P_SUPER_BLOCK/FDC_SECTOR_SIZE, S_SUPER_BLOCK/FDC_SECTOR_SIZE,
              (void*)&super_block);
  if (rootdirty->d_group)
    rawdev.raw_write(P_GROUP_DESC/FDC_SECTOR_SIZE, S_GROUP_DESC/FDC_SECTOR_SIZE,
              (void*)group_descs);
  if (rootdirty->d_blkbmp)
    rawdev.raw_write(P_BLOCK_BITMAP/FDC_SECTOR_SIZE, S_BLOCK_BITMAP/FDC_SECTOR_SIZE,
              (void*)block_bitmap);
  if (rootdirty->d_inobmp)
    rawdev.raw_write(P_INODE_BITMAP/FDC_SECTOR_SIZE, S_INODE_BITMAP/FDC_SECTOR_SIZE,
              (void*)inode_bitmap);

  ext3_inode_dirty* inodirty;
  ext3_block_dirty* blkdirty;
  struct dlist_set* p;
  dlist_for_each(p, &(rootdirty->d_inodirty.list)) {
    inodirty = dlist_entry(p, ext3_inode_dirty, list);
    int ino_pos
      = (inodirty->ino * sizeof(ext3_inode))/FDC_SECTOR_SIZE;
    char* write_pos = (char*)inodirty->inode;
    rawdev.raw_write(P_INODE_BLOCK/FDC_SECTOR_SIZE + ino_pos, 1, write_pos);
  }
  dlist_for_each(p, &(rootdirty->d_blkdirty.list)) {
    blkdirty = dlist_entry(p, ext3_block_dirty, list);
    rawdev.raw_write(blkdirty->iblock*BLOCK_PER_SECTOR, BLOCK_SIZE/FDC_SECTOR_SIZE,
              (char*)blkdirty->pblock);
    //int err = kfree(blkdirty->pblock);
    //if (err)
    //  _kprintf("%s:kfree error:%s\n", __func__, err);
  }
  rootdirty->d_super = FALSE;
  rootdirty->d_group = FALSE;
  rootdirty->d_inobmp = FALSE;
  rootdirty->d_blkbmp = FALSE;
  init_dlist_set(&(rootdirty->d_inodirty.list));
  init_dlist_set(&(rootdirty->d_blkdirty.list));
}

PUBLIC void ext3_dirty_print()
{
  _kprintf("super:%x, group:%x, inobmp:%x, blkbmp:%x\n",
           rootdirty->d_super, rootdirty->d_group,
           rootdirty->d_inobmp, rootdirty->d_blkbmp);
  ext3_inode_dirty* inodirty;
  ext3_block_dirty* blkdirty;
  struct dlist_set* p;
  _kprintf("The dirty inode  ");
  dlist_for_each(p, &(rootdirty->d_inodirty.list)) {
    inodirty = dlist_entry(p, ext3_inode_dirty, list);
    _kprintf("ino:%x ", inodirty->ino);
  }
  _kputc('\n');
  _kprintf("The dirty block  ");
  dlist_for_each(p, &(rootdirty->d_blkdirty.list)) {
    blkdirty = dlist_entry(p, ext3_block_dirty, list);
    _kprintf("iblock:%x ", blkdirty->iblock);
  }
  _kputc('\n');
}

