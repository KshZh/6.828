// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)
// #define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT*NINDIRECT)

// On-disk inode structure
struct dinode {
  // The type ﬁeld distinguishes between ﬁles, directories, and special ﬁles (devices). XXX A type of zero indicates that an on-disk inode is free.
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  // XXX The nlink ﬁeld counts the number of directory entries that refer to this inode, in order to recognize when the on-disk inode and its data
  // blocks should be freed. 
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  // The addrs array records the block numbers of the disk blocks holding the ﬁle’s content. 
  uint addrs[NDIRECT+1];   // Data block addresses
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
// 一个block包含多个dinode对象，sb.inodestart是磁盘inode区的起始block的no。
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
// 第一个参数是blockno，返回包含该blockno的bitmap block的blockno。
#define BBLOCK(b, sb) (b/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

