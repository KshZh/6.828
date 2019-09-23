# Homework: xv6 lazy page allocation

Xv6 applications ask the kernel for heap memory using the sbrk() system call. In the kernel we've given you, sbrk() allocates physical memory and maps it into the process's virtual address space. There are programs that allocate memory but never use it, for example to implement large sparse arrays. Sophisticated kernels delay allocation of each page of memory until the application tries to use that page -- as signaled by a page fault. You'll add this lazy allocation feature to xv6 in this exercise.

## Part One: Eliminate allocation from sbrk()

```c
int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  // hw04, lazy page allocation
  // if(growproc(n) < 0)
  //  return -1;
  // return addr;

  // 注意不要脑抽写成下面这样，返回的肯定是新分配的内存的起始地址。
  // 主要是要更新myproc()->sz。
  // return addr+n;
  
  myproc()->sz += PGROUNDUP(n); // 注意round up，如果仔细看的话，growproc()会这样更新myproc()->sz。
  return addr;
}
```

Make this modification, boot xv6, and type `echo hi` to the shell. You should see something like this:

```
init: starting sh
$ echo hi
pid 3 sh: trap 14 err 6 on cpu 0 eip 0x112c addr 0xc004--kill proc
```

The "pid 3 sh: trap..." message is from the kernel trap handler in trap.c; **it has caught a page fault (trap 14, or T_PGFLT),** which the xv6 kernel does not know how to handle. Make sure you understand why this page fault occurs. **The "addr 0x4004" indicates that the virtual address that caused the page fault is 0x4004.**

## Part Two: Lazy allocation

这里就要我们在trap.c中添加page fault的handler了。在handler中，我们只需要分配并映射一个page就可以了，之后不够用，再触发page fault，到时再分配并映射就行了，不用像sbrk()调用的growproc()那样分配并映射足够多的page覆盖oldsz到newsz。

```c
  case T_PGFLT:
  {
    // page fault handler
    uint va = rcr2();
    va = PGROUNDDOWN(va);
    char *m = kalloc();
    if(va<=KERNBASE && m != 0){
        memset(m, 0, PGSIZE);
        // 分配+映射。
        int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
        if (mappages(myproc()->pgdir, (void*)va, PGSIZE, V2P(m), PTE_W|PTE_U) >= 0)
            break;
        kfree(m); // 出错了记得释放分配的page。
    }
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
    break;
  }
```

Optional challenges: Handle negative sbrk() arguments. Handle error cases such as sbrk() arguments that are too large. Verify that fork() and exit() work even if some sbrk()'d address have no memory allocated for them. Correctly handle faults on the invalid page below the stack. Make sure that kernel use of not-yet-allocated user addresses works -- for example, if a program passes an sbrk()-allocated address to read().

