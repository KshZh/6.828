// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

extern void _pgfault_upcall(void);

// 由于只需读（且只能读）pte，所以不需要返回指针。
static pte_t get_pte(void *va) {
    uint32_t a = (uint32_t)va;
    if ((uvpd[PDX(a)]&PTE_P) == 0)
        // XXX 这里要注意一个点，不能返回-1，-1的补码表示为全1，这样的话caller将返回的pte and 任何标志如PTE_P都会得到非0。
        // 更重要的一个点是，pte_t被定义为无符号数，这样的话-1会被转换为很大的无符号数，会逃过caller的`pte<0`的检查。
        // 返回0符合逻辑，因为PTE_P位被复位了，表示该pte不存在。
        return 0;
    return uvpt[PGNUM(a)];
    // 在JOS中，V设置为0x3BD。
    // PDX=V, PTX!=V，这样的地址通过翻译可以form 4MB的物理地址空间。
    // uvpt, PDX=V, PTX=0，这样uvpt通过翻译得到第0个页表的物理起始地址。
    // uvpt[x]，等价于*(pte_t*)(uvpt+x<<2)。
    // 若x<1024，则uvpt[x]可以索引第0个页表中的pte。
    // 若x>=1024，则权大于等于1024的位会加到uvpt的PTX部分，从而从页目录中选中不同的页表，权小于1024的位则索引选中的页表中的pte。
    // 也就是可以认为，uvpt构成了“连续”的1024个页表的数组，此时我们只需要用VPN直接索引即可得到va对应的pte。
}

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
    pte_t pte = get_pte(addr);
    // pgfault() checks that the fault is a write (check for FEC_WR in the error code) and that the PTE for the page is marked PTE_COW. If not, panic.
    if(!(err&FEC_WR) || !(pte&PTE_COW)) {
        cprintf("[%08x] user fault va %08x ip %08x\n", sys_getenvid(), addr, utf->utf_eip);
        panic("!(err&FEC_WR) || !(pte&PTE_COW)");
    }

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
    // pgfault() allocates a new page mapped at a temporary location and copies the contents of the faulting page into it.
    // Then the fault handler maps the new page at the appropriate address with read/write permissions, in place of the old read-only mapping.
    // 注意，pgfault()新分配并映射的page不被标记为COW，否则fault恢复重新执行那条导致page fault的指令时，
    // 因为新分配并映射的page也是COW，就又会进入pgfault()，无限循环，直到free page被用完。
	if ((r = sys_page_alloc(0, PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);
    memcpy(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	if ((r = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);
	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("sys_page_unmap: %e", r);

	// panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
int
duppage(envid_t envid, unsigned vpn)
{
	int r;
    void *va = (void*)(vpn<<PTXSHIFT);

	// LAB 4: Your code here.
	// panic("duppage not implemented");
    //
    // 一个vpn对应一个pte，描述一个4KB的虚拟页->物理页映射。
    pte_t pte = get_pte(va);
    if (pte==0 || !(pte&PTE_P))
	    // panic("pte does not exist");
        return -E_INVAL;
    if ((pte&(PTE_W|PTE_COW)) != 0) {
        if ((r=sys_page_map(0, va, envid, va, PTE_U|PTE_P|PTE_COW)) != 0)
	        panic("sys_page_map fails: %e", r);
        if ((r=sys_page_map(0, va, 0, va, PTE_U|PTE_P|PTE_COW)) != 0)
	        panic("sys_page_map fails: %e", r);
    } else {
        if ((r=sys_page_map(0, va, envid, va, PTE_U|PTE_P)) != 0)
	        panic("sys_page_map fails: %e", r);
    }
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	// panic("fork not implemented");
	envid_t envid;
    unsigned vpn;
	int r;
	extern unsigned char end[];

    // Set up our page fault handler appropriately.
    set_pgfault_handler(pgfault);

	envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0; // 子进程返回0，表示当前进程
	}

	// We're the parent.

    // 为子进程分配异常栈
    if((r=sys_page_alloc(envid, (void*)(UXSTACKTOP-PGSIZE), PTE_W|PTE_U|PTE_P)) < 0)
	    panic("sys_page_alloc fails: %e", r);
    
    // Copy our address space and page fault handler setup to the child.
    // 为子进程设置page fault回调处理函数的入口地址，注意入口地址是lib/pfentry.S:_pgfault_upcall，
    // 它会转而调用真正的处理函数_pgfault_handler，然后恢复寄存器状态返回。
    // 这里我们不需要再设置_pgfault_handler了，因为上面父进程调用用户态库函数set_pgfault_handler设置了
    // 全局变量_pgfault_handler，该变量位于用户内存中，而子进程会复制同样的内核映射，由于COW，所以子进程也可以
    // 看得到自己的_pgfault_handler已经被设置和父进程一样了。所以这里只需要设置子进程独立的Env结构体的env_pgfault_upcall成员即可。
    sys_env_set_pgfault_upcall(envid, _pgfault_upcall);

    // 从0到UTOP之间的页，若父进程有映射的话，就复制给子进程。复制的细节由duppage考虑和实现。
    for(int i=0; i<PGNUM(UTOP); i++) {
        // 跳过用户异常栈
        if(i == PGNUM(UXSTACKTOP-PGSIZE))
            continue;
        duppage(envid, i);
    }
    // 用户栈也是copy on write的。

	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);

    return envid; // 父进程返回子进程id
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
