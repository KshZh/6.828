// Concurrent version of prime sieve of Eratosthenes.
// Invented by Doug McIlroy, inventor of Unix pipes.
// See http://swtch.com/~rsc/thread/.
// The picture halfway down the page and the text surrounding it
// explain what's going on here.
//
// Since NENV is 1024, we can print 1022 primes before running out.
// The remaining two environments are the integer generator at the bottom
// of main and user/idle.

#include <inc/lib.h>

unsigned
primeproc(void)
{
	int i, id, p;
	envid_t envid;

	// fetch a prime from our left neighbor
top:
    // 从“左边”的进程收到一个质数p。
	p = ipc_recv(&envid, 0, 0);
	cprintf("CPU %d: %d\n", thisenv->env_cpunum, p);

	// fork a right neighbor to continue the chain
	if ((id = fork()) < 0)
		panic("fork: %e", id);
	if (id == 0)
		goto top;

	// filter out multiples of our prime
	while (1) {
        // 从“左边”继续接受整数。
		i = ipc_recv(&envid, 0, 0);
        // 这里丢弃是p的倍数的数i，因为这意味着i不可能是质数。
        // 只将可能是质数的数i传递给“右边”的进程。
		if (i % p)
			ipc_send(id, i, 0, 0);
	}
}

void
umain(int argc, char **argv)
{
	int i, id;

	// fork the first prime process in the chain
	if ((id = fork()) < 0)
		panic("fork: %e", id);
	if (id == 0)
        // 子进程
		primeproc();

    // 父进程
	// feed all the integers through
    // 从2开始，2是质数，遍历所有整数，将这每个整数发给刚创建的子进程。
    // 可以认为当前父进程“在左边”，其fork的子进程“在右边”。
	for (i = 2; ; i++)
		ipc_send(id, i, 0, 0);
}

