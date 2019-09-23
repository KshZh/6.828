	.text

/* Switch from current_thread to next_thread. Make next_thread
 * the current_thread, and set next_thread to 0.
 * Use eax as a temporary register; it is caller saved.
 */
	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */
    // 因为是用户线程，切换后也还在用户代码段和数据段，用户栈也是一样的，
    // 所以段寄存器/选择子%cs, %ds, %ss始终是相同的，不必保存，要保存%eip, “栈指针”。

    // call指令隐含地push %eip
    pushal

    // 切换线程栈
    movl current_thread, %eax
    movl %esp, (%eax) // 将当前%esp保存到旧“线程”的sp中。
    movl next_thread, %eax
    movl (%eax), %esp // 将目标“线程”的sp载入%esp。

    // Make next_thread the current_thread, and set next_thread to 0.
    movl next_thread, %eax
    movl %eax, current_thread
    movl $0, %eax
    movl %eax, next_thread

    popal
	ret				/* pop return address from stack */
    // ret指令隐含地pop %eip
