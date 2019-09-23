# Homework: xv6 locking

## Don't do this

```c
  struct spinlock lk;
  initlock(&lk, "test lock");
  acquire(&lk);
  acquire(&lk);
```

> **Submit**: Explain in one sentence what happens.

> 当前CPU会死锁，lapicid 0: panic: acquire

## Interrupts in ide.c

An `acquire` ensures that interrupts are off on the local processor using the `cli` instruction (via `pushcli()`), and that interrupts remain off until the `release` of the last lock held by that processor (at which point they are enabled using `sti`).

Let's see what happens if we turn on interrupts while holding the `ide` lock. In `iderw` in `ide.c`, add a call to `sti()` after the `acquire()`, and a call to `cli()` just before the `release()`. Rebuild the kernel and boot it in QEMU. Chances are the kernel will panic soon after boot; try booting QEMU a few times if it doesn't.

> **Submit**: Explain in a few sentences why the kernel panicked. You may find it useful to look up the stack trace (the sequence of `%eip`values printed by `panic`) in the `kernel.asm` listing.

这里有两种panic的情况，

- iderw通过idestart发起一个磁盘读写任务后，进入sleep，之后任务完成，设备发出中断请求，进入trap，再进入ideintr，**ideintr会在同一个CPU上请求之前被iderw加锁的idelock**，发生了死锁，于是acquire会panic。
- iderw请求idelock，又sti后，来了时钟中断，进入trap -> yield -> sched，然而让出CPU的进程必须释放除ptable.lock之外的锁，否则即将在同一个CPU上执行的进程如果请求了前面进程未解锁的锁，**由于在同一CPU上，且不管加了什么锁xv6一定会屏蔽中断（也就屏蔽了时钟中断，使得前面的进程没有机会被调度从而没有机会释放锁）**，于是也会死锁，所以sched会panic。

## Interrupts in file.c

Remove the `sti()``cli()`

Now let's see what happens if we turn on interrupts while holding the `file_table_lock`. This lock protects the table of file descriptors, which the kernel modifies when an application opens or closes a file. In `filealloc()` in `file.c`, add a call to `sti()` after the call to `acquire()`, and a `cli()` just before **each** of the `release()`es. You will also need to add `#include "x86.h"` at the top of the file after the other `#include` lines. Rebuild the kernel and boot it in QEMU. It most likely will not panic.

> **Submit**: Explain in a few sentences why the kernel didn't panic. Why do `file_table_lock` and `ide_lock` have different behavior in this respect?
>
> You do not need to understand anything about the details of the IDE hardware to answer this question, but you may find it helpful to look at which functions acquire each lock, and then at when those functions get called.

```c
// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}
```

与idelock不同，中断处理函数中不会请求ftable.lock，所以上面第一种情况不会发生，但好像还是无法排除第二种情况。

## xv6 lock implementation

> **Submit**: Why does `release()` clear `lk->pcs[0]` and `lk->cpu` *before* clearing `lk->locked`? Why not wait until after?

> 若在释放锁后面，会发生：
>
> 1. 锁释放， `lk->pcs[0]` 跟 `lk->cpu` 未清除
> 2. 另一个 CPU 尝试获取锁并成功，设置 `lk->pcs[0]` 跟 `lk->cpu`
> 3. 当前 CPU 清除 `lk->pcs[0]` 跟 `lk->cpu`
>
> 从而导致锁的信息不正确。



References:

- https://cowlog.com/post/6-dot-828-homework-7-xv6-locking

