#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
using namespace std;

#define INIT_TEST

#define EXT3_N_BLOCKS       15
#define EXT3_NAME_LEN       256
#define BLOCK_SIZE          4096    //4096Byte
#define BLOCK_MAX           360     //360個
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
#define SODEX_INIT_INO          14
#define SODEX_INIT2_INO         15
#define SODEX_FIRST_INO         (SODEX_INIT2_INO+1)

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
#define S_GROUP_DESC        4096    // 1block
#define S_BLOCK_BITMAP      4096    // 1block
#define S_INODE_BITMAP      4096    // 1block
#define S_INODE             (4096*(INODE_MAX/INODE_PER_BLOCK)) // 4block

// buf size
#define BUF_BOOTA           512
#define BUF_BOOTM           2048
#define BUF_KERNEL          512000 //500KB
#define BUF_INIT            16384

struct ext3_super_block {
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
};

struct ext3_group_desc {
  u_int32_t bg_block_bitmap;        /* Blocks bitmap block */
  u_int32_t bg_inode_bitmap;        /* Inodes bitmap block */
  u_int32_t bg_inode_table;     /* Inodes table block */
  u_int16_t bg_free_blocks_count;   /* Free blocks count */
  u_int16_t bg_free_inodes_count;   /* Free inodes count */
  u_int16_t bg_used_dirs_count; /* Directories count */
  int16_t   bg_pad;
  u_int16_t bg_reserved[3];
};

struct ext3_inode {
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
};

struct ext3_direntry {
  u_int32_t inode;
  u_int16_t rec_len;
  u_int8_t  name_len;
  u_int8_t  file_type;
  char      name[EXT3_NAME_LEN];
};

void set_bitmap(u_int8_t* bitmap, u_int32_t bit);
void read_dir(fstream& kernel_iofs, ext3_inode* inode, ext3_super_block* sb,
              ext3_group_desc* gd, u_int8_t* inode_bitmap,
              u_int8_t* block_bitmap, int parent_ino, string path);

int first_data_block = 8;

int current_inode = 0;
int rootdir_inode;

void error(char* str)
{
  cerr << str << endl;
  exit(1);
}

void set_boot_block(fstream& ofs, char *boot) {
  ofs.seekp(0, ios::beg);
  ofs.write(boot, BUF_BOOTA);
  ofs.flush();
}

void set_super_block(fstream& ofs, ext3_super_block* sb)
{
  memset(sb, 0, sizeof(ext3_super_block));

  sb->s_magic = EXT3_SUPER_MAGIC;
  sb->s_state = EXT2_VALID_FS;

  sb->s_inodes_count = INODE_MAX;
  sb->s_blocks_count = BLOCK_MAX;
  sb->s_r_blocks_count = BLOCK_MAX - 8;
  sb->s_max_mnt_count = EXT2_DFL_MAX_MNT_COUNT;
  sb->s_errors = EXT2_ERRORS_DEFALT;
  sb->s_free_blocks_count = 
    BLOCK_MAX - INODE_MAX/INODE_PER_BLOCK - 4; //360 - 4  - 4 = 352
  sb->s_free_inodes_count = INODE_MAX;
  sb->s_first_data_block = 1;

  sb->s_first_ino = SODEX_FIRST_INO;

  sb->s_log_block_size = 2; // 4KB
  sb->s_log_frag_size = 2; // 4KB

  sb->s_blocks_per_group = BLOCK_MAX;
  sb->s_inodes_per_group = INODE_MAX;

  sb->s_feature_compat = 0;
  sb->s_feature_incompat = 0;
  sb->s_feature_ro_compat = 0;
  sb->s_first_meta_bg = 0;

  sb->s_rev_level = EXT2_GOOD_OLD_REV;
  sb->s_checkinterval = EXT2_DFL_CHECKINTERVAL;

  sb->s_creator_os = EXT2_OS_SODEX;


  ofs.seekp(P_SUPER_BLOCK, ios::beg);
  ofs.write((char*)sb, sizeof(ext3_super_block));
  if (!ofs.good())
    error("super block write error");
  ofs.flush();
}

