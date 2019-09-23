// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock; // XXX bcache.lock互斥对bcache数据结构的访问。
  struct buf buf[NBUF]; // XXX 在内存中最多缓存NBUF个block。

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head; // 这并不是一个指针，这是哨兵结点。
} bcache; // 全局变量，默认初始化为0。

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
  // Create linked list of buffers
  // 这是一个循环双向链表。
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    // 头插法
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 要修改多进程（多CPU）共享的bcache数据结构，要先上锁，以便互斥对bcache的访问和修改。
  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    // 设备号和块号相同，即为同一个块，即该块已缓存在内存中了。
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock); // 修改完了bcache，解锁。
      // It is safe for bget to acquire the buﬀer’s sleep-lock outside of the bcache.lock critical section,
      // since the non-zero b->refcnt prevents the buﬀer from being re-used for a diﬀerent disk block. 
      // The sleep-lock protects reads and writes of the block’s bufered content,
      // while the bcache.lock protects information about which blocks are cached. 
      acquiresleep(&b->lock); // 获取这个buf的sleeplock，使得只有一个进程可以读写这个buf。
      return b;
    }
  }

  // Not cached; recycle an unused buffer.
  // Even if refcnt==0, B_DIRTY indicates a buffer is in use
  // because log.c has modified it but not yet committed it.
  // 从后往前扫描，这是双向LRU cache链表的使用约定，最近free的buf会插入头部，所以表尾的buf最可能是最少使用的，也就最可能是free的。
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0; // B_VALID=0，通知caller从disk读出内容。
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // A more graceful response might be to sleep until a
  // bufer became free, though there would then be a possibility of deadlock.
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if((b->flags & B_VALID) == 0) {
    // 缓存miss，从disk读出。
    iderw(b);
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  // 这个buf的使用者必须持有该buf的sleeplock，进行互斥访问。
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  // XXX disk层的iderw接口的调用约定就是，如果要将传入的buf写入磁盘，就将其flags中的B_DIRTY置位，
  // 这样iderw调用的idestart就会对ide控制器发出写请求将传入的buf写入磁盘。否则，iderw将从磁盘中读出buf.blockno块到buf中。
  b->flags |= B_DIRTY;
  iderw(b); // 写回disk
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock); // 释放buf的sleeplock

  // 要读写bcache，得先获取bcache.lock，以互斥对bcache的读写。
  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // 插入头部
    // Moving the bufer causes the list to be ordered by how recently the bufers were used (meaning released):
    // the ﬁrst bufer in the list is the most recently used, and the last is the least recently used.
    // The two loops in bget take advantage of this: the scan for an existing bufer must process the entire list in the worst case,
    // but checking the most recently used bufers ﬁrst (starting at bcache.head and following next pointers) will reduce scan time when there is good locality of
    // reference. The scan to pick a bufer to reuse picks the least recently used buﬀer by scanning backward (following prev pointers).
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}
//PAGEBREAK!
// Blank page.

