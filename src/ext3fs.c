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
PRIVATE void __clear_bitmap(u_int8_t* bitmap, u_int32_t bit);
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
PRIVATE ext3_dentry* __get_dentry(const char* dirname, ext3_dentry* dentry);
PRIVATE ext3_dentry* __dir_walk(const char* pathname,
                                ext3_dentry* search_dentry);
PRIVATE void ext3_trim_trailing_slash(char *path);
PRIVATE ext3_dentry* ext3_resolve_path(char *pathbuf);
PRIVATE ext3_dentry* ext3_resolve_parent(char *pathbuf, char **name_out);
PRIVATE void ext3_mark_inode_dirty(ext3_dentry *dentry);
PRIVATE void ext3_mark_block_dirty(u_int32_t iblock, char *block);
PRIVATE int ext3_padded_name_len(int len);
PRIVATE void ext3_write_dirent(char *dst, ext3_dentry *dentry, u_int16_t rec_len);
PRIVATE void ext3_rebuild_dirblock(ext3_dentry *parent);
PRIVATE u_int32_t* ext3_load_block_table(u_int32_t iblock);
PRIVATE int ext3_inode_get_block(ext3_inode *inode, int lblock);
PRIVATE int ext3_inode_ensure_block(ext3_dentry *dentry, int lblock);
PRIVATE void ext3_free_block(u_int32_t block);
PRIVATE void ext3_release_inode_blocks(ext3_dentry *dentry);
PRIVATE int ext3_is_open_dentry(ext3_dentry *target);
PRIVATE int ext3_is_dir_empty(ext3_dentry *dentry);
PRIVATE void ext3_release_inode(ext3_dentry *dentry);
PRIVATE int ext3_name_equal(const char *name, ext3_dentry *dentry);

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

PRIVATE void __clear_bitmap(u_int8_t* bitmap, u_int32_t bit)
{
  bitmap[bit/8] &= (u_int8_t)(~(1 << (bit % 8)));
}

PRIVATE inline ext3_inode* ext3_get_inode(u_int32_t ino)
{
  if (ino == 0)
    return NULL;
  return &inodes[ino-1];
}

PRIVATE inline void ext3_set_inode(u_int32_t ino, ext3_inode* inode)
{
  inodes[ino-1] = *inode;
}

PRIVATE void ext3_trim_trailing_slash(char *path)
{
  int len;

  if (path == NULL)
    return;

  len = strlen(path);
  while (len > 1 && path[len - 1] == '/') {
    path[len - 1] = '\0';
    len--;
  }
}

PRIVATE ext3_dentry* ext3_resolve_path(char *pathbuf)
{
  if (pathbuf == NULL || pathbuf[0] == '\0')
    return current != NULL ? current->dentry : rootdir;

  ext3_trim_trailing_slash(pathbuf);
  if (strcmp(pathbuf, "/") == 0)
    return rootdir;
  if (pathbuf[0] == '/')
    return __dir_walk(pathbuf, rootdir);
  if (current == NULL || current->dentry == NULL)
    return __dir_walk(pathbuf, rootdir);
  return __dir_walk(pathbuf, current->dentry);
}

PRIVATE ext3_dentry* ext3_resolve_parent(char *pathbuf, char **name_out)
{
  char *slash;

  if (pathbuf == NULL || name_out == NULL)
    return NULL;

  ext3_trim_trailing_slash(pathbuf);
  slash = strrchr(pathbuf, '/');
  if (slash == NULL) {
    *name_out = pathbuf;
    return current != NULL ? current->dentry : rootdir;
  }
  if (slash == pathbuf) {
    *name_out = slash + 1;
    return rootdir;
  }

  *slash = '\0';
  *name_out = slash + 1;
  return ext3_resolve_path(pathbuf);
}

PRIVATE void ext3_mark_inode_dirty(ext3_dentry *dentry)
{
#ifdef DIRTY
  ext3_inode_dirty* idirty;

  if (dentry == NULL)
    return;

  idirty = kalloc(sizeof(ext3_inode_dirty));
  if (idirty == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return;
  }
  memset((char*)idirty, 0, sizeof(ext3_inode_dirty));
  idirty->ino = dentry->d_inonum;
  idirty->inode = dentry->d_inode;
  dlist_insert_after(&(idirty->list), &(rootdirty->d_inodirty.list));
#else
  (void)dentry;
#endif
}

