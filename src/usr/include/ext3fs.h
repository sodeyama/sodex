#ifndef _EXT3FS_H
#define _EXT3FS_H

#include <sodex/const.h>
#include <sodex/list.h>
#include <sys/types.h>
#include <fs.h>

#define EXT3_N_BLOCKS       15
#define EXT3_NAME_LEN       256
#define BLOCK_SIZE          4096    //4096Byte
#define BLOCK_MAX           360     //360個
#define GROUP_MAX           128     // max number of group desc at 1 BLOCK
#define INODE_SIZE          128     //128Byte
#define INODE_MAX           128     //128個
#define INODE_PER_BLOCK     ((BLOCK_SIZE)/(INODE_SIZE)) //32
#define IBLOCK_SIZE         512

// super block
#define EXT3_SUPER_MAGIC        0xEF53
#define EXT2_VALID_FS           0x0001
#define EXT2_DFL_MAX_MNT_COUNT  20
#define EXT2_ERRORS_CONTINUE    1
#define EXT2_ERRORS_DEFALT      EXT2_ERRORS_CONTINUE
#define EXT2_GOOD_OLD_REV       0
#define EXT2_DFL_CHECKINTERVAL  (86400L * 180L)
#define EXT2_OS_SODEX           0

//sodex inode position
#define SODEX_BAD_INO           1
#define SODEX_ROOT_INO          2
#define SODEX_LOST_FOUND_INO    11
#define SODEX_BOOTM_INO         12
#define SODEX_KERNEL_INO        13
#define SODEX_FIRST_INO         (SODEX_KERNEL_INO+1)


//sodex mode flags
#define SODEX_S_IFMT  00170000                                                 
#define SODEX_S_IFSOCK 0140000                                                 
#define SODEX_S_IFLNK  0120000                                               
#define SODEX_S_IFREG  0100000                                                 
#define SODEX_S_IFBLK  0060000                                                 
#define SODEX_S_IFDIR  0040000                                                 
#define SODEX_S_IFCHR  0020000                                                 
#define SODEX_S_IFIFO  0010000                                                 
#define SODEX_S_ISUID  0004000
#define SODEX_S_ISGID  0002000 
#define SODEX_S_ISVTX  0001000 

// file types
#define FTYPE_UNDEFINE      0
#define FTYPE_FILE          1
#define FTYPE_DIR           2
#define FTYPE_CHARDEV       3
#define FTYPE_BLOCKDEV      4
#define FTYPE_PIPE          5
#define FTYPE_SOCKET        6
#define FTYPE_SYMLNK        7

// position
#define P_SUPER_BLOCK       1024
#define P_GROUP_DESC        4096
#define P_BLOCK_BITMAP      8192    // 4096*2
#define P_INODE_BITMAP      12288   // 4096*3
#define P_INODE_BLOCK       16384   // 4096*4
#define P_DATA_BLOCK        (16384+(4096*(INODE_MAX/INODE_PER_BLOCK))) // 4096*8

// size
#define S_SUPER_BLOCK       1024
#define S_GROUP_DESC        4096    // 1block
#define S_BLOCK_BITMAP      4096    // 1block
#define S_INODE_BITMAP      4096    // 1block
#define S_INODE_BLOCK       (4096*(INODE_MAX/INODE_PER_BLOCK)) // 4block

// buf size
#define BUF_BOOTA           512
#define BUF_BOOTM           2048
#define BUF_KERNEL          307200  //300KB