void set_group_block(fstream& ofs, ext3_group_desc* gd)
{
  char* empty = new char[BLOCK_SIZE];
  memset(empty, 0, BLOCK_SIZE);
  ofs.seekp(P_GROUP_DESC, ios::beg);
  ofs.write((char*)empty, BLOCK_SIZE);
  ofs.flush();

  memset(gd, 0, sizeof(ext3_group_desc));
  gd->bg_block_bitmap = 2;
  gd->bg_inode_bitmap = 3;
  gd->bg_inode_table = 4;
  gd->bg_free_blocks_count = 346;
  gd->bg_free_inodes_count = 125;
  gd->bg_used_dirs_count = 1;

  ofs.seekp(P_GROUP_DESC, ios::beg);
  ofs.write((char*)gd, sizeof(ext3_group_desc));
  ofs.flush();
}

void set_init_bitmap(u_int8_t* inode_bitmap, u_int8_t* block_bitmap)
{
  set_bitmap(inode_bitmap, 0);
  set_bitmap(inode_bitmap, 1);
  set_bitmap(inode_bitmap, 3);
  set_bitmap(inode_bitmap, 4);
  set_bitmap(inode_bitmap, 5);
  set_bitmap(inode_bitmap, 6);
  set_bitmap(inode_bitmap, 7);
  set_bitmap(inode_bitmap, 8);
  set_bitmap(inode_bitmap, 9);
  set_bitmap(inode_bitmap, 10);

  set_bitmap(block_bitmap, 0);
  set_bitmap(block_bitmap, 1);
  set_bitmap(block_bitmap, 2);
  set_bitmap(block_bitmap, 3);
  set_bitmap(block_bitmap, 4);
  set_bitmap(block_bitmap, 5);
  set_bitmap(block_bitmap, 6);
  set_bitmap(block_bitmap, 7);
}

u_int32_t alloc_inode(ext3_super_block* sb, ext3_group_desc* gd)
{
  u_int32_t ret_ino = sb->s_first_ino++;
  sb->s_free_inodes_count--;
  gd->bg_free_inodes_count--;

  return ret_ino;
}

u_int32_t alloc_inode_block(ext3_super_block* sb, ext3_group_desc* gd)
{
  u_int32_t ret_inoblock = first_data_block++;
  sb->s_free_blocks_count--;
  gd->bg_free_blocks_count--;

  return ret_inoblock;
}

void set_bitmap(u_int8_t* bitmap, u_int32_t bit)
{
  bitmap[bit/8] |=  (1<<bit%8);
}

void set_inode_block(fstream& ofs, ext3_inode* inode)
{
  ofs.seekp(P_INODE_BLOCK, ios::beg);
  ofs.write((char*)inode, S_INODE);
  ofs.flush();
}

void set_block_bitmap(fstream& ofs, u_int8_t* block_bitmap, u_int32_t size)
{
  ofs.seekp(P_BLOCK_BITMAP, ios::beg);
  ofs.write((char*)block_bitmap, size);
  ofs.flush();
}

void set_inode_bitmap(fstream& ofs, u_int8_t* ino_bitmap, u_int32_t size)
{
  ofs.seekp(P_INODE_BITMAP, ios::beg);
  ofs.write((char*)ino_bitmap, size);
  ofs.flush();
}

