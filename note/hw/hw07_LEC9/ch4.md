# Chapter 4 Locking

> **The concurrency may arise from accesses by multiple cores, or by multiple threads, or by interrupt code. **
>
> **You must keep in mind that a single C statement can be several machine instructions and thus another processor or an interrupt may muck around in the middle of a C statement. You cannot assume that lines of code on the page are executed atomically. Concurrency makes reasoning about correctness much more diﬃcult.**

## Race conditions

hw06的并发操作链表就是race的一个例子，表现为丢失修改。

> This implementation is correct if executed in isolation. However, the code is not correct if more than one copy executes concurrently.
>
> **A race condition is a situation in which a memory location is accessed concurrently, and at least one access is a write.** A race is often a sign of a bug, either a **lost update** (if the accesses are writes) or **a read of an incompletely-updated data structure**. 
>
> **The usual way to avoid races is to use a lock. Locks ensure mutual exclusion, so that only one CPU can execute insert at a time**.
>
> The sequence of instructions between acquire and release is often called a critical section, and the lock protects list. 
>
> **When we say that a lock protects data, we really mean that the lock protects some collection of invariants that apply to the data. Invariants are properties of data structures that are maintained across operations（在数据操作之间维持，在操作时可被破坏再建立）.Typically, an operation’s correct behavior depends on the invariants being true when the operation begins. **
>
> You can **think of locks as serializing concurrent critical sections so that they run one at a time, and thus preserve invariants **(assuming they are correct in isolation). You can also **think of critical sections as being atomic** with respect to each other, **so that a critical section that obtains the lock later sees only the complete set of changes from earlier critical sections, and never sees partially-completed updates. **

> **Ways to think about what locks achieve
>   locks help avoid lost updates
>   locks help you create atomic multi-step operations -- hide intermediate states
>   locks help operations maintain invariants on a data structure
>     assume the invariants are true at start of operation
>     operation uses locks to hide temporary violation of invariants
>     operation restores invariants before releasing locks**

## Code: Locks

spinlock.[ch], sleeplock.[ch]

## Code: Using locks

> A hard part about using locks is deciding how many locks to use and which data and invariants each lock protects. There are a few basic principles. **First, any time a variable can be written by one CPU at the same time that another CPU can read or write it, a lock should be introduced to keep the two operations from overlapping. Second, remember that locks protect invariants**: if an invariant involves multiple memory locations, typically all of them need to be protected by a single lock to ensure the invariant is maintained.
>
> **it is important for eﬃciency not to lock too much, because locks reduce parallelism.**

## Deadlock and lock ordering

> **If a code path through the kernel must hold several locks at the same time, it is important that all code paths acquire the locks in the same order. If they don’t, there is a risk of deadlock. **

## Interrupt handlers

> **Interrupts can cause concurrency even on a single processor: if interrupts are enabled, kernel code can be stopped at any moment to run an interrupt handler instead. **
>
> ```c
> // Acquire the lock.
> // Loops (spins) until the lock is acquired.
> // Holding a lock for a long time may cause
> // other CPUs to waste time spinning to acquire it.
> void
> acquire(struct spinlock *lk)
> {
>   pushcli(); // disable interrupts to avoid deadlock.
>   if(holding(lk))
>     // 如果当前CPU的某处代码已经加上了锁lk，并且中断被屏蔽了，
>     // 这就意味这当前CPU上只有这一个执行流，除非主动yield（没有时钟中断来切换），否则其它执行流将一直挂起。
>     // 也就是意味着死锁，所以panic。
>     panic("acquire");
> 
>   // The xchg is atomic.
>   while(xchg(&lk->locked, 1) != 0)
>     ;
> 
>   // Tell the C compiler and the processor to not move loads or stores
>   // past this point, to ensure that the critical section's memory
>   // references happen after the lock is acquired.
>   __sync_synchronize();
> 
>   // Record info about lock acquisition for debugging.
>   lk->cpu = mycpu();
>   getcallerpcs(&lk, lk->pcs);
> }
> ```
>
> To avoid this situation, if a spin-lock is used by an interrupt handler, **a processor must never hold that lock with interrupts enabled**. Xv6 is more conservative: **when a processor enters a spin-lock critical section, xv6 always ensures interrupts are disabled on that processor**. **Interrupts may still occur on other processors, so an interrupt’s acquire can wait for a thread to release a spin-lock; just not on the same processor. **
>
> **it must do a little book-keeping to cope with nested critical sections. acquire calls pushcli (1667) and release calls popcli (1679) to track the nesting level of locks on the current processor. When that count reaches zero, popcli restores the interrupt enable state that existed at the start of the outermost critical section. **

## Instruction and memory orderin

> Many compilers and processors, however, execute code out of order to achieve higher performance. 
>
> **To tell the hardware and compiler not to perform such re-orderings, xv6 uses __sync_synchronize()**, in both acquire and release. _sync_synchronize() is a memory barrier: it tells the compiler and CPU to not reorder loads or stores across the barrier. **Xv6 worries about ordering only in acquire and release, because concurrent access to data structures other than the lock structure is performed between acquire and release.**

## Sleep locks

> Xv6 sleep-locks support yielding the processor during their critical sections. This property poses a design challenge: if thread T1 holds lock L1 and has yielded the processor, and thread T2 wishes to acquire L1, we have to ensure that T1 can execute while T2 is waiting so that T1 can release L1. **T2 can’t use the spin-lock acquire function here: it spins with interrupts turned oﬀ, and that would prevent T1 from running. To avoid this deadlock, the sleep-lock acquire routine (called acquiresleep) yields the processor while waiting, and does not disable interrupts.**

## Limitations of locks

