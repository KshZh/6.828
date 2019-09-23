struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe;
  struct inode *ip;
  uint off; // XXX 每一个文件描述符/file对象有自己的offset。
};


// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number（其实也就是该inode所处的block的no？）
  // The ref ﬁeld counts the number of C pointers referring to the in-memory inode,
  // and the kernel discards the inode from memory if the reference count drops to zero. 
  // （比如open同一个文件多次，就有多个指针指向同一个inode对象？open/iget递增ref，close/iput递减ref。）
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?
  // 下面的字段与dinode一样。
  short type;         // copy of disk inode
  short major;
  short minor;
  // XXX nlink counts the number of directory entries that refer to a ﬁle; xv6 won’t free an inode if its link count is greater than zero. 
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

// table mapping major device number to
// device functions
struct devsw {
  int (*read)(struct inode*, char*, int);
  int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
