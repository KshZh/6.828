# Homework: xv6 CPU alarm

You should add a new `alarm(interval, handler)` system call. If an application calls `alarm(n, fn)`, then after every `n` "ticks" of CPU time that the program consumes, the kernel will cause application function `fn` to be called. When `fn` returns, the application will resume where it left off. A tick is a fairly arbitrary unit of time in xv6, determined by how often a hardware timer generates interrupts.

首先添加测试代码，将其添加到Makefile中。

首先要添加系统调用`grep -n uptime *.[Sch]`。

系统调用的内核态实现已给出：

```c
int
sys_alarm(void)
{
  int ticks;
  void (*handler)();

  if(argint(0, &ticks) < 0)
    return -1;
  if(argptr(1, (char**)&handler, 1) < 0)
    return -1;
  myproc()->alarmticks = ticks;
  myproc()->alarmhandler = handler;
  return 0;
}
```

Hint: You'll need to keep track of how many ticks have passed since the last call (or are left until the next call) to a process's alarm handler; you'll need a new field in `struct proc` for this too. You can initialize `proc` fields in `allocproc()` in `proc.c`.

```c
// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)

  // hw05, for alarm system call
  int alarmticks;
  int tickspassed;
  void (*alarmhandler)();
};
```

```c
  // hw05, for alarm system call
  p->tickspassed = 0;
  p->alarmticks = -1; // 为了在没有系统调用alarm的时候，永远不触发alarm。
```

Hint: Every tick, the hardware clock forces an interrupt, which is handled in `trap()` by `case T_IRQ0 + IRQ_TIMER`; you should add some code here.

Hint: You only want to manipulate a process's alarm ticks if there's a process running and if the timer interrupt came from user space; you want something like

```
    if(myproc() != 0 && (tf->cs & 3) == 3) ...
```

Hint: In your `IRQ_TIMER` code, when a process's alarm interval expires, you'll want to cause it to execute its handler. How can you do that?

Hint: You need to arrange things so that, when the handler returns, the process resumes executing where it left off. How can you do that?

回忆一下LEC5的讲义或HW03的内容：[l-internal.txt](../hw03_LEC5/l-internal.txt)

```c
  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    // Hint: Every tick, the hardware clock forces an interrupt, which is handled in trap() by case T_IRQ0 + IRQ_TIMER; you should add some code here.
    // Hint: You only want to manipulate a process's alarm ticks if there's a process running and if the timer interrupt came from user space; 
    if(myproc() != 0 && (tf->cs & 3) == 3) {
        myproc()->tickspassed++;
        if (myproc()->tickspassed >= myproc()->alarmticks) {
            myproc()->tickspassed = 0; // 清零
            // call myproc()->alarmhandler
            // 注意不能在内核态调用用户程序提供的函数。
            // 对系统调用，可以看一下LEC5的讲义。
            // 这里简单说一下，**tf->eip时用户态系统调用的返回地址，*(tf->esp)是调用用户态系统调用的代码的返回地址**。
            // **显然在执行完handler后，要返回用户态系统调用继续执行**。
            tf->esp -= 4; // 32位机器，地址占4字节。
            *(uint*)(tf->esp) = tf->eip;
            tf->eip = (uint)myproc()->alarmhandler; // 返回后先执行handler。
        }
    }
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
```

It's OK if your solution doesn't save the caller-saved user registers when calling the handler.

Optional challenges: 1) Save and restore the caller-saved user registers around the call to handler. 2) Prevent re-entrant calls to the handler -- if a handler hasn't returned yet, don't call it again. 3) Assuming your code doesn't check that `tf->esp` is valid, implement a security attack on the kernel that exploits your alarm handler calling code.

