# Homework: xv6 system calls

## Part One: System call tracing

Your first task is to modify the xv6 kernel to print out a line for each system call invocation. It is enough to print the name of the system call and the return value; you don't need to print the system call arguments.

**call write -> write（用户态系统调用） -> INT T_SYSCALL（在write中的指令） -> 用中断向量号index idt -> 中断处理函数 -> trapasm.S:alltraps -> trap.c:trap -> syscall.c:syscall（根据系统调用号分发系统调用） -> 系统调用 -> syscall.c:syscall -> trap.c:trap -> trapasm.S:trapret -> IRET -> ret**。

讲义中包含对这个系统调用过程的详细调试，包括执行流程、用户栈和内核栈中保存了哪些数据等，一定要认真看看。

清楚了系统调用过程，就知道该在哪里添加输出语句了。

Hint: modify the syscall() function in syscall.c.

Optional challenge: print the system call arguments.

## Part Two: Date system call

Your second task is to add a new system call to xv6. The main point of the exercise is for you to see some of the different pieces of the system call machinery. Your new system call will get the current UTC time and return it to the user program. You may want to use the helper function, `cmostime()``lapic.c``date.h``struct rtcdate``cmostime()`

You should create a user-level program that calls your new date system call; here's some source you should put in `date.c`:

```c
#include "types.h"
#include "user.h"
#include "date.h"

int
main(int argc, char *argv[])
{
  struct rtcdate r;

  if (date(&r)) {
    printf(2, "date failed\n");
    exit();
  }

  // your code to print the time in any format you like...

  exit();
}
```

In order to make your new `date` program available to run from the xv6 shell, add `_date` to the `UPROGS` definition in `Makefile`.

Your strategy for making a date system call should be to clone all of the pieces of code that are specific to some existing system call, for example the "uptime" system call. You should grep for uptime in all the source files, using `grep -n uptime *.[chS]`.

这里的意思是让我们跟踪已经实现好的一个系统调用，如uptime，参考它的实现轨迹来实现date调用。

When you're done, typing `date` to an xv6 shell prompt should print the current UTC time.

```
k@ubuntu:~/6.828/xv6-public$ grep -n uptime *.[chS]
syscall.c:113:extern int sys_uptime(void);
syscall.c:130:[SYS_uptime]  sys_uptime,
syscall.c:155:// [SYS_uptime]  "uptime",
syscall.h:15:#define SYS_uptime 14
sysproc.c:83:sys_uptime(void)
user.h:25:int uptime(void);
usys.S:31:SYSCALL(uptime)
```

在user.h中添加系统调用在用户态的声明：

```c
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int date(struct rtcdate *); // hw03

// ulib.c
int stat(const char*, struct stat*);
```

在usys.S中添加系统调用在用户态的实现/定义（直接用汇编实现）：

```assembly
#define SYSCALL(name) \
  .globl name; \
  name: \
    movl $SYS_ ## name, %eax; \
    int $T_SYSCALL; \
    ret

...
SYSCALL(sleep)
SYSCALL(uptime)
SYSCALL(date)
```

在syscall.h中为date系统调用分配系统调用号：

```c
#define SYS_close  21
#define SYS_date   22
```

在syscall.c中添加系统调用在内核态的外部声明并添加到函数表中：

```c
extern int sys_uptime(void);
extern int sys_date(void);

static int (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_date]    sys_date,
};
```

在sysproc.c中添加系统调用的内核态实现/定义：

```c
int
sys_date(void)
{
  struct rtcdate *r;
  if(argint(0, (int*)&r) < 0)
    return -1;
  cmostime(r);
  return 0;
}
```

Optional challenge: add a dup2() system call and modify the shell to use it.

TODO：这个涉及到xv6的文件系统，后面再回过头来做。

