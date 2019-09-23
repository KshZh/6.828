# Homework: mmap()

When the process accesses the square root table, the mapping does not exist and the kernel passes control to the signal handler code in `handle_sigsegv()`. Modify the code in `handle_sigsegv()` to map in a page at the faulting address, unmap a previous page to stay within the physical memory limit, and initialize the new page with the correct square root values.

```c
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <math.h>

static size_t page_size;

// align_down - rounds a value down to an alignment
// @x: the value
// @a: the alignment (must be power of 2)
//
// Returns an aligned value.
#define align_down(x, a) ((x) & ~((typeof(x))(a) - 1))

// 注意到sqrt表的最大大小比用户地址空间大。
#define AS_LIMIT	(1 << 25) // Maximum limit on virtual memory bytes
#define MAX_SQRTS	(1 << 27) // Maximum limit on sqrt table entries
static double *sqrts;

// Use this helper function as an oracle for square root values.
static void
calculate_sqrts(double *sqrt_pos, int start, int nr)
{
  int i;

  for (i = 0; i < nr; i++) {
    sqrt_pos[i] = sqrt((double)(start + i));
  }
}

static void
handle_sigsegv(int sig, siginfo_t *si, void *ctx)
{
  // Your code here.

  // replace these three lines with your implementation
  uintptr_t fault_addr = (uintptr_t)si->si_addr;
  // printf("oops got SIGSEGV at 0x%lx\n", fault_addr);
  // exit(EXIT_FAILURE);
  static double *p = NULL;
  if (p != NULL) {
    if (munmap(p, 4096) == -1)
        exit(EXIT_FAILURE);
  }
  fault_addr = align_down(fault_addr, 4096);
  p = mmap((void*)fault_addr, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  // 往物理页中填充内容。
  calculate_sqrts(p, (double*)fault_addr-sqrts, 4096/sizeof(double));
}

static void
setup_sqrt_region(void)
{
  struct rlimit lim = {AS_LIMIT, AS_LIMIT};
  struct sigaction act;

  // Only mapping to find a safe location for the table.
  //
  // mmap() creates a new mapping in the virtual address space of the calling process.
  // The starting address for the new mapping is specified in addr.  The length argument specifies
  // the length of the mapping (which must be greater than 0).
  // 
  // On  success,  mmap()  returns  a  pointer  to the mapped area. 
  // 
  // MAP_ANONYMOUS: The mapping is not backed by any file; its contents are initialized to zero.
  // MAP_PRIVATE: Create a private copy-on-write mapping.  Updates to the mapping are not visible to other processes mapping the same file,
  //   and are not carried through to  the  underlying file.
  //
  // mmap创建一个映射，将我们指定的（若不指定则由内核来选择）虚拟地址所在的页映射到一个物理页，
  // 这个物理页是由内核来选择并分配，我们无法指定具体哪个物理页，它的内容可以是一个文件，或者是0值。
  sqrts = mmap(NULL, MAX_SQRTS * sizeof(double) + AS_LIMIT, PROT_NONE,
	       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (sqrts == MAP_FAILED) {
    fprintf(stderr, "Couldn't mmap() region for sqrt table; %s\n",
	    strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Now release the virtual memory to remain under the rlimit.
  if (munmap(sqrts, MAX_SQRTS * sizeof(double) + AS_LIMIT) == -1) {
    fprintf(stderr, "Couldn't munmap() region for sqrt table; %s\n",
            strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Set a soft rlimit on virtual address-space bytes.
  if (setrlimit(RLIMIT_AS, &lim) == -1) {
    fprintf(stderr, "Couldn't set rlimit on RLIMIT_AS; %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Register a signal handler to capture SIGSEGV.
  // 
  // $ man 7 signal
  //    SIGSEGV      11       Core    Invalid memory reference
  act.sa_sigaction = handle_sigsegv;
  act.sa_flags = SA_SIGINFO;
  sigemptyset(&act.sa_mask);
  if (sigaction(SIGSEGV, &act, NULL) == -1) {
    fprintf(stderr, "Couldn't set up SIGSEGV handler;, %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
}

static void
test_sqrt_region(void)
{
  int i, pos = rand() % (MAX_SQRTS - 1);
  double correct_sqrt;

  printf("Validating square root table contents...\n");
  srand(0xDEADBEEF);

  for (i = 0; i < 500000; i++) {
    if (i % 2 == 0)
      pos = rand() % (MAX_SQRTS - 1); // 在sqrt表中随机选择一个位置，可能超出AS_LIMIT。
    else
      pos += 1;
    calculate_sqrts(&correct_sqrt, pos, 1);
    // sqrts[pos]可能触发page fault。
    if (sqrts[pos] != correct_sqrt) {
      fprintf(stderr, "Square root is incorrect. Expected %f, got %f.\n",
              correct_sqrt, sqrts[pos]);
      exit(EXIT_FAILURE);
    }
  }

  printf("All tests passed!\n");
}

int
main(int argc, char *argv[])
{
  page_size = sysconf(_SC_PAGESIZE);
  printf("page_size is %ld\n", page_size);
  setup_sqrt_region();
  test_sqrt_region();
  return 0;
}
```

注意到上面我们是如何根据fault_addr计算出fault_addr指向的元素在数组sqrts中的下标的：

```c
#include <stdio.h>

int main() {
    unsigned a = 0x1234;
    unsigned b = 0x124c;
    printf("%x\n", (int*)a+1); // 0x1238，在该机器上，int类型对象占4字节。
    printf("%x\n", (double*)a+1); // 0x123c，在该机器上，double类型对象占8字节。
    printf("%x\n", (double*)b-(double*)a); // 3
    // printf("%x\n", (char*)b+(char*)a); // 错误，指针相加是没有意义的。

    // 如果有一个数组，
    double d[340];
    // 现知道两个指针，一个数组首地址&d[0]，另一个
    double *p = d+123;
    // 在不知道p是如何计算出来的情况下，怎么知道p指向的元素在数组d中的下标？
    printf("%d\n", p-d); // 123，注意，并不需要除以sizeof(double)。
}
```