PRIVATE void ext3_mark_block_dirty(u_int32_t iblock, char *block)
{
#ifdef DIRTY
  ext3_block_dirty* bdirty;

  if (block == NULL)
    return;

  bdirty = kalloc(sizeof(ext3_block_dirty));
  if (bdirty == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return;
  }
  memset((char*)bdirty, 0, sizeof(ext3_block_dirty));
  bdirty->iblock = iblock;
  bdirty->pblock = block;
  dlist_insert_after(&(bdirty->list), &(rootdirty->d_blkdirty.list));
#else
  (void)iblock;
  (void)block;
#endif
}

PRIVATE int ext3_padded_name_len(int len)
{
  if (len % 4 != 0)
    len += 4 - (len % 4);
  return len;
}

PRIVATE void ext3_write_dirent(char *dst, ext3_dentry *dentry, u_int16_t rec_len)
{
  int name_len;

  if (dst == NULL || dentry == NULL)
    return;

  memset(dst, 0, rec_len);
  memcpy(dst, &dentry->d_inonum, 4);
  memcpy(dst + 4, &rec_len, 2);
  dst[6] = dentry->d_namelen;
  dst[7] = dentry->d_filetype;
  name_len = ext3_padded_name_len(dentry->d_namelen);
  memcpy(dst + 8, dentry->d_name, name_len);
}

PRIVATE void ext3_rebuild_dirblock(ext3_dentry *parent)
{
  struct dlist_set *pos;
  int offset = 0;

  if (parent == NULL || parent->d_dirblock == NULL)
    return;

  memset(parent->d_dirblock, 0, BLOCK_SIZE);
  dlist_for_each(pos, &(parent->d_subdirs)) {
    ext3_dentry *child = dlist_entry(pos, ext3_dentry, d_child);
    int rec_len = 8 + ext3_padded_name_len(child->d_namelen);

    if (pos->next == &(parent->d_subdirs))
      rec_len = BLOCK_SIZE - offset;
    ext3_write_dirent(parent->d_dirblock + offset, child, (u_int16_t)rec_len);
    offset += rec_len;
  }
  ext3_mark_block_dirty(parent->d_inode->i_block[0], parent->d_dirblock);
}