int create_file(fstream& ofs, ext3_inode* inode, ext3_super_block* sb,
                ext3_group_desc* gd, u_int8_t* ino_bitmap,
                u_int8_t* block_bitmap, char *filename,
                u_int8_t* data, u_int32_t data_size, u_int32_t newino)
{
  if (newino > INODE_MAX)
    error("inode alloc error\n");

  //ext3_inode* ino = (ext3_inode*)(inode + newino - 1);
  ext3_inode* ino = (ext3_inode*)(inode + newino - 1);

  memset((char*)ino, 0, INODE_SIZE);
  ino->i_mode = SODEX_S_IFREG | 0600;
  ino->i_size = data_size;
  ino->i_links_count = 1;
  ino->i_blocks = (u_int32_t)ceil(((double)data_size/BLOCK_SIZE));
  cout << filename << "'s i_blocks is " << ino->i_blocks << endl;
  if (ino->i_blocks > BLOCK_MAX)
    error("we can't carete file on file system because of too large file"
          "which we can't support");

  u_int32_t first_stage = 11+BLOCK_SIZE/4;
  u_int32_t second_stage = 11+BLOCK_SIZE/4+(BLOCK_SIZE/4)*(BLOCK_SIZE/4);
  u_int32_t third_stage = 11+BLOCK_SIZE/4+(BLOCK_SIZE/4)*(BLOCK_SIZE/4)
    +(BLOCK_SIZE/4)*(BLOCK_SIZE/4)*(BLOCK_SIZE/4);

  if (ino->i_blocks <= 11) {
    int i;
    for (i=0; i<ino->i_blocks; i++) {
      int iblock = alloc_inode_block(sb, gd);
      ino->i_block[i] = iblock;
      ofs.seekp(iblock*BLOCK_SIZE, ios::beg);
      ofs.write((char*)(data+i*BLOCK_SIZE), BLOCK_SIZE);
      ofs.flush();
      set_bitmap(block_bitmap, iblock);
    }

  } else if (ino->i_blocks <= first_stage) {
    int i;
    for (i=0; i<=11; i++) {
      int iblock = alloc_inode_block(sb, gd);
      ino->i_block[i] = iblock;
      ofs.seekp(iblock*BLOCK_SIZE);
      ofs.write((char*)(data+i*BLOCK_SIZE), BLOCK_SIZE);
      ofs.flush();
      set_bitmap(block_bitmap, iblock);
    }

    u_int32_t first_blocks[BLOCK_SIZE/4]; // 1024
    memset((char*)first_blocks, 0, BLOCK_SIZE);

    for (i=12; i<=ino->i_blocks; i++) {
      int iblock = alloc_inode_block(sb, gd);
      first_blocks[i-12] = iblock;
      ofs.seekp(iblock*BLOCK_SIZE, ios::beg);
      ofs.write((char*)(data+i*BLOCK_SIZE), BLOCK_SIZE);
      ofs.flush();
      set_bitmap(block_bitmap, iblock);
    }

    int first_stage_block = alloc_inode_block(sb, gd);
    ofs.seekp(first_stage_block*BLOCK_SIZE, ios::beg);
    ofs.write((char*)first_blocks, BLOCK_SIZE);
    ofs.flush();
    ino->i_block[12] = first_stage_block;

  } else if (ino->i_blocks <= second_stage) {
    error("We don't implement the second stage of i_blocks");
  } else if (ino->i_blocks <= third_stage) {
    error("We don't implement the third stage of i_blocks");
  } else {
    error("The size of data is too large blocks.");
  }

  set_bitmap(ino_bitmap, newino);

  return newino;
}

