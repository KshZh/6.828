# Homework: Threads and Locking

```c
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

#define SOL
#define NBUCKET 5
#define NKEYS 100000

struct entry {
  int key;
  int value;
  struct entry *next;
};
struct entry *table[NBUCKET]; // 有NBUCKET个链表，链表不带哨兵结点
int keys[NKEYS];
int nthread = 1;
volatile int done; // 让编译器不将done缓存在寄存器上


double
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void
print(void)
{
  int i;
  struct entry *e;
  for (i = 0; i < NBUCKET; i++) {
    printf("%d: ", i);
    for (e = table[i]; e != 0; e = e->next) {
      printf("%d ", e->key);
    }
    printf("\n");
  }
}

static void 
insert(int key, int value, struct entry **p, struct entry *n)
{
  struct entry *e = malloc(sizeof(struct entry));
  e->key = key;
  e->value = value;
  e->next = n;
  *p = e; // 头插法
}

static 
void put(int key, int value)
{
  int i = key % NBUCKET; // 做模运算决定key插入到NBUCKET个链表中的哪一个
  insert(key, value, &table[i], table[i]);
}

static struct entry*
get(int key)
{
  // 从key本应该映射到的那个链表中寻找值为key的entry
  struct entry *e = 0;
  for (e = table[key % NBUCKET]; e != 0; e = e->next) {
    if (e->key == key) break;
  }
  return e;
}

static void *
thread(void *xa)
{
  long n = (long) xa; // id为n，处理keys[n*b]开始的b个元素
  int i;
  int b = NKEYS/nthread; // 当前线程要处理的KEYS数
  int k = 0;
  double t1, t0;

  //  printf("b = %d\n", b);
  t0 = now();
  for (i = 0; i < b; i++) {
    // printf("%d: put %d\n", n, b*n+i);
    put(keys[b*n + i], n);
  }
  t1 = now();
  printf("%ld: put time = %f\n", n, t1-t0);

  // Should use pthread_barrier, but MacOS doesn't support it ...
  __sync_fetch_and_add(&done, 1);
  while (done < nthread) ;
  // barrier

  t0 = now();
  for (i = 0; i < NKEYS; i++) {
    struct entry *e = get(keys[i]);
    if (e == 0) k++;
  }
  t1 = now();
  printf("%ld: get time = %f\n", n, t1-t0);
  printf("%ld: %d keys missing\n", n, k);
  return NULL;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);
  assert(NKEYS % nthread == 0); // 要求NKEYS能被线程数整分
  for (i = 0; i < NKEYS; i++) {
    keys[i] = random();
  }
  t0 = now();
  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    // 主线程等待回收子线程
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();
  printf("completion time = %f\n", t1-t0);
}
```

问题在于多线程put时，如果操作的是同一个链表，比如两个不同的key，但`i = key % NBUCKET`相同，则两个线程都调用了`insert(key, value, &table[i], table[i])`，其中i虽是不同对象，但值相同。这就会导致链表i中只会插入其中一个结点，漏了一个结点。

> called a "lost update"; example of a "race"
> race = concurrent accesses; at least one write

改进的一个方法就是**用锁进行同步，让后面的insert调用能看到前面insert造成的对table[i]的修改**。

```c
pthread_mutex_t lock;     // declare a lock
pthread_mutex_init(&lock, NULL);   // initialize the lock
pthread_mutex_lock(&lock);  // acquire lock
pthread_mutex_unlock(&lock);  // release lock

static 
void put(int key, int value)
{
  int i = key % NBUCKET; // 做模运算决定key插入到NBUCKET个链表中的哪一个
  pthread_mutex_lock(&lock);  // acquire lock
  insert(key, value, &table[i], table[i]);
  pthread_mutex_unlock(&lock);  // release lock
}
```

**另一个思路是维护一个锁数组，每一个bucket对应一个锁，这样一定程度可以提升性能，不必多个线程总是抢占一个锁。**

> **then they likely hold different locks,**
> **so they can execute in parallel -- get more work done.**

然后看到代码中似乎没有对全局变量table的初始化，难道不怕其中的指针是未定义的值而不是NULL，从而导致遍历链表时找不到结束条件吗？

其实，注意到，table是什么变量？全局变量，而且是未初始化的全局变量。也就是table会被放入.bss节中，由加载器运行时对.bss节中的变量分配内存并初始化为0。所以其实table会默认初始化为0，对指针来说也就是NULL。

```
k@ubuntu:~/6.828/hw06$ objdump -t a.out | grep bss
0000000000202020 l    d  .bss	0000000000000000              .bss
0000000000202028 l     O .bss	0000000000000001              completed.7697
0000000000202040 g     O .bss	0000000000000028              lock
0000000000202068 g     O .bss	0000000000000004              done
0000000000202080 g     O .bss	0000000000000028              table
0000000000263b40 g       .bss	0000000000000000              _end
0000000000202014 g       .bss	0000000000000000              __bss_start
00000000002020c0 g     O .bss	0000000000061a80              keys
0000000000202020 g     O .bss	0000000000000008              stderr@@GLIBC_2.2.5
```

第一列是链接地址，第4列是大小，可以看到table有5个指针，即0x28字节。

**.bss节中的变量并不占文件的磁盘空间，也就是可执行文件中，.bss节中的变量只是一个符号而已，只有加载到内存中运行时，才会有对应的实体/对象**。

C程序中未初始化的全局和静态变量，和被初始化为0的全局和静态变量会被记录到ELF头中的.bss节中，只记录对象的元数据，如链接地址、对象大小等，然而不会在文件中实际分配对象。

而已初始化（不为0）的全局和静态变量，则记录到ELF头的.data节中，不仅记录对象的元数据，还在文件中分配磁盘空间存放这些对象。

如果使用objdump的选项`  -s, --full-contents      Display the full contents of all sections requested`，会看到.data节才有内容，.bss节没有显示出来。

