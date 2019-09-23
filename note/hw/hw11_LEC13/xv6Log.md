# Homework: xv6 log

This assignment explores the xv6 log in two parts. First, you'll artificially create a crash which illustrates why logging is needed. Second, you'll remove one inefficiency in the xv6 logging system.

## Creating a Problem

**The point of the xv6 log is to cause all the disk updates of a filesystem operation to be atomic with respect to crashes.** For example, file creation involves both adding a new entry to a directory and marking the new file's inode as in-use. A crash that happened after one but before the other would leave the file system in an incorrect state after a reboot, if there were no log.

The following steps will break the logging code in a way that leaves a file partially created.

First, replace `commit()` in `log.c` with this code:

```c
#include "mmu.h"
#include "proc.h"
void
commit(void)
{
  int pid = myproc()->pid;
  if (log.lh.n > 0) {
    write_log(); // 将log.lh.block数组中的block no对应的内存缓存中的buf，写入磁盘的log区域。
    write_head(); // 写入log.lh.n，完成了commit。
    if(pid > 1)            // AAA
      log.lh.block[0] = 0; // BBB 
    install_trans(); // 执行完install_trans()后才crash。
    if(pid > 1)            // AAA
      panic("commit mimicking crash"); // CCC
    log.lh.n = 0; 
    write_head();
  }
}
```

The BBB line causes the first block in the log to be written to block zero, rather than wherever it should be written. During file creation, the first block in the log is the new file's inode updated to have non-zero `type`. Line BBB causes the block with the updated inode to be written to block 0 (whence it will never be read), leaving the on-disk inode still marked unallocated. The CCC line forces a crash. The AAA lines suppress this buggy behavior for `init`, which creates files before the shell starts.

Second, replace `recover_from_log()` in `log.c` with this code:

```c
static void
recover_from_log(void)
{
  read_head();      
  cprintf("recovery: n=%d but ignoring\n", log.lh.n);
  // install_trans();
  log.lh.n = 0;
  // write_head();
}
```

消除了recover，删除fs.img，启动xv6：

```
init: starting sh
$ echo hi > a
lapicid 0: panic: commit mimicking crash
 80102d7e 801050b6 801048b9 801058cd 8010572c 0 0 0 0 0QEMU: Terminated
```

保持fs.img，重启xv6：

```
init: starting sh
$ echo hi > a
lapicid 0: panic: ilock: no type
 80101757 8010494d 80105104 801048b9 801058cd 8010572c 0 0 0 0QEMU: Terminated
```

在crash前，创建文件a，首先分配并创建了一个inode，代表文件a，然后在当前目录文件中分配并写入了一个dirent对象，其inum成员的值就是文件a的inode的inum。这里总共写了两个block的内存缓存buf，事务结束时log层会commit，将这两个内存缓存buf先写入磁盘log区，然后将log.lh.n写入磁盘log区的头部block中的logheader对象，完成commit，然后，在上面这种情况中，会按安排好的crash。

## Solving the Problem

现在去掉recover_from_log()`中的代码注释，保持fs.img，重启xv6：

```
init: starting sh
$ cat a
$ 
```

因为crash之前log层已经完成了commit，现在恢复了recover功能，所以log层会redo crash前的已commit事务，所以就把两个logged blocks写回磁盘lh.block[i]处的block。

Why was the file empty, even though you created it with `echo hi > a`?

这个命令涉及I/O重定向，对于这个问题，要考虑是echo程序先执行还是文件a先被打开/创建。

可以看一下sh.c的相关部分：

```c
  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      printf(2, "open %s failed\n", rcmd->file);
      exit();
    }
    runcmd(rcmd->cmd);
    break;
```

可以看到，是先打开/创建文件a，将子进程的描述符1指向文件a的file对象，然后才载入echo程序执行。

而由于创建文件a时会crash，所以echo程序没机会运行，所以当恢复后，文件a是空的。

## Streamlining Commit

Suppose the file system code wants to update an inode in block 33. The file system code will call `bp=bread(block 33)` **and update the buffer data**. `write_log()` in `commit()`will copy the data to a block in the log on disk, for example block 3. A bit later in `commit`, `install_trans()` reads block 3 from the log (containing block 33), copies its contents into the in-memory buffer for block 33, and then writes that buffer to block 33 on the disk.

However, in `install_trans()`, it turns out that the modified block 33 is guaranteed to be still in the buffer cache, where the file system code left it.（**因为这个buf的B_DIRTY位被log_write或bwrite设置了，而buffer cache层不会evict一个B_DIRTY的buf/block（buf包含block，还附带一些元数据），注意，buffer cache层在事务commit前evict一个在该事务中只读而未被修改过的buf是没有任何问题的**） Make sure you understand why it would be a mistake for the buffer cache to evict block 33 from the buffer cache before the commit.

假设在一个事务中，系统调用修改了block 33，buffer cache层evict这个block，将其写回磁盘，然后系统调用又修改了block 33（可能使得bcache层再次evict，再将block 33读入），再commit前，crash了。但是，这个事务中，第一次对block 33的修改在恢复时应该被忽略，然而事实是，该修改却被应用了。

**Since the modified block 33 is guaranteed to already be in the buffer cache, there's no need for `install_trans()` to read block 33 from the log.** Your job: modify `log.c`so that, when `install_trans()` is **called from `commit()`**, `install_trans()` does not perform the needless read from the log.

To test your changes, create a file in xv6, restart, and make sure the file is still there.

```c
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
```