int insert_dir_data(fstream& ofs, ext3_inode* inode, u_int32_t n_dirinode,
                    u_int32_t n_fileinode, u_int8_t file_type, char *name)
{
  u_int16_t rec_len;
  u_int8_t  pure_name_len, name_len;

  name_len = strlen(name);
  pure_name_len = name_len;
  if (name_len%4 != 0)
    name_len += 4 - name_len%4;
  
  char* name_buf;
  try {
    name_buf = new char[name_len];
  } catch (bad_alloc &e) {
    cerr << "memory new error" << endl;
    cerr << "e = " << e.what() << endl;
    exit(1);
  }
  
  memset(name_buf, 0, name_len);
  memcpy(name_buf, name, strlen(name));

  rec_len = 8 + name_len;

  ext3_inode* dino = (ext3_inode*)(inode + n_dirinode - 1);
  ext3_inode* fino = (ext3_inode*)(inode + n_fileinode - 1);

  int dir_iblock = dino->i_block[0];
  char buf[BLOCK_SIZE];
  ofs.seekp(dir_iblock*BLOCK_SIZE, ios::beg);
  ofs.read((char*)buf, BLOCK_SIZE);

  // rec_len check and search null point
  bool first_flag = false;
  u_int16_t sum_len = 0;
  char *p = buf;
  char *old_p = p;
  u_int8_t  old_name_len = (u_int8_t)(p[6]);
  while (true) {
    u_int16_t len = (u_int16_t)((u_int16_t*)(p+4))[0];
    //if (p[4] == 0 && p[5] == 0)
    if (len == 0) {
      first_flag = true;
      break;
    } else {
      old_p = p;
      old_name_len = (u_int8_t)(p[6]);
      if (old_name_len%4 != 0)
        old_name_len += 4 - old_name_len%4;
      if (len == BLOCK_SIZE - sum_len)
        break;
      p += len;
      sum_len += len;
    }
  }

  if (!first_flag) {
    u_int16_t old_rec_len = old_name_len + 8;
    p += old_rec_len;
    sum_len += old_rec_len;

    rec_len = BLOCK_SIZE - sum_len;
    memcpy(old_p+4, &old_rec_len, 2);
  }

  rec_len = BLOCK_SIZE - sum_len;

  memcpy(p, &n_fileinode, 4);
  memcpy(p+4, &rec_len, 2);
  memcpy(p+6, &pure_name_len, 1);
  memcpy(p+7, &file_type, 1);
  memcpy(p+8, name_buf, name_len);

  ofs.seekp(dir_iblock*BLOCK_SIZE, ios::beg);
  ofs.write((char*)buf, BLOCK_SIZE);
  ofs.flush();

  return rec_len;
}

int create_root_dir(fstream& ofs, ext3_inode* inode, ext3_super_block* sb,
                    ext3_group_desc* gd, u_int8_t* ino_bitmap,
                    u_int8_t* block_bitmap)
{
  u_int32_t root_ino = SODEX_ROOT_INO;
  if (root_ino > INODE_MAX)
    error("inode alloc error\n");

  rootdir_inode = root_ino;

  ext3_inode* ino = (ext3_inode*)(inode + root_ino - 1);

  memset((char*)ino, 0, INODE_SIZE);
  ino->i_mode = SODEX_S_IFDIR | 0755;
  ino->i_size = BLOCK_SIZE;    // 4096
  ino->i_links_count = 1;
  ino->i_blocks = (BLOCK_SIZE/IBLOCK_SIZE); // 8

  int iblock = alloc_inode_block(sb, gd);
  ino->i_block[0] = iblock;


  // set '0' to dir block
  u_int8_t dir_data_block[BLOCK_SIZE];
  memset(dir_data_block, 0, BLOCK_SIZE);
  ofs.seekp(iblock*BLOCK_SIZE, ios::beg);
  ofs.write((char*)(dir_data_block), BLOCK_SIZE);
  ofs.flush();

  u_int32_t dotino = root_ino;
  u_int32_t dot2ino = root_ino;
  u_int32_t lf_ino = SODEX_LOST_FOUND_INO;
  insert_dir_data(ofs, inode, root_ino, dotino, FTYPE_DIR, ".");
  insert_dir_data(ofs, inode, root_ino, dot2ino, FTYPE_DIR, "..");
  insert_dir_data(ofs, inode, root_ino, lf_ino, FTYPE_DIR, "lost+found");
 
  set_bitmap(ino_bitmap, root_ino);

  return root_ino;
}

