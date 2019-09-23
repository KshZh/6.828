# Homework: User-level threads

```c
#include "types.h"
#include "stat.h"
#include "user.h"

/* Possible states of a thread; */
#define FREE        0x0
#define RUNNING     0x1
#define RUNNABLE    0x2

#define STACK_SIZE  8192
#define MAX_THREAD  4

typedef struct thread thread_t, *thread_p;
typedef struct mutex mutex_t, *mutex_p;

struct thread {
  int        sp;                /* saved stack pointer */
  char stack[STACK_SIZE];       /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
};
static thread_t all_thread[MAX_THREAD];
thread_p  current_thread;
thread_p  next_thread;
extern void thread_switch(void);

void 
thread_init(void)
{
  // main() is thread 0, which will make the first invocation to
  // thread_schedule().  it needs a stack so that the first thread_switch() can
  // save thread 0's state.  thread_schedule() won't run the main thread ever
  // again, because its state is set to RUNNING, and thread_schedule() selects
  // a RUNNABLE thread.
  current_thread = &all_thread[0];
  current_thread->state = RUNNING;
}

static void 
thread_schedule(void)
{
  thread_p t;

  /* Find another runnable thread. */
  next_thread = 0;
  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == RUNNABLE && t != current_thread) {
      next_thread = t;
      break;
    }
  }

  if (t >= all_thread + MAX_THREAD && current_thread->state == RUNNABLE) {
    /* The current thread is the only runnable thread; run it. */
    next_thread = current_thread;
  }

  if (next_thread == 0) {
    printf(2, "thread_schedule: no runnable threads\n");
    exit();
  }

  if (current_thread != next_thread) {         /* switch threads?  */
    next_thread->state = RUNNING;
    thread_switch();
  } else
    next_thread = 0;
}

void 
thread_create(void (*func)())
{
  thread_p t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->sp = (int) (t->stack + STACK_SIZE);   // set sp to the top of the stack
  t->sp -= 4;                              // space for return address
  * (int *) (t->sp) = (int)func;           // push return address on stack
  // The intent is that thread_switch use the assembly instructions popal and pushal to restore and save all eight x86 registers. Note that thread_create simulates eight pushed registers (32 bytes) on a new thread's stack.
  t->sp -= 32;                             // space for registers that thread_switch expects
  t->state = RUNNABLE;
}

void 
thread_yield(void)
{
  current_thread->state = RUNNABLE;
  thread_schedule();
}

static void 
mythread(void)
{
  int i;
  printf(1, "my thread running\n");
  for (i = 0; i < 100; i++) {
    printf(1, "my thread 0x%x\n", (int) current_thread);
    thread_yield();
  }
  printf(1, "my thread: exit\n");
  current_thread->state = FREE;
  thread_schedule(); // 调度别的“线程”执行，当前“线程”退出。
}


int 
main(int argc, char *argv[]) 
{
  thread_init();
  // 创建两个“线程”，当然，这个程序还是单线程的，同一时刻只能有一个“线程”执行。
  thread_create(mythread);
  thread_create(mythread);
  thread_schedule(); // 调度其中一个“线程”开始执行。
  return 0;
}
```

Once your xv6 shell runs, type "uthread", and gdb will break at `thread_switch`. Now you can type commands like the following to inspect the state of `uthread`:

```
(gdb) p/x next_thread->sp
$4 = 0x4ae8
(gdb) x/9x next_thread->sp
0x4ae8 :      0x00000000      0x00000000      0x00000000      0x00000000
0x4af8 :      0x00000000      0x00000000      0x00000000      0x00000000
0x4b08 :      0x000000d8
```

看thread_create可以知道0x4b08处的这个字（4字节）就是函数mythread的地址，（地址）往下的8个字就是pushal推入的8个寄存器。

```c
	.text

/* Switch from current_thread to next_thread. Make next_thread
 * the current_thread, and set next_thread to 0.
 * Use eax as a temporary register; it is caller saved.
 */
	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */
    // **因为是用户线程，切换后也还在用户代码段和数据段，用户栈也是一样的，**
    // **所以段寄存器/选择子%cs, %ds, %ss始终是相同的，不必保存，要保存%eip, “栈指针”。**

    // call指令隐含地push %eip
    pushal

    // 切换线程栈
    movl current_thread, %eax
    movl %esp, (%eax) // 将当前%esp保存到旧“线程”的sp中。
    movl next_thread, %eax
    movl (%eax), %esp // 将目标“线程”的sp载入%esp。

    // Make next_thread the current_thread, and set next_thread to 0.
    movl next_thread, %eax
    movl %eax, current_thread
    movl $0, %eax
    movl %eax, next_thread

    popal
	ret				/* pop return address from stack */
    // ret指令隐含地pop %eip
```

## Optional challenges

The user-level thread package interacts badly with the operating system in several ways. For example, if one user-level thread blocks in a system call, another user-level thread won't run, because the user-level threads scheduler doesn't know that one of its threads has been descheduled by the xv6 scheduler. As another example, two user-level threads will not run concurrently on different cores, because the xv6 scheduler isn't aware that there are multiple threads that could run in parallel. Note that if two user-level threads were to run truly in parallel, this implementation won't work because of several races (e.g., two threads on different processors could call `thread_schedule` concurrently, select the same runnable thread, and both run it on different processors.)

There are several ways of addressing these problems. One is using [scheduler activations](http://en.wikipedia.org/wiki/Scheduler_activations) and another is to use one kernel thread per user-level thread (as Linux kernels do). Implement one of these ways in xv6.

Add locks, condition variables, barriers, etc. to your thread package.