typedef struct _ext3_super_block {
  u_int32_t s_inodes_count; //inodeの総数
  u_int32_t s_blocks_count; //ブロックの総数
  u_int32_t s_r_blocks_count; //予約ブロック数
  u_int32_t s_free_blocks_count; //空きブロック数
  u_int32_t s_free_inodes_count; //空きinode数
  u_int32_t s_first_data_block; //使用可能な最初のブロック番号
  u_int32_t s_log_block_size; //ブロックサイズ 2^x KB
  u_int32_t s_log_frag_size; //フラグメントサイズ
  u_int32_t s_blocks_per_group; //ブロックグループあたりのブロック数
  u_int32_t s_frags_per_group; //ブロックグループあたりのフラグメント数
  u_int32_t s_inodes_per_group; //ブロックグループあたりのinode数
  u_int32_t s_mtime; //最終マウント時間
  u_int32_t s_wtime; //最終書き込み時間
  u_int16_t s_mnt_count; //Mount count
  u_int16_t s_max_mnt_count; //Maximal mount count 
  u_int16_t s_magic;        /* Magic signature */      
  u_int16_t s_state;        /* File system state */    
  u_int16_t s_errors;       /* Behaviour when detecting errors */
  u_int16_t s_minor_rev_level;  /* minor revision level */
  u_int32_t s_lastcheck;        /* time of last check */
  u_int32_t s_checkinterval;    /* max. time between checks */
  u_int32_t s_creator_os;       /* OS */
  u_int32_t s_rev_level;        /* Revision level */
  u_int16_t s_def_resuid;       /* Default uid for reserved blocks */ 
  u_int16_t s_def_resgid;       /* Default gid for reserved blocks */
  /*                                                                           
   * These fields are for EXT3_DYNAMIC_REV superblocks only.                   
   *                                                                           
   * Note: the difference between the compatible feature set and               
   * the incompatible feature set is that if there is a bit set                
   * in the incompatible feature set that the kernel doesn't                   
   * know about, it should refuse to mount the filesystem.                     
   *                                                                           
   * e2fsck's requirements are more strict; if it doesn't know                 
   * about a feature in either the compatible or incompatible                  
   * feature set, it must abort and not try to meddle with                     
   * things it doesn't understand...                                           
   */
  u_int32_t s_first_ino;
  u_int16_t s_inode_size;
  u_int16_t s_block_group_nr;
  u_int32_t s_feature_compat;
  u_int32_t s_feature_incompat;
  u_int32_t s_feature_ro_compat;
  u_int8_t  s_uuid[16];
  char      s_volume_name[16];
  char      s_last_mounted[64];
  u_int32_t s_algorithm_usage_bitmap;
  u_int8_t  s_prealloc_blocks;
  u_int8_t  s_prealloc_dir_blocks;
  u_int16_t s_reserved_gdt_blocks;

  /*                                                                           
   * Journaling support valid if EXT3_FEATURE_COMPAT_HAS_JOURNAL set.          
   */                                                                          
  u_int8_t  s_journal_uuid[16];
  u_int32_t s_journal_inum;
  u_int32_t s_journal_dev;
  u_int32_t s_last_orphan;
  u_int32_t s_hash_seed[4];
  u_int8_t  s_def_hash_version;
  u_int8_t  s_reserved_char_pad;
  u_int16_t s_reserved_word_pad;
  u_int32_t s_default_mount_opts;
  u_int32_t s_first_meta_bg;
  u_int32_t s_reserved[190]; //padding
} ext3_super_block;

typedef struct _ext3_group_desc {
  u_int32_t bg_block_bitmap;        /* Blocks bitmap block */
  u_int32_t bg_inode_bitmap;        /* Inodes bitmap block */
  u_int32_t bg_inode_table;     /* Inodes table block */
  u_int16_t bg_free_blocks_count;   /* Free blocks count */
  u_int16_t bg_free_inodes_count;   /* Free inodes count */
  u_int16_t bg_used_dirs_count; /* Directories count */
  int16_t   bg_pad;
  u_int32_t bg_reserved[3];
} ext3_group_desc;