int create_lostfound_dir(fstream& ofs, ext3_inode* inode, ext3_super_block* sb,
                         ext3_group_desc* gd, u_int8_t* ino_bitmap,
                         u_int8_t* block_bitmap)
{
  u_int32_t dir_ino = SODEX_LOST_FOUND_INO;
  if (dir_ino > INODE_MAX)
    error("inode alloc error\n");

  ext3_inode* ino = (ext3_inode*)(inode + dir_ino - 1);

  memset((char*)ino, 0, INODE_SIZE);
  ino->i_mode = SODEX_S_IFDIR | 0755;
  ino->i_size = BLOCK_SIZE;    // 4096
  ino->i_links_count = 2;
  ino->i_blocks = (BLOCK_SIZE/IBLOCK_SIZE); // 8

  int iblock = alloc_inode_block(sb, gd);
  ino->i_block[0] = iblock;


  // set '0' to dir block
  u_int8_t dir_data_block[BLOCK_SIZE];
  memset(dir_data_block, 0, BLOCK_SIZE);
  ofs.seekp(iblock*BLOCK_SIZE, ios::beg);
  ofs.write((char*)(dir_data_block), BLOCK_SIZE);
  ofs.flush();

  u_int32_t dotino = dir_ino;
  u_int32_t dot2ino = SODEX_ROOT_INO;
  insert_dir_data(ofs, inode, dir_ino, dotino, FTYPE_DIR, ".");
  insert_dir_data(ofs, inode, dir_ino, dot2ino, FTYPE_DIR, "..");
 
  set_bitmap(ino_bitmap, dir_ino);

  return dir_ino;
}

int create_dir(fstream& ofs, ext3_inode* inode, ext3_super_block* sb,
               ext3_group_desc* gd, u_int8_t* ino_bitmap,
               u_int8_t* block_bitmap)
{
  u_int32_t dir_ino = alloc_inode(sb, gd);
  cout << "alloc_inode:" << dir_ino << endl;
  if (dir_ino > INODE_MAX)
    error("inode alloc error\n");

  ext3_inode* ino = (ext3_inode*)(inode + dir_ino - 1);

  memset((char*)ino, 0, INODE_SIZE);
  ino->i_mode = SODEX_S_IFDIR | 0755;
  ino->i_size = BLOCK_SIZE;    // 4096
  ino->i_links_count = 2;
  ino->i_blocks = (BLOCK_SIZE/IBLOCK_SIZE); // 8

  int iblock = alloc_inode_block(sb, gd);
  ino->i_block[0] = iblock;


  // set '0' to dir block
  u_int8_t dir_data_block[BLOCK_SIZE];
  memset(dir_data_block, 0, BLOCK_SIZE);
  ofs.seekp(iblock*BLOCK_SIZE, ios::beg);
  ofs.write((char*)(dir_data_block), BLOCK_SIZE);
  ofs.flush();

  u_int32_t dotino = dir_ino;
  u_int32_t dot2ino = SODEX_ROOT_INO;
  insert_dir_data(ofs, inode, dir_ino, dotino, FTYPE_DIR, ".");
  insert_dir_data(ofs, inode, dir_ino, dot2ino, FTYPE_DIR, "..");
 
  set_bitmap(ino_bitmap, dir_ino);

  return dir_ino;
}


