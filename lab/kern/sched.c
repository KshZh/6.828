#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle;
    struct Env *p;

	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment. Make sure curenv is not null before
	// dereferencing it.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.

	// LAB 4: Your code here.
    // JOS中，每个CPU只有一个内核栈，进程有自己的用户栈，但共有内核栈。
    // 因为同一个时刻，只允许一个CPU上的一个进程进入内核态，所以同一时刻，只能有
    // 一个CPU的sched_yield执行，这避免了不同CPU同时调度同一个进程执行。
    // 在内核态的进程，其必然通过中断（无论是系统调用还是时钟中断）进入内核态，
    // 必然通过中断处理函数，必然进入kern/trapentry.S:_alltraps，配合INT指令在当前CPU的内核栈中
    // 建立Trapframe。当然，这个Trapframe马上就会被复制到当前进程对应的Env对象的Trapframe成员中，
    // 因为内核栈是共享的，必须这样做，不然该进程的内核栈中的Trapframe可能被其它即将在该CPU上运行的
    // 进程覆盖。
    // sched_yield会对ENV_RUNNABLE的进程调用env_run，env_run会调用env_pop_tf，恢复目标进程
    // 对应的数据结构Env中的Trapframe（不是内核栈中的），从而使目标进程返回用户态继续执行。
    // env_run不会返回。
    idle = (curenv==NULL)? envs: ((curenv+1==envs+NENV)? envs: curenv+1);
    p = idle;
    do {
        if (p->env_status == ENV_RUNNABLE)
            env_run(p);
        p++;
        if (p == envs+NENV)
            p = envs;
    } while(p != idle);

    if (p == idle) {
        // no envs are runnable
        if (curenv != NULL && curenv->env_status == ENV_RUNNING)
            env_run(curenv);
    }

	// sched_halt never returns
	sched_halt();
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movl $0, %%ebp\n"
		"movl %0, %%esp\n"
		"pushl $0\n"
		"pushl $0\n"
		// Uncomment the following line after completing exercise 13
		"sti\n"
		"1:\n"
		"hlt\n"
		"jmp 1b\n"
	: : "a" (thiscpu->cpu_ts.ts_esp0));
}

