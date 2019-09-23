#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
  // XXX The count in the header block on disk is either zero, indicating that there is no transaction in the log,
  // or non-zero, indicating that the log contains a complete committed transaction with the indicated number of logged blocks.
  int n; // 记录logged block的数目，最大为LOGSIZE。
  int block[LOGSIZE]; // 记录logged block的blockno。
};

struct log {
  struct spinlock lock; // 该自旋锁提供对该共享数据结构的互斥访问。
  int start; // 磁盘的log区的其实block no。
  int size;  // 磁盘的log区的大小，单位是block，不包括存放logheader的block？只是可容纳的log block数？
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
struct log log; // 如果有struct前缀，则log表示log类型，否则log表示这里定义的log对象。

static void recover_from_log(void);
static void commit();

void
initlog(int dev)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  struct superblock sb;
  initlock(&log.lock, "log");
  readsb(dev, &sb);
  log.start = sb.logstart;
  log.size = sb.nlog;
  log.dev = dev;
  // 机器重启后，尝试恢复上次被中断的事务，如果有的话。
  recover_from_log();
}

// Copy committed blocks from log to their home location
static void
// install_trans(void)
install_trans(int calledFromCommit)
{
  int tail;

  // 注意到这个循环条件，如果log.lh.n为0，那么不会写任何block到磁盘。
  for (tail = 0; tail < log.lh.n; tail++) {
    // log.start是磁盘log区的起始block no，存放logheader，+1是跳过这个block。
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst
    if (!calledFromCommit) {
        struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block
        memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
        brelse(lbuf);
    }
    bwrite(dbuf);  // write dst to disk
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void
write_head(void)
{
  // 用内存中更新过的logheader更新磁盘上的logheader数据结构。
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  // install_trans可能会redo已commit事务，或者忽略。
  // 这里的代码如果要清晰的话，就直接`if (log.lh.n != 0)`才redo，否则什么也不用做。
  cprintf("recovery: n=%d\n", log.lh.n);
  install_trans(0); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
void
begin_op(void)
{
  acquire(&log.lock);
  // 注意，sleep要配合循环使用。
  while(1){
    if(log.committing){
      // 赶不上这个事务了，那就只能并入下一个事务。
      sleep(&log, &log.lock); // 等待commit完成。
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // LOGSIZE在param.h中定义为`3*MAXOPBLOCKS`。
      // this op might exhaust log space; wait for commit.
      // 这里假设每个FS系统调用最多修改MAXOPBLOCKS个block。
      // 如果该系统调用也并入当前事务的话，则磁盘的log区域可能没有足够的block来存放该系统调用修改的block。
      // 既然不能保证有足够的log磁盘空间存放该系统调用修改的block，那就只能等待当前事务commit，
      // 然后将该系统调用加入到下一个事务中了。
      // 当然，如果当前事务中，有一些系统调用实际修改的block数少于MAXOPBLOCKS，那么当前系统调用还是有机会加入到当前事务中的。
      // 注意这里并没有修改log.lh.n，只有log_write会根据实际来修改它，这里只是读取它，用它来做一个预测。
      sleep(&log, &log.lock);
    } else {
      // 将该系统调用并入当前事务。
      // Incrementing log.outstanding both reserves space and prevents a commit from occuring during this system call. 
      log.outstanding += 1; // how many FS sys calls are executing.
      release(&log.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock); // 获取锁，才能操作多进程（多CPU）共享的log对象。
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
    // 上面这段注释说的是，我们假设每个FS系统调用最多修改MAXOPBLOCKS个block，
    // 那么如果磁盘的log区域没有足够的空闲block，就不能把一个系统调用加入到当前事务。
    // 然而，一个FS系统调用可能修改的block数少于MAXOPBLOCKS，也就是log.lh.n实际上只
    // 递增了一个小于MAXOPBLOCKS的数，则原本预测log磁盘空间不足的，现在也许log磁盘空间就足够了，
    // 所以就试着wakeup其中一个sleep在&log上的进程/系统调用，也许它现在就可以加入到当前事务了。
    // 如果没有进程sleep在&log这个chan上，wakeup也就没事做，就会返回。
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    // 其它进程只要看到log.committing==1，就会sleep，暂停操作log对象。
    // 故commit()不需要持锁。
    commit();
    acquire(&log.lock);
    log.committing = 0;
    // commit已经完成，wakeup其它等待commit完成的进程。
    // 使得这些进程开启下一个事务。
    wakeup(&log);
    release(&log.lock);
  }
}

// Copy modified blocks from cache to log.
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    // 系统调用修改的都是cache block。
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    // 将系统调用更新过的cache block写入磁盘的log区域。
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  // The count in the header block on disk is either zero, indicating that there is no transaction in the log,
  // or non-zero, indicating that the log contains a complete committed transaction with the indicated number of logged blocks.
  if (log.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    // 下面的write_head将内存对象log.lh的n成员写入磁盘对象lh的n成员中，这完成了commit，在之后commit()剩余的操作中，
    // 但凡中间crash了，机器恢复后，看到磁盘对象lh的n成员不为0，且上面的write_log已经把事务中修改过的block拷贝到磁盘中的log区域了，
    // 会redo这个事务，重做下面的这些操作。
    // 如果write_head在更新磁盘对象lh的n成员之前crash，此时也没有对磁盘中相应的block进行更新，最多只是将更新过的内存block写入磁盘的log区域，
    // 机器恢复后，看到磁盘对象lh的n成员为0，就会忽略这个事务，
    // 这个策略保证了事务相对于crash的原子性。
    write_head();    // Write header to disk -- the real commit

    install_trans(1); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache with B_DIRTY.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  // log_write是buffer cache层bwrite的一个proxy，它不直接调用iderw将logged block写入磁盘，
  // 而是先标记在lh的block数组中，直到这个transaction commit才会将其写入磁盘。
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  // 将b添加到在lh.block中，可能由于b在一个事务中被多次log_write导致b已经在lh.block中了。
  // XXX log_write notices when a block is written multiple times during a single transaction,
  // and allocates that block the same slot in the log. This optimization is often called absorption.
  // It is common that, for example, the disk block containing inodes of several ﬁles is written several times within a transaction.
  // By absorbing several disk writes into one, the ﬁle system can save log space
  // and can achieve better performance because only one copy of the disk block must be written to disk. 
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion(吸收)
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n)
    log.lh.n++;
  b->flags |= B_DIRTY; // prevent eviction
  // The block must stay in the cache until committed: until then, the cached copy is the only record of the modiﬁcation;
  // it cannot be written to its place on disk until after commit; and other reads in the same transaction must see the modiﬁcations. 
  release(&log.lock);
}