int main(int argc, char **argv)
{
  if (argc < 6) {
    cerr << "Usage: kmkfs boota.bin bootm.bin kernel.bin fsboot.bin"
	     << " init init2"
         << endl;
    exit(1);
  }

  ext3_super_block sb;
  ext3_group_desc gd;
  ext3_inode inode[INODE_MAX];
  u_int8_t inode_bitmap[BLOCK_SIZE];
  u_int8_t block_bitmap[BLOCK_SIZE];
  u_int8_t boota[BUF_BOOTA];
  u_int8_t bootm[BUF_BOOTM];
  u_int8_t kernel[BUF_KERNEL];
  u_int8_t init[BUF_INIT];
  u_int8_t init2[BUF_INIT];
  u_int8_t fskernel[BUF_KERNEL];

  string boota_filename = argv[1];
  string bootm_filename = argv[2];
  string kernel_filename = argv[3];
  string fskernel_filename = argv[4];
#ifdef INIT_TEST
  string init_filename = argv[5];
  string init2_filename = argv[6];
#endif
  cout << init_filename << endl;

  /*
   * open bootacient binary file and read it its buffer
   */
  ifstream boota_ifs(boota_filename.c_str(), ios::in|ios::binary);
  if (!boota_ifs)
    error("boota.bin file open error");

  int boota_size;
  boota_ifs.read((char*)boota, BUF_BOOTA);
  boota_size = boota_ifs.gcount();
  cout << "The size of boota file is " << boota_size << endl;

  /*
   * open bootmiddle binary file and read it to its buffer.
   */
  ifstream bootm_ifs(bootm_filename.c_str(), ios::in|ios::binary);
  if (!bootm_ifs)
    error("bootm.bin file open error");

  int bootm_size;
  bootm_ifs.read((char*)bootm, BUF_BOOTM);
  bootm_size = bootm_ifs.gcount();
  cout << "The size of bootm file is " << bootm_size << endl;

  /*
   * open kernel binary file and read it to its buffer.
   */
  ifstream kernel_ifs(kernel_filename.c_str(), ios::in|ios::binary);
  if (!kernel_ifs)
    error("kernel.bin file open error");

  int kernel_size;
  kernel_ifs.read((char*)kernel, BUF_KERNEL);
  kernel_size = kernel_ifs.gcount();
  cout << "The size of kernel file is " << kernel_size << endl;

  /*
   * open init binary file and read it to its buffer.
   */
#ifdef INIT_TEST
  ifstream init_ifs(init_filename.c_str(), ios::in|ios::binary);
  if (!init_ifs)
    error("init file open error");

  int init_size;
  init_ifs.read((char*)init, BUF_INIT);
  init_size = init_ifs.gcount();
  cout << "The size of init file is " << init_size << endl;

  ifstream init2_ifs(init2_filename.c_str(), ios::in|ios::binary);
  if (!init2_ifs)
    error("init2 file open error");

  int init2_size;
  init2_ifs.read((char*)init2, BUF_INIT);
  init2_size = init2_ifs.gcount();
  cout << "The size of init2 file is " << init2_size << endl;
#endif

  fstream kernel_iofs(fskernel_filename.c_str(), ios::in|ios::out|ios::binary);

  set_boot_block(kernel_iofs, (char*)boota);

  /*
   * set parameter to super block and group descriptor
   * and set blocks to kernel_ofs set_super_block(&sb);
   */
  set_super_block(kernel_iofs, &sb);
  set_group_block(kernel_iofs, &gd);

  set_init_bitmap(inode_bitmap, block_bitmap);

  // create inode and block, and set data blocks to kernel_ofs
  int bootm_inode, kernel_inode, root_inode, lostfound_inode;
  int init_inode, init2_inode, init3_inode;
  bootm_inode = create_file(kernel_iofs, (ext3_inode*)&inode, &sb, &gd, 
                            (u_int8_t*)&inode_bitmap, (u_int8_t*)&block_bitmap,
                            "bootm.bin", bootm, bootm_size, SODEX_BOOTM_INO);
  kernel_inode = create_file(kernel_iofs, (ext3_inode*)&inode, &sb, &gd,
                             (u_int8_t*)&inode_bitmap,(u_int8_t*)&block_bitmap,
                             "kernel.bin", kernel, kernel_size,
                             SODEX_KERNEL_INO);
#ifdef INIT_TEST
  init_inode = create_file(kernel_iofs, (ext3_inode*)&inode, &sb, &gd,
                           (u_int8_t*)&inode_bitmap,(u_int8_t*)&block_bitmap,
                           "ptest", init, init_size,
                           SODEX_INIT_INO);
  init2_inode = create_file(kernel_iofs, (ext3_inode*)&inode, &sb, &gd,
                           (u_int8_t*)&inode_bitmap,(u_int8_t*)&block_bitmap,
                           "ptest2", init2, init2_size,
                           SODEX_INIT2_INO);
#endif
  root_inode = create_root_dir(kernel_iofs, (ext3_inode*)&inode, &sb, &gd,
                      (u_int8_t*)&inode_bitmap, (u_int8_t*)&block_bitmap);

  lostfound_inode = create_lostfound_dir(kernel_iofs, (ext3_inode*)&inode,
            &sb, &gd, (u_int8_t*)&inode_bitmap, (u_int8_t*)&block_bitmap);


  insert_dir_data(kernel_iofs, (ext3_inode*)inode, root_inode, bootm_inode,
                  FTYPE_FILE, "bootm.bin");
  insert_dir_data(kernel_iofs, (ext3_inode*)inode, root_inode, kernel_inode,
                  FTYPE_FILE, "kernel.bin");

#ifdef INIT_TEST
  insert_dir_data(kernel_iofs, (ext3_inode*)inode, root_inode, init_inode,
                  FTYPE_FILE, "ptest");
  insert_dir_data(kernel_iofs, (ext3_inode*)inode, root_inode, init2_inode,
                  FTYPE_FILE, "ptest2");
#endif

  /* make usr directory */
  int usr_inode, usr_bin_inode;
  usr_inode = create_dir(kernel_iofs, (ext3_inode*)&inode, &sb, &gd,
                         (u_int8_t*)&inode_bitmap, (u_int8_t*)&block_bitmap);
  usr_bin_inode = create_dir(kernel_iofs, (ext3_inode*)&inode, &sb, &gd,
                         (u_int8_t*)&inode_bitmap, (u_int8_t*)&block_bitmap);
  insert_dir_data(kernel_iofs, (ext3_inode*)inode, root_inode, usr_inode,
                  FTYPE_DIR, "usr");
  insert_dir_data(kernel_iofs, (ext3_inode*)inode, usr_inode, usr_bin_inode,
                  FTYPE_DIR, "bin");
  string path_usrbin = PATH_USRBIN;
  read_dir(kernel_iofs, inode, &sb, &gd, (u_int8_t*)&inode_bitmap,
           (u_int8_t*)&block_bitmap, usr_bin_inode, path_usrbin);

  // set inode blocks to kernel_ofs
  set_inode_block(kernel_iofs, (ext3_inode*)inode);

  // set bitmap blocks to kernel_ofs
  set_block_bitmap(kernel_iofs, block_bitmap, S_BLOCK_BITMAP);
  set_inode_bitmap(kernel_iofs, inode_bitmap, S_INODE_BITMAP);
}

