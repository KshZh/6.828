#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0); // istrap参数为0表示在进入对应的中断处理函数vectors[trapno]之前，要INT指令屏蔽中断。
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER); // istrap参数为1。
  // 注意这里特地将IDT表的这个描述符的DPL设置为3，使得用户模式的代码可以使用这个描述符（满足CPL<=DPL）。

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

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
            // 这里简单说一下，tf->eip时用户态系统调用的返回地址，*(tf->esp)是调用用户态系统调用的代码的返回地址。
            // 显然在执行完handler后，要返回用户态系统调用继续执行。
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
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
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

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