PRIVATE u_int32_t* ext3_load_block_table(u_int32_t iblock)
{
  u_int32_t *table;

  if (iblock == 0)
    return NULL;

  table = kalloc(BLOCK_SIZE);
  if (table == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  rawdev.raw_read(iblock * BLOCK_SIZE / FDC_SECTOR_SIZE,
                  BLOCK_SIZE / FDC_SECTOR_SIZE, table);
  return table;
}

PRIVATE int ext3_inode_get_block(ext3_inode *inode, int lblock)
{
  int real_block = 0;

  if (inode == NULL || lblock < 0)
    return 0;

  if (lblock <= BLOCK_ZERO_STAGE)
    return inode->i_block[lblock];
  if (lblock <= BLOCK_FIRST_STAGE) {
    u_int32_t *table;
    int first_stage = inode->i_block[EXT3_IND_BLOCK];

    if (first_stage == 0)
      return 0;
    table = ext3_load_block_table((u_int32_t)first_stage);
    if (table == NULL) {
      return 0;
    }
    real_block = table[lblock - BLOCK_ZERO_STAGE - 1];
    kfree(table);
    return real_block;
  }
  if (lblock <= BLOCK_SECOND_STAGE) {
    u_int32_t *table;
    u_int32_t *leaf;
    u_int32_t leaf_block;
    int table_index;
    int leaf_index;
    int second_stage = inode->i_block[EXT3_DIND_BLOCK];
    int offset = lblock - BLOCK_FIRST_STAGE - 1;

    if (second_stage == 0)
      return 0;
    table = ext3_load_block_table((u_int32_t)second_stage);
    if (table == NULL)
      return 0;
    table_index = offset / EXT3_PTRS_PER_BLOCK;
    leaf_index = offset % EXT3_PTRS_PER_BLOCK;
    leaf_block = table[table_index];
    kfree(table);
    if (leaf_block == 0)
      return 0;
    leaf = ext3_load_block_table(leaf_block);
    if (leaf == NULL)
      return 0;
    real_block = leaf[leaf_index];
    kfree(leaf);
    return real_block;
  }

  _kputs("We don't implement the third stage of i_blocks");
  return 0;
}

PRIVATE int ext3_inode_ensure_block(ext3_dentry *dentry, int lblock)
{
  ext3_inode *inode;

  if (dentry == NULL || dentry->d_inode == NULL)
    return 0;

  inode = dentry->d_inode;
  if (lblock <= BLOCK_ZERO_STAGE) {
    if (inode->i_block[lblock] == 0) {
      inode->i_block[lblock] = __alloc_block();
      ext3_mark_inode_dirty(dentry);
    }
    return inode->i_block[lblock];
  }
  if (lblock <= BLOCK_FIRST_STAGE) {
    u_int32_t *table;
    int entry = lblock - BLOCK_ZERO_STAGE - 1;
    int table_block;
    int table_dirty = FALSE;
    int resolved_block;

    table_block = inode->i_block[EXT3_IND_BLOCK];
    if (table_block == 0) {
      table = kalloc(BLOCK_SIZE);

      if (table == NULL) {
        _kprintf("%s kalloc error\n", __func__);
        return 0;
      }
      memset((char*)table, 0, BLOCK_SIZE);
      table_block = __alloc_block();
      inode->i_block[EXT3_IND_BLOCK] = table_block;
      ext3_mark_inode_dirty(dentry);
      table_dirty = TRUE;
    } else {
      table = ext3_load_block_table((u_int32_t)table_block);
      if (table == NULL)
        return 0;
    }
    if (table[entry] == 0) {
      table[entry] = __alloc_block();
      table_dirty = TRUE;
    }
    resolved_block = table[entry];
    if (table_dirty)
      ext3_mark_block_dirty((u_int32_t)table_block, (char*)table);
    else
      kfree(table);
    if (resolved_block != 0)
      return resolved_block;
  }
  if (lblock <= BLOCK_SECOND_STAGE) {
    u_int32_t *table;
    u_int32_t *leaf;
    u_int32_t leaf_block;
    int offset = lblock - BLOCK_FIRST_STAGE - 1;
    int table_index = offset / EXT3_PTRS_PER_BLOCK;
    int leaf_index = offset % EXT3_PTRS_PER_BLOCK;
    int table_block = inode->i_block[EXT3_DIND_BLOCK];
    int table_dirty = FALSE;
    int leaf_dirty = FALSE;
    int resolved_block;

    if (table_block == 0) {
      table = kalloc(BLOCK_SIZE);
      if (table == NULL) {
        _kprintf("%s kalloc error\n", __func__);
        return 0;
      }
      memset((char*)table, 0, BLOCK_SIZE);
      table_block = __alloc_block();
      inode->i_block[EXT3_DIND_BLOCK] = table_block;
      ext3_mark_inode_dirty(dentry);
      table_dirty = TRUE;
    } else {
      table = ext3_load_block_table((u_int32_t)table_block);
      if (table == NULL)
        return 0;
    }

    leaf_block = table[table_index];
    if (leaf_block == 0) {
      leaf = kalloc(BLOCK_SIZE);
      if (leaf == NULL) {
        _kprintf("%s kalloc error\n", __func__);
        if (table_dirty == FALSE)
          kfree(table);
        return 0;
      }
      memset((char*)leaf, 0, BLOCK_SIZE);
      leaf_block = __alloc_block();
      table[table_index] = leaf_block;
      table_dirty = TRUE;
      leaf_dirty = TRUE;
    } else {
      leaf = ext3_load_block_table(leaf_block);
      if (leaf == NULL) {
        if (table_dirty == FALSE)
          kfree(table);
        return 0;
      }
    }

    if (leaf[leaf_index] == 0) {
      leaf[leaf_index] = __alloc_block();
      leaf_dirty = TRUE;
    }
    resolved_block = leaf[leaf_index];

    if (table_dirty)
      ext3_mark_block_dirty((u_int32_t)table_block, (char*)table);
    else
      kfree(table);
    if (leaf_dirty)
      ext3_mark_block_dirty(leaf_block, (char*)leaf);
    else
      kfree(leaf);
    if (resolved_block != 0)
      return resolved_block;
  }

  _kputs("We don't implement the third stage of i_blocks");
  return 0;
}

PRIVATE void ext3_free_block(u_int32_t block)
{
  if (block == 0)
    return;
  if ((block_bitmap[block / 8] & (1 << (block % 8))) == 0)
    return;

  __clear_bitmap(block_bitmap, block);
  super_block.s_free_blocks_count++;
  group_descs[0].bg_free_blocks_count++;
  rootdirty->d_super = TRUE;
  rootdirty->d_group = TRUE;
  rootdirty->d_blkbmp = TRUE;
}

PRIVATE void ext3_release_inode_blocks(ext3_dentry *dentry)
{
  ext3_inode *inode;
  int i;

  if (dentry == NULL || dentry->d_inode == NULL)
    return;

  inode = dentry->d_inode;
  for (i = 0; i <= BLOCK_ZERO_STAGE; i++) {
    if (inode->i_block[i] != 0) {
      ext3_free_block(inode->i_block[i]);
      inode->i_block[i] = 0;
    }
  }
  if (inode->i_block[EXT3_IND_BLOCK] != 0) {
    u_int32_t *table = kalloc(BLOCK_SIZE);

    if (table == NULL) {
      _kprintf("%s kalloc error\n", __func__);
      return;
    }
    rawdev.raw_read(inode->i_block[EXT3_IND_BLOCK] * BLOCK_SIZE / FDC_SECTOR_SIZE,
                    BLOCK_SIZE / FDC_SECTOR_SIZE, table);
    for (i = 0; i < BLOCK_SIZE / 4; i++) {
      if (table[i] != 0)
        ext3_free_block(table[i]);
    }
    kfree(table);
    ext3_free_block(inode->i_block[EXT3_IND_BLOCK]);
    inode->i_block[EXT3_IND_BLOCK] = 0;
  }
  if (inode->i_block[EXT3_DIND_BLOCK] != 0) {
    u_int32_t *table = ext3_load_block_table(inode->i_block[EXT3_DIND_BLOCK]);

    if (table == NULL) {
      _kprintf("%s kalloc error\n", __func__);
      return;
    }
    for (i = 0; i < EXT3_PTRS_PER_BLOCK; i++) {
      if (table[i] != 0) {
        u_int32_t *leaf = ext3_load_block_table(table[i]);
        int j;

        if (leaf == NULL) {
          _kprintf("%s kalloc error\n", __func__);
          kfree(table);
          return;
        }
        for (j = 0; j < EXT3_PTRS_PER_BLOCK; j++) {
          if (leaf[j] != 0)
            ext3_free_block(leaf[j]);
        }
        kfree(leaf);
        ext3_free_block(table[i]);
      }
    }
    kfree(table);
    ext3_free_block(inode->i_block[EXT3_DIND_BLOCK]);
    inode->i_block[EXT3_DIND_BLOCK] = 0;
  }
  inode->i_size = 0;
  inode->i_blocks = 0;
  ext3_mark_inode_dirty(dentry);
}

PRIVATE int ext3_is_open_dentry(ext3_dentry *target)
{
  struct task_struct *task;

  if (target == NULL || current == NULL)
    return FALSE;

  task = current;
  do {
    int fd;

    if (task->files != NULL) {
      for (fd = 0; fd < FILEDESC_MAX; fd++) {
        struct file *file = task->files->fs_fd[fd];

        if (file != NULL && file->f_dentry == target)
          return TRUE;
      }
    }
    task = dlist_entry(task->run_list.next, struct task_struct, run_list);
  } while (task != current);

  return FALSE;
}

PRIVATE int ext3_is_dir_empty(ext3_dentry *dentry)
{
  struct dlist_set *pos;

  if (dentry == NULL)
    return FALSE;

  dlist_for_each(pos, &(dentry->d_subdirs)) {
    ext3_dentry *child = dlist_entry(pos, ext3_dentry, d_child);

    if (strcmp(child->d_name, ".") != 0 &&
        strcmp(child->d_name, "..") != 0)
      return FALSE;
  }
  return TRUE;
}

PRIVATE void ext3_release_inode(ext3_dentry *dentry)
{
  if (dentry == NULL || dentry->d_inode == NULL)
    return;

  ext3_release_inode_blocks(dentry);
  memset(dentry->d_inode, 0, sizeof(ext3_inode));
  __clear_bitmap(inode_bitmap, dentry->d_inonum);
  super_block.s_free_inodes_count++;
  group_descs[0].bg_free_inodes_count++;
  rootdirty->d_super = TRUE;
  rootdirty->d_group = TRUE;
  rootdirty->d_inobmp = TRUE;
  ext3_mark_inode_dirty(dentry);
}

PRIVATE int ext3_name_equal(const char *name, ext3_dentry *dentry)
{
  int len;

  if (name == NULL || dentry == NULL || dentry->d_name == NULL)
    return FALSE;

  len = strlen(name);
  if (len != dentry->d_namelen)
    return FALSE;
  return memcmp(name, dentry->d_name, len) == 0;
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
  dentry->d_name[0] = '/';
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
      char* blockbuf = kalloc(BLOCK_SIZE);
      if (blockbuf == NULL) {
        _kprintf("%s kalloc error\n", __func__);
        return;
      }
      char* p = blockbuf;
      rawdev.raw_read(BLOCK_SIZE*(inode->i_block[0])/FDC_SECTOR_SIZE,
               BLOCK_SIZE/FDC_SECTOR_SIZE, blockbuf);
      parent->d_dirblock = p;
      u_int16_t len, sum_len = 0;
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
        memset(dentry->d_name, 0, EXT3_NAME_LEN);
        memcpy(dentry->d_name, p+8, dentry->d_namelen);
        dentry->d_name[dentry->d_namelen] = '\0';
        dentry->d_flags = dentry->d_inode->i_flags;
        dentry->d_parent = parent;
        init_dentry_lists(dentry);

        // chain between the parent and childrens
        dlist_insert_after(&(dentry->d_child), &(parent->d_subdirs));
        if (ext3_name_equal(".", dentry) == FALSE &&
            ext3_name_equal("..", dentry) == FALSE) {
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
  ext3_dentry* dentry;
  ext3_inode* inode;
  char* filename;
  char* namebuf = kalloc(PATHNAME_MAX);
  int newino;

  if (namebuf == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset(namebuf, 0, PATHNAME_MAX);
  memcpy(namebuf, (char*)pathname, strlen(pathname));
  parentdir = ext3_resolve_parent(namebuf, &filename);

  if (parentdir == NULL) {
    _kprintf("parentdir is null\n");
    return NULL;
  }
  if (filename == NULL || filename[0] == '\0')
    return NULL;

  // If the file we want to open already exist,
  //  return NULL
  if (__get_dentry(filename, parentdir) != NULL) {
    com1_printf("mkdir: %s already exists\r\n", pathname);
    return NULL;
  }
  
  newino = __alloc_ino();
  if (newino == -1)
    return NULL;

  inode = ext3_get_inode(newino);
  memset(inode, 0, sizeof(ext3_inode));
  inode->i_mode = (flags & O_DIRECTORY ? SODEX_S_IFDIR : SODEX_S_IFREG) |
                  (mode & 0777);
  inode->i_links_count = (flags & O_DIRECTORY) ? 2 : 1;
  inode->i_flags = 0;

  dentry = kalloc(sizeof(ext3_dentry));
  if (dentry == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset((char*)dentry, 0, sizeof(ext3_dentry));
  dentry->d_inode = inode;
  dentry->d_inonum = newino;
  dentry->d_namelen = strlen(filename);
  dentry->d_filetype = (flags & O_DIRECTORY) ? FTYPE_DIR : FTYPE_FILE;
  dentry->d_name = kalloc(EXT3_NAME_LEN);
  if (dentry->d_name == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return NULL;
  }
  memset(dentry->d_name, 0, EXT3_NAME_LEN);
  memcpy(dentry->d_name, filename, strlen(filename));
  dentry->d_flags = flags;
  init_dentry_lists(dentry);

  dentry->d_parent = parentdir;
  dlist_insert_after(&(dentry->d_child), &(parentdir->d_subdirs));
  ext3_mark_inode_dirty(dentry);
  ext3_rebuild_dirblock(parentdir);

  int err = kfree(namebuf);
  if (err)
    _kprintf("%s:kfree erro:%s\n", __func__, err);

  return dentry;
}

PRIVATE ext3_dentry* __get_dentry(const char* filename, ext3_dentry* dentry)
{
  ext3_dentry* pdentry;
  struct dlist_set* p;

  if (filename == NULL || dentry == NULL)
    return NULL;

  dlist_for_each(p, &(dentry->d_subdirs)) {
    pdentry = dlist_entry(p, ext3_dentry, d_child);
    if (ext3_name_equal(filename, pdentry)) {
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

  if (pathname == NULL || search_dentry == NULL)
    return NULL;

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
  ext3_dentry* dentry;
  char* pathbuf = kalloc(PATHNAME_MAX);
  struct file* file;
  int fd;

  if (pathbuf == NULL) {
    _kprintf("%s org_buf kalloc error\n", __func__);
    return FS_OPEN_FAIL;
  }
  memset(pathbuf, 0, PATHNAME_MAX);
  strcpy(pathbuf, pathname);
  ext3_trim_trailing_slash(pathbuf);

  dentry = ext3_resolve_path(pathbuf);

  if (flags & O_CREAT) {
    if (dentry == NULL) {
      dentry = __create_file(pathname, flags, mode);
      if (dentry == NULL) {
        _kprintf("create file:fail\n");
        return FS_OPEN_FAIL;
      }
      ext3_flush();
    }
  } else {
    if (dentry == NULL)
      return FS_OPEN_FAIL;
  }

  if ((flags & O_TRUNC) && dentry->d_filetype == FTYPE_FILE) {
    ext3_release_inode_blocks(dentry);
    ext3_flush();
  }

  file = kalloc(sizeof(struct file));
  if (file == NULL) {
    _kprintf("%s struct file kalloc error\n", __func__);
    return FS_OPEN_FAIL;
  }

  memset((char*)file, 0, sizeof(struct file));
  file->f_mode = mode;
  file->f_flags = flags;
  file->f_dentry = dentry;
  file->f_stdioflag = FLAG_FILE;
  file->f_pos = (flags & O_APPEND) ? dentry->d_inode->i_size : 0;
  file->f_refcount = 1;

  fd = files_alloc_fd(current->files, file);
  kfree(pathbuf);
  if (fd < 0) {
    kfree(file);
    return FS_OPEN_FAIL;
  }

  return fd;
}

PUBLIC ssize_t ext3_read(int fd, void* buf, size_t count)
{
  ext3_dentry* dentry = FD_TODENTRY(fd, current);
  struct file* file = FD_TOFILE(fd, current);
  ext3_inode* inode;
  size_t total = 0;

  if (dentry == NULL || file == NULL || buf == NULL)
    return -1;

  inode = dentry->d_inode;
  if (file->f_pos >= (off_t)inode->i_size)
    return 0;
  if (count > (size_t)(inode->i_size - file->f_pos))
    count = (size_t)(inode->i_size - file->f_pos);

  while (total < count) {
    char *blockbuf;
    off_t pos = file->f_pos + (off_t)total;
    int lblock = pos / BLOCK_SIZE;
    int block_offset = pos % BLOCK_SIZE;
    size_t chunk = BLOCK_SIZE - block_offset;
    int real_block;

    if (chunk > count - total)
      chunk = count - total;
    real_block = ext3_inode_get_block(inode, lblock);
    if (real_block == 0) {
      memset((char*)buf + total, 0, chunk);
      total += chunk;
      continue;
    }

    blockbuf = kalloc(BLOCK_SIZE);
    if (blockbuf == NULL) {
      _kprintf("%s kalloc error\n", __func__);
      break;
    }
    rawdev.raw_read(real_block * BLOCK_SIZE / FDC_SECTOR_SIZE,
                    BLOCK_SIZE / FDC_SECTOR_SIZE, blockbuf);
    memcpy((char*)buf + total, blockbuf + block_offset, chunk);
    kfree(blockbuf);
    total += chunk;
  }

  file->f_pos += total;
  return (ssize_t)total;
}

PUBLIC ssize_t ext3_write(int fd, const void* buf, size_t count)
{
  ext3_dentry* dentry = FD_TODENTRY(fd, current);
  struct file* file = FD_TOFILE(fd, current);
  ext3_inode* inode;
  off_t base_pos;
  size_t total = 0;

  if (dentry == NULL || file == NULL || buf == NULL)
    return -1;

  inode = dentry->d_inode;
  base_pos = (file->f_flags & O_APPEND) ? inode->i_size : file->f_pos;
  while (total < count) {
    char *blockbuf;
    off_t pos = base_pos + (off_t)total;
    int lblock = pos / BLOCK_SIZE;
    int block_offset = pos % BLOCK_SIZE;
    size_t chunk = BLOCK_SIZE - block_offset;
    int real_block;
    int existed;

    if (chunk > count - total)
      chunk = count - total;
    existed = ext3_inode_get_block(inode, lblock);
    real_block = ext3_inode_ensure_block(dentry, lblock);
    if (real_block == 0)
      break;

    blockbuf = kalloc(BLOCK_SIZE);
    if (blockbuf == NULL) {
      _kprintf("%s kalloc error\n", __func__);
      break;
    }
    if (existed != 0) {
      rawdev.raw_read(real_block * BLOCK_SIZE / FDC_SECTOR_SIZE,
                      BLOCK_SIZE / FDC_SECTOR_SIZE, blockbuf);
    } else {
      memset(blockbuf, 0, BLOCK_SIZE);
    }
    memcpy(blockbuf + block_offset, (char*)buf + total, chunk);
    ext3_mark_block_dirty(real_block, blockbuf);
    total += chunk;
  }

  if (base_pos + (off_t)total > (off_t)inode->i_size) {
    inode->i_size = base_pos + (off_t)total;
    inode->i_blocks = (inode->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    ext3_mark_inode_dirty(dentry);
  }
  file->f_pos = base_pos + (off_t)total;
  ext3_flush();
  return (ssize_t)total;
}

PUBLIC int ext3_mkdir(const char* pathname, mode_t mode)
{
  ext3_dentry* dentry = __create_file(pathname, O_CREAT|O_DIRECTORY,
                                      (mode == 0 ? 0755 : mode));
  ext3_dentry* dot;
  ext3_dentry* dotdot;
  int iblock;

  if (dentry == NULL)
    return MKDIR_FAIL;

  iblock = __alloc_block();
  dentry->d_inode->i_block[0] = iblock;
  dentry->d_inode->i_size = BLOCK_SIZE;
  dentry->d_inode->i_blocks = BLOCK_SIZE / IBLOCK_SIZE;
  dentry->d_dirblock = kalloc(BLOCK_SIZE);
  if (dentry->d_dirblock == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return MKDIR_FAIL;
  }
  memset(dentry->d_dirblock, 0, BLOCK_SIZE);

  dot = kalloc(sizeof(ext3_dentry));
  dotdot = kalloc(sizeof(ext3_dentry));
  if (dot == NULL || dotdot == NULL) {
    _kprintf("%s kalloc error\n", __func__);
    return MKDIR_FAIL;
  }
  memset(dot, 0, sizeof(ext3_dentry));
  memset(dotdot, 0, sizeof(ext3_dentry));
  init_dentry_lists(dot);
  init_dentry_lists(dotdot);

  dot->d_inode = dentry->d_inode;
  dot->d_inonum = dentry->d_inonum;
  dot->d_namelen = 1;
  dot->d_filetype = FTYPE_DIR;
  dot->d_name = kalloc(EXT3_NAME_LEN);
  dot->d_parent = dentry;
  memset(dot->d_name, 0, EXT3_NAME_LEN);
  memcpy(dot->d_name, ".", 1);

  dotdot->d_inode = dentry->d_parent->d_inode;
  dotdot->d_inonum = dentry->d_parent->d_inonum;
  dotdot->d_namelen = 2;
  dotdot->d_filetype = FTYPE_DIR;
  dotdot->d_name = kalloc(EXT3_NAME_LEN);
  dotdot->d_parent = dentry;
  memset(dotdot->d_name, 0, EXT3_NAME_LEN);
  memcpy(dotdot->d_name, "..", 2);

  dlist_insert_after(&(dot->d_child), &(dentry->d_subdirs));
  dlist_insert_after(&(dotdot->d_child), &(dentry->d_subdirs));
  ext3_mark_inode_dirty(dentry);
  ext3_rebuild_dirblock(dentry);
  ext3_flush();
  return MKDIR_SUCCESS;
}

PUBLIC int ext3_unlink(const char* pathname)
{
  ext3_dentry* dentry;
  char* pathbuf;

  pathbuf = kalloc(PATHNAME_MAX);
  if (pathbuf == NULL)
    return -1;
  memset(pathbuf, 0, PATHNAME_MAX);
  strcpy(pathbuf, pathname);
  dentry = ext3_resolve_path(pathbuf);
  kfree(pathbuf);
  if (dentry == NULL || dentry->d_parent == NULL || dentry->d_filetype == FTYPE_DIR)
    return -1;
  if (ext3_is_open_dentry(dentry))
    return -1;

  ext3_release_inode(dentry);
  dlist_remove(&(dentry->d_child));
  ext3_rebuild_dirblock(dentry->d_parent);
  ext3_flush();
  return 0;
}

PUBLIC int ext3_rmdir(const char* pathname)
{
  ext3_dentry* dentry;
  char* pathbuf;

  pathbuf = kalloc(PATHNAME_MAX);
  if (pathbuf == NULL)
    return -1;
  memset(pathbuf, 0, PATHNAME_MAX);
  strcpy(pathbuf, pathname);
  dentry = ext3_resolve_path(pathbuf);
  kfree(pathbuf);
  if (dentry == NULL || dentry == rootdir || dentry->d_parent == NULL)
    return -1;
  if (dentry->d_filetype != FTYPE_DIR)
    return -1;
  if (ext3_is_open_dentry(dentry) || ext3_is_dir_empty(dentry) == FALSE)
    return -1;

  ext3_release_inode(dentry);
  dlist_remove(&(dentry->d_child));
  ext3_rebuild_dirblock(dentry->d_parent);
  ext3_flush();
  return 0;
}

PUBLIC int ext3_rename(const char* oldpath, const char* newpath)
{
  ext3_dentry* dentry;
  ext3_dentry* old_parent;
  ext3_dentry* new_parent;
  char* oldbuf;
  char* newbuf;
  char* newname;

  oldbuf = kalloc(PATHNAME_MAX);
  newbuf = kalloc(PATHNAME_MAX);
  if (oldbuf == NULL || newbuf == NULL)
    return -1;
  memset(oldbuf, 0, PATHNAME_MAX);
  memset(newbuf, 0, PATHNAME_MAX);
  strcpy(oldbuf, oldpath);
  strcpy(newbuf, newpath);

  dentry = ext3_resolve_path(oldbuf);
  new_parent = ext3_resolve_parent(newbuf, &newname);
  if (dentry == NULL || new_parent == NULL || newname == NULL || newname[0] == '\0')
    return -1;
  if (__get_dentry(newname, new_parent) != NULL)
    return -1;

  old_parent = dentry->d_parent;
  memset(dentry->d_name, 0, EXT3_NAME_LEN);
  memcpy(dentry->d_name, newname, strlen(newname));
  dentry->d_namelen = strlen(newname);

  if (old_parent != new_parent) {
    dlist_remove(&(dentry->d_child));
    dentry->d_parent = new_parent;
    dlist_insert_after(&(dentry->d_child), &(new_parent->d_subdirs));
    ext3_rebuild_dirblock(old_parent);
  }
  ext3_rebuild_dirblock(new_parent);
  if (dentry->d_filetype == FTYPE_DIR) {
    ext3_dentry* dotdot = __get_dentry("..", dentry);

    if (dotdot != NULL) {
      dotdot->d_inode = new_parent->d_inode;
      dotdot->d_inonum = new_parent->d_inonum;
      ext3_rebuild_dirblock(dentry);
    }
  }
  ext3_flush();
  kfree(oldbuf);
  kfree(newbuf);
  return 0;
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
    int inode_index;
    int sector_index;
    int aligned_inode_index;
    inodirty = dlist_entry(p, ext3_inode_dirty, list);
    inode_index = inodirty->ino - 1;
    sector_index = (inode_index * sizeof(ext3_inode)) / FDC_SECTOR_SIZE;
    aligned_inode_index
      = (sector_index * FDC_SECTOR_SIZE) / sizeof(ext3_inode);
    rawdev.raw_write(P_INODE_BLOCK/FDC_SECTOR_SIZE + sector_index, 1,
              (char*)&inodes[aligned_inode_index]);
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