void read_dir(fstream& kernel_iofs, ext3_inode* inode, ext3_super_block* sb,
              ext3_group_desc* gd, u_int8_t* inode_bitmap,
              u_int8_t* block_bitmap, int parent_ino, string path)
{
  DIR *dir = opendir(path.c_str());
  struct stat statbuf;
  string dir_path = path, file_path;
  struct dirent *dent;
  u_int8_t buf[BUF_INIT];

  if (dir) {
    while (dent = readdir(dir)) {
      memset(buf, 0, BUF_INIT);
      if ( strcmp(dent->d_name, ".") == 0 ||
           strcmp(dent->d_name, "..") == 0)
        continue;
      file_path = path + dent->d_name;
      ifstream buf_ifs(file_path.c_str(), ios::in|ios::binary);
      if (!buf_ifs)
        error("file open error");

      lstat(file_path.c_str(), &statbuf);
      int file_size;
      stat(file_path.c_str(), &statbuf);
      file_size = statbuf.st_size;
      buf_ifs.read((char*)buf, BUF_INIT);
      cout << "The size of this " << dent->d_name << " file is "
           << file_size << endl;

      int file_ino;
      if (S_ISDIR(statbuf.st_mode)) {
        file_ino = create_dir(kernel_iofs, inode, sb, gd,
                              inode_bitmap, block_bitmap);
        insert_dir_data(kernel_iofs, inode, parent_ino,
                        file_ino, FTYPE_DIR, dent->d_name);
        read_dir(kernel_iofs, inode, sb, gd, inode_bitmap, block_bitmap,
                 file_ino, file_path + "/");
      } else if (S_ISREG(statbuf.st_mode)) {
        int newino = alloc_inode(sb, gd);
        file_ino = create_file(kernel_iofs, inode, sb, gd,
                               inode_bitmap, block_bitmap,
                               dent->d_name, buf, file_size, newino);
        insert_dir_data(kernel_iofs, inode, parent_ino,
                        file_ino, FTYPE_FILE, dent->d_name);
      } else {
        cout << "This file is not file and directory" << endl;
      }
    }
  } else {
    cout << "Failed to open directory '" << path << "'" << endl;
  }
  closedir(dir);
}
