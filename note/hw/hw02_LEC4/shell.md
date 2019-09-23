# Homework: shell

## Executing simple commands

## I/O redirection

## Implement pipes

注意<>和|的区别是，前者的一个操作数是文件，后者两个操作数都是可执行程序。

在sh.c中做了注释，直接看代码即可。

## Optional challenge exercises

- Implement lists of commands, separated by ";"
- Implement sub shells by implementing "(" and ")"
- Implement running commands in the background by supporting "&" and "wait"

这些可以参考xv6的sh.c的实现，homework是该实现的简化版，至于`wait`命令，则可认为是shell的内置命令，可参考csapp的shell lab，这里的wait命令似乎没有参数，也就是可以简单地`waitpid(pid, NULL, 0)`等待后台进程执行结束。