typedef struct _ext3_inode {
  u_int16_t  i_mode;     /* File mode */
  u_int16_t  i_uid;      /* Low 16 bits of Owner Uid */
  u_int32_t  i_size;     /* Size in bytes */
  u_int32_t  i_atime;    /* Access time */
  u_int32_t  i_ctime;    /* Creation time */
  u_int32_t  i_mtime;    /* Modification time */
  u_int32_t  i_dtime;    /* Deletion Time */
  u_int16_t  i_gid;      /* Low 16 bits of Group Id */
  u_int16_t  i_links_count;  /* Links count */
  u_int32_t  i_blocks;   /* Blocks count */
  u_int32_t  i_flags;    /* File flags */
  union {
    struct {
      u_int32_t  l_i_reserved1;
    } linux1;
    struct {
      u_int32_t  h_i_translator;
    } hurd1;
    struct {
      u_int32_t  m_i_reserved1;
    } masix1;
  } osd1;             /* OS dependent 1 */
  u_int32_t  i_block[EXT3_N_BLOCKS];/* Pointers to blocks */
  u_int32_t  i_generation;   /* File version (for NFS) */
  u_int32_t  i_file_acl; /* File ACL */
  u_int32_t  i_dir_acl;  /* Directory ACL */
  u_int32_t  i_faddr;    /* Fragment address */
  union {
    struct {
      u_int8_t   l_i_frag;   /* Fragment number */
      u_int8_t   l_i_fsize;  /* Fragment size */
      u_int16_t  i_pad1;
      u_int16_t  l_i_uid_high;   /* these 2 fields    */
      u_int16_t  l_i_gid_high;   /* were reserved2[0] */
      u_int32_t   l_i_reserved2;
    } linux2;
    struct {
      u_int8_t    h_i_frag;   /* Fragment number */
      u_int8_t    h_i_fsize;  /* Fragment size */
      u_int16_t   h_i_mode_high;
      u_int16_t   h_i_uid_high;
      u_int16_t   h_i_gid_high;
      u_int32_t   h_i_author;
    } hurd2;
    struct {
      u_int8_t    m_i_frag;   /* Fragment number */
      u_int8_t    m_i_fsize;  /* Fragment size */
      u_int16_t   m_pad1;
      u_int32_t   m_i_reserved2[2];
    } masix2;
  } osd2;             /* OS dependent 2 */
} ext3_inode;

struct _ext3_dentry {
  ext3_inode*           d_inode;
  u_int32_t             d_inonum;
  u_int16_t             d_reclen;
  u_int8_t              d_namelen;
  u_int8_t              d_filetype;
  char*                 d_name;
  struct dlist_set      d_child;
  struct dlist_set      d_subdirs;
  struct dlist_set      d_alias;
  u_int32_t             d_flags;
  struct _ext3_dentry*  d_parent;
  char*					d_dirblock;
  void*					d_elfhdr;
  void**				d_elfscthdr;
  void**				d_elfprghdr;
};
typedef struct _ext3_dentry ext3_dentry;

typedef struct _ext3_inode_dirty {
  u_int32_t ino;
  ext3_inode* inode;
  struct dlist_set list;
} ext3_inode_dirty;

typedef struct _ext3_block_dirty {
  u_int32_t iblock;
  char* pblock;
  struct dlist_set list;
} ext3_block_dirty;

typedef struct _ext3_dirty {
  int d_super;
  int d_group;
  int d_inobmp;
  int d_blkbmp;
  ext3_inode_dirty d_inodirty;
  ext3_block_dirty d_blkdirty;
} ext3_dirty;

#define DIRTY


#define BLOCK_PER_SECTOR 8  // BLOCK_SIZE/FDC_SECTOR_SIZE

#define BLOCK_ZERO_STAGE 11
#define BLOCK_FIRST_STAGE (11+BLOCK_SIZE/4)
#define BLOCK_SECOND_STAGE (11+BLOCK_SIZE/4+(BLOCK_SIZE/4)*(BLOCK_SIZE/4))
#define BLOCK_THIRD_STAGE (11+BLOCK_SIZE/4+(BLOCK_SIZE/4)*(BLOCK_SIZE/4) \
                           +(BLOCK_SIZE/4)*(BLOCK_SIZE/4)*(BLOCK_SIZE/4))

#endif /* _EXT3FS_H */
