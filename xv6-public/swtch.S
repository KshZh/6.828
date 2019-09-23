# Context switch
#
#   void swtch(struct context **old, struct context *new);
# 
# Save the current registers on the stack, creating
# a struct context, and save its address in *old.
# Switch stacks to new and pop previously-saved registers.

.globl swtch
swtch:
  # C calling convention将实参列表从后往前推入栈中。
  movl 4(%esp), %eax # 实参1，加4是跳过栈中的返回地址
  movl 8(%esp), %edx # 实参2

  # Save old callee-saved registers（在被切换前，在当前process的内核栈中建立context）
  # pushl %eip由call指令在跳转前隐含地执行，显然推入栈中的%eip指向call指令后的一条指令，而不是当前的%eip值。
  pushl %ebp
  pushl %ebx
  pushl %esi
  pushl %edi

  # Switch stacks
  movl %esp, (%eax)
  movl %edx, %esp # 切换到当前CPU的调度线程的内核栈。

  # Load new callee-saved registers（将切换到的process之前被切换前在其内核栈中建立的context恢复）
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret # 隐含地popl %eip
