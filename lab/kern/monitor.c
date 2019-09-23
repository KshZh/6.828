// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[5] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Backtrace", mon_backtrace },
    { "continue", "Continue execute program", mon_continue },
    { "stepi", "Execute the next instruction", mon_stepi },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
    // Your code here.
    // entry.S中将%ebp初始化为0，这就是终止条件。
    // The listed eip value is the function's return instruction pointer: the instruction address to which control will return when the function returns.
    // The ebp value indicates the base pointer into the stack used by that function:
    // i.e., the position of the stack pointer just after the function was entered and the function prologue code set up the base pointer.
    size_t ebp = read_ebp(), eip, args[5];
    struct Eipdebuginfo info;
    cprintf("Stack backtrace:\n");
    while (ebp != 0) {
        eip = *(size_t*)(ebp+4);
        for (int i=0; i<5; i++) {
            args[i] = *(size_t*)(ebp+4*(i+2));
        }
        cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n", ebp, eip, args[0], args[1], args[2], args[3], args[4]);
        if(debuginfo_eip(eip, &info) == 0) {
            cprintf("         %s:%d: ", info.eip_file, info.eip_line);
            cprintf("%.*s+%d\n", info.eip_fn_namelen, info.eip_fn_name, eip-(uint32_t)info.eip_fn_addr);
        }
        ebp = *(size_t*)(ebp);
    }
    return 0;
}

int mon_continue(int argc, char **argv, struct Trapframe *tf) {
    // Continue exectuion of current env. 
    // Because we need to exit the monitor, retrun -1 when we can do so
    // Corner Case: If no trapframe(env context) is given, do nothing
    if(tf == NULL) {
        cprintf("No Env is Running! This is Not a Debug Monitor!\n");
        return 0;
    }
    // Because we want the program to continue running; clear the TF bit
    tf->tf_eflags &= ~(FL_TF);
    return -1;
}

int mon_stepi(int argc, char **argv, struct Trapframe *tf) {
    // Continue exectuion of current env. 
    // Because we need to exit the monitor, retrun -1 when we can do so
    // Corner Case: If no trapframe(env context) is given, do nothing
    if(tf == NULL) {
        cprintf("No Env is Running! This is Not a Debug Monitor!\n");
        return 0;
    }
    // Because we want the program to single step, set the TF bit
    tf->tf_eflags |= (FL_TF);
    return -1;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
