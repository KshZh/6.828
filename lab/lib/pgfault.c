// User-level page fault handler support.
// Rather than register the C page fault handler directly with the
// kernel as the page fault handler, we register the assembly language
// wrapper in pfentry.S, which in turns calls the registered C
// function.

#include <inc/lib.h>


// Assembly language pgfault entrypoint defined in lib/pfentry.S.
extern void _pgfault_upcall(void);

// Pointer to currently installed C-language pgfault handler.
// 汇编入口函数lib/pfentry.S会调用这个函数指针指向的函数。
void (*_pgfault_handler)(struct UTrapframe *utf);

//
// Set the page fault handler function.
// If there isn't one yet, _pgfault_handler will be 0.
// The first time we register a handler, we need to
// allocate an exception stack (one page of memory with its top
// at UXSTACKTOP), and tell the kernel to call the assembly-language
// _pgfault_upcall routine when a page fault occurs.
//
void
set_pgfault_handler(void (*handler)(struct UTrapframe *utf))
{
	int r;

	if (_pgfault_handler == 0) {
		// First time through!
		// LAB 4: Your code here.
		// panic("set_pgfault_handler not implemented");
        // 用户态库函数借助系统调用实现功能。
        sys_page_alloc(0, (void*)(UXSTACKTOP-PGSIZE), PTE_U|PTE_W|PTE_P);
        // 注意这里不是设置handler，要设置汇编入口_pgfault_handler，让_pgfault_handler调用真正的进程注册的page fault handler，然后做一些恢复工作，返回进程继续执行。
        sys_env_set_pgfault_upcall(0, _pgfault_upcall);
	}

	// Save handler pointer for assembly to call.
	_pgfault_handler = handler;
}
