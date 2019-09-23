/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
    user_mem_assert(curenv, s, len, PTE_U|PTE_P);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	// panic("sys_exofork not implemented");
    struct Env *e;
    int err;
    if ((err=env_alloc(&e, curenv->env_id)) != 0)
        return err;
    e->env_status = ENV_NOT_RUNNABLE;
    e->env_tf = curenv->env_tf;
    e->env_tf.tf_regs.reg_eax = 0; // 子进程返回0
    return e->env_id; // 父进程返回子进程id
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	// panic("sys_env_set_status not implemented");
    struct Env *e;
    if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
        return -E_INVAL;
    if (envid2env(envid, &e, 1) != 0)
        return -E_BAD_ENV;
    e->env_status = status;
    return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3), interrupts enabled, and IOPL of 0.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	// panic("sys_env_set_trapframe not implemented");
    user_mem_assert(curenv, tf, sizeof(struct Trapframe), PTE_U|PTE_P);
    struct Env *e;
    if (envid2env(envid, &e, 1) != 0)
        return -E_BAD_ENV;
    e->env_tf = *tf;
    return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	// panic("sys_env_set_pgfault_upcall not implemented");
    struct Env *e;
    if (envid2env(envid, &e, 1) != 0)
        return -E_BAD_ENV;
    e->env_pgfault_upcall = func;
    return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.（这交给page_insert去处理）
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	// panic("sys_page_alloc not implemented");
    //
    // XXX 注意使用我们之前构建好的基础设施，不要自己重新实现了。
    if ((uint32_t)va >= UTOP || (uint32_t)va%PGSIZE != 0)
        return -E_INVAL;
    if ((perm&(PTE_P|PTE_U)) != (PTE_P|PTE_U) || (perm&(~PTE_SYSCALL)) != 0) // 短路操作
        return -E_INVAL;
    struct Env *e;
    if (envid2env(envid, &e, 1) != 0)
        return -E_BAD_ENV;
    struct PageInfo *p = page_alloc(ALLOC_ZERO);
    if (!p)
        return -E_NO_MEM;
    if (page_insert(e->env_pgdir, p, va, perm) != 0)
        return -E_NO_MEM;
    return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	// panic("sys_page_map not implemented");
    if ((uint32_t)srcva >= UTOP || (uint32_t)srcva%PGSIZE != 0 || (uint32_t)dstva >= UTOP || (uint32_t)dstva%PGSIZE != 0)
        return -E_INVAL;
    if ((perm&(PTE_P|PTE_U)) != (PTE_P|PTE_U) || (perm&(~PTE_SYSCALL)) != 0)
        return -E_INVAL;
    struct Env *srce, *dste;
    if (envid2env(srcenvid, &srce, 1) != 0 || envid2env(dstenvid, &dste, 1) != 0)
        return -E_BAD_ENV;
    pte_t *pte;
    struct PageInfo *p;
    if ((p=page_lookup(srce->env_pgdir, srcva, &pte)) == NULL)
        return -E_INVAL;
    if (((*pte)&PTE_W) == 0 && (perm&PTE_W) != 0)
        return -E_INVAL;
    if (page_insert(dste->env_pgdir, p, dstva, perm) != 0)
        return -E_NO_MEM;
    return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	// panic("sys_page_unmap not implemented");
    if ((uint32_t)va >= UTOP || (uint32_t)va%PGSIZE != 0)
        return -E_INVAL;
    struct Env *e;
    if (envid2env(envid, &e, 1) != 0)
        return -E_BAD_ENV;
    page_remove(e->env_pgdir, va);
    return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	// panic("sys_ipc_try_send not implemented");
    struct Env *dste;
    struct PageInfo *p;
    pte_t *pte;
    // 第三个参数为0，即不要求curenv就是dste，也不要求curenv是dste的父Env。
    if (envid2env(envid, &dste, 0) != 0)
        return -E_BAD_ENV;
    if (dste->env_ipc_recving == 0)
        return -E_IPC_NOT_RECV;
    // 注意不能在这里就设置dste->env_ipc_recving为0，因为下面还要检查sender是否合法。
    // 调用约定，srcva>=UTOP表示sender不想发送一个页映射。
    if ((uint32_t)srcva < UTOP) {
        if ((uint32_t)srcva%PGSIZE != 0)
            return -E_INVAL;
        if ((perm&(PTE_P|PTE_U)) != (PTE_P|PTE_U) || (perm&(~PTE_SYSCALL)) != 0)
            return -E_INVAL;
        if ((p=page_lookup(curenv->env_pgdir, srcva, &pte)) == NULL)
            return -E_INVAL;
        if ((perm&PTE_W)!=0 && ((*pte)&PTE_W)==0)
            return -E_INVAL;
        if ((uint32_t)(dste->env_ipc_dstva) < UTOP) {
            if (page_insert(dste->env_pgdir, p, dste->env_ipc_dstva, perm) != 0)
                return -E_NO_MEM;
        }
    }

    // the send succeeds
    dste->env_ipc_recving = 0;
    dste->env_ipc_from = curenv->env_id;
    dste->env_ipc_value = value;
    dste->env_ipc_perm = (uint32_t)srcva<UTOP? perm: 0;

    // sched_yield并不会使得返回dste的sys_ipc_recv函数，而会运行env_run，然后进入env_pop_tf，直接恢复dste的Trapframe，
    // 直接返回用户态库函数继续执行。所以想让dste的sys_ipc_recv返回0，只需设置dste的Trapframe的reg_eax即可。
    // 然后调用sys_ipc_recv系统调用的用户态库函数就会从%eax中获取返回值。
    dste->env_tf.tf_regs.reg_eax = 0;
    dste->env_status = ENV_RUNNABLE; // 这样sched_yield就会在某一时刻调度dste运行。

    return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	// panic("sys_ipc_recv not implemented");
    // 调用约定，如果dstva>=UTOP，表示receiver不想接受一个页映射。
    if ((uint32_t)dstva<UTOP && (uint32_t)dstva%PGSIZE!=0)
        return -E_INVAL;

    curenv->env_ipc_recving = 1;
    curenv->env_ipc_dstva = dstva; // datva可能<UTOP也可能>=UTOP，表示receiver想或不想接受一个页映射。
    curenv->env_status = ENV_NOT_RUNNABLE; // 让sched_yield不要调度当前receiver执行，除非sender将receiver标记为ENV_RUNNABLE。
    sched_yield(); // 不会直接返回到这里。
	// return 0;
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	// panic("syscall not implemented");

    // 根据系统调用号分发系统调用。
	switch (syscallno) {
    case SYS_cputs: sys_cputs((char*)a1, a2); return 0;
    case SYS_cgetc: return sys_cgetc();
    case SYS_env_destroy: return sys_env_destroy(a1);
    case SYS_getenvid: return sys_getenvid();
    case SYS_yield: sys_yield(); return 0;
    case SYS_page_alloc: return sys_page_alloc(a1, (void*)a2, a3);
    case SYS_page_map: return sys_page_map(a1, (void*)a2, a3, (void*)a4, a5);
    case SYS_page_unmap: return sys_page_unmap(a1, (void*)a2);
    case SYS_exofork: return sys_exofork();
    case SYS_env_set_status: return sys_env_set_status(a1, a2);
    case SYS_env_set_pgfault_upcall: return sys_env_set_pgfault_upcall(a1, (void*)a2);
    case SYS_ipc_try_send: return sys_ipc_try_send(a1, a2, (void*)a3, a4);
    case SYS_ipc_recv: sys_ipc_recv((void*)a1); // return 0;
    case SYS_env_set_trapframe: return sys_env_set_trapframe(a1, (struct Trapframe*)a2);
	default:
		return -E_INVAL;
	}
}

