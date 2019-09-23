# Homework: bigger files for xv6

This limit comes from the fact that an xv6 inode contains 12 "direct" block numbers and one "singly-indirect" block number, which refers to a block that holds up to 128（一个block no占4字节） more block numbers, for a total of 12+128=140. You'll change the xv6 file system code to support a "doubly-indirect" block in each inode, containing 128 addresses of singly-indirect blocks, each of which can contain up to 128 addresses of data blocks. The result will be that a file will be able to consist of up to 16523（11+128+128*128） sectors (or about 8.5 megabytes).

## Your Job

Modify `bmap() `so that it implements a doubly-indirect block, in addition to direct blocks and a singly-indirect block. You'll have to have only 11 direct blocks, rather than 12, to make room for your new doubly-indirect block; you're not allowed to change the size of an on-disk inode. The first 11 elements of `ip->addrs[]` should be direct blocks; the 12th should be a singly-indirect block (just like the current one); the 13th should be your new doubly-indirect block.

You don't have to modify xv6 to handle deletion of files with doubly-indirect blocks.

If all goes well, `big` will now report that it can write 16523 sectors. It will take `big` a few dozen seconds to finish.

param.h:

```c
#define FSSIZE       20000  // size of file system in blocks
```

fs.h:

```c
#define NDIRECT 11
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT*NINDIRECT)
uint addrs[NDIRECT+2];   // Data block addresses
```

file.h:

```c
uint addrs[NDIRECT+2];
```

fs.c:

```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev); // 分配indirect block。
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data; // indicate block是一个block no数组。
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev); // 分配content block。
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  bn -= NINDIRECT;
  if (bn < NINDIRECT*NINDIRECT) {
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT+1]) == 0)
      ip->addrs[NDIRECT+1] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr); // 读入二级indirect block，这是一个一级indirect block的block no数组，有128即NINDIRECT个一级indirect block。
    a = (uint*)bp->data;
    if ((addr = a[bn/NINDIRECT]) == 0) {
      a[bn/NINDIRECT] = addr = balloc(ip->dev);
      log_write(bp);
    }
    struct buf *bp2 = bp;
    // 读入bn所在的一级indirect block。
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data; // 一级indicate block也是一个block no数组。
    if((addr = a[bn%NINDIRECT]) == 0){
      a[bn%NINDIRECT] = addr = balloc(ip->dev); // 分配content block。
      log_write(bp);
    }
    brelse(bp2);
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}
```

运行结果：

```
$ big
.....................................................................................................................................................................
wrote 16523 sectors
done; ok
```

当然，这并不是对doubly-indirect的全部实现，可能还需要改写itrunc等。

