# Lab 4 Report

## 练习1

实现这个练习只需要初始化`proc_struct`中的成员：`state`应赋值为`PROC_UNINIT`，`pid`应赋值为-1表示未分配，`cr3`赋值为当前内核页目录表`boot_cr3`，其他值均赋值为零即可。

### 请说明`proc_struct`中`struct context context`和`struct trapframe *tf`成员变量含义和在本实验中的作用是什么

`context`用于在进程切换时保存当前进程执行的上下文，它的各个成员存储了一个进程的程序计数器`eip`、栈顶指针`esp`，和通用寄存器`ebx`、`ecx`、`edx`、`esi`、`edi`、`ebp`中的值（`ebp`按实际情况也可视作专用寄存器）。当发生切换时，μCore先保存CPU各寄存器的值到原进程的`context`，然后将新进程的`context`加载到CPU各寄存器中。由于进程切换时所有进程都位于内核态（如果用户态进程要进行切换，首先要通过异常/中断切换到内核态），所以段寄存器、页表等都不会变化，不必保存。

`tf`则用于保存一个进程在发生中断时的上下文，各成员含义如下

- `tf_regs`保存了各通用寄存器的值；
- `tf_gs`、`tf_fs`、`tf_es`、`tf_ds`、`tf_cs`、`tf_ss`保存了各段选择子的值；
- `tf_trapno`保存了中断/异常编号，`tf_err`保存了某些中断/异常的具体原因；
- `tf_eip`保存返回地址；
- `tf_eflags`保存了各标志位的值；
- `tf_esp`保存栈顶指针；
- `tf_padding0~5`无实际含义，仅用于将上述各成员按32位对齐。

发生中断时，硬件和μCore会将当前进程的上下文保存到`tf`中，并在从异常返回时恢复上述值。其中`tf_cs`、`tf_ss`、`tf_trapno`、`tf_err`、`tf_eip`、`tf_eflags`、`tf_esp`是由硬件负责的，`tf`的其余成员是由μCore负责的。此外，`tf_esp`和`tf_ss`仅会在用户态进程发生中断时被保存。由于用户态进程要进行切换时，首先要通过异常/中断切换到内核态，所以`tf`也间接地在本实验发挥作用。

## 练习2

练习2流程如下：

- 调用`alloc_proc`，首先获得一块用户信息块；
- 调用`setup_kstack`，为进程分配一个内核栈；
- 调用`copy_mm`，复制原进程的内存管理信息到新进程；
- 调用`copy_thread`，复制原进程上下文到新进程；
- 调用`get_pid`，获取进程号；
- 增加总进程计数，将新进程添加到进程列表和哈希表；
- 调用`wakeup_proc`，唤醒新进程；
- 返回新进程号。

### 请说明ucore是否做到给每个新fork的线程一个唯一的id？请说明你的分析和理由

可以做到分配唯一ID。分配ID是`get_pid`函数实现的，它通过`last_pid`和`next_safe`维护了一段连续开区间`(last_pid, next_safe)`，此区间中的ID是没有被任何进程使用的。当需要分配新ID时，默认分配`last_pid + 1`为新ID，这样可以使大多数情况下分配ID是O(1)时间的，也保证新分配的ID一般是逐次递增的。而当`last_pid + 1 >= next_safe`时，就不能分配`last_pid + 1`，因为此ID已被占用，`get_pid`则会遍历进程列表，找出下一段没有进程占用的连续区间`(last_pid, next_safe)`，再进行分配。显然此流程可以保证各进程的ID是不同的。

## 练习3

`proc_run`执行流程如下：

- 关中断；
- 更新表示当前进程的全局变量`current`；
- 将栈顶寄存器`esp`切换到新进程的栈顶；
- 加载新进程的页目录表/页表；
- 保存旧进程的上下文到旧进程的`context`成员变量，并从新进程的`context`成员变量加载上下文。具体要保存/加载的内容如练习1所述；
- 恢复中断。

### 在本实验的执行过程中，创建且运行了几个内核线程？

本实验中，创建了两个进程：`idle`和`init`。

创建`idle`进程时，将`idle`进程的上下文设为了当前运行的上下文，实际上相当于将当前正在运行的上下文定义为`idle`进程，而非“创建并运行”了`idle`进程。所以`idle`进程被创建后会继续执行内核初始化工作。初始化完毕后，`idle`进程用于调度其他进程，每次`idle`执行时，都会找到下一个需要被执行的进程，然后马上切换到该进程。

`init`是从`idle`进程fork出来的进程，在本实验中仅输出了一些字符串。

### 语句`local_intr_save(intr_flag);....local_intr_restore(intr_flag);`在这里有何作用？请说明理由

`local_intr_save(intr_flag)`将当前的中断使能与否状态保存到`intr_flag`中，然后关闭了中断。`local_intr_restore(intr_flag)`则是恢复`intr_flag`中记录的中断使能情况。这两条语句保证了：

1. 执行这两条语句之间的代码时不会发生中断，使该部分代码形成临界区，避免各进程在异步执行时产生错误；
2. 保证执行完临界区代码后，中断使能可以恢复到一开始的状态。

## 总结

### 完成实验后，请分析ucore_lab中提供的参考答案，并请在实验报告中说明你的实现与参考答案的区别

- `do_fork`中，参考答案设置了`proc->parent = current`而我没有设置，但这实际上是Lab5的任务；
- `do_fork`中，参考答案在将新进程加入进程列表和哈希表时关闭了中断，而我没有设置，在某些特定的情况下可能出现并发访问冲突，应该改进。

### 重要知识点

本实验涉及的原理课知识点有：内核线程、进程控制块、进程创建、进程切换。本实验还涉及关于`trapframe`的相关知识，这是第三讲异常与中断的内容。

本实验未涉及的原理课知识点有：用户进程、进程状态模型、进程的等待与退出、进程和线程的区别，其中前三点将在下一个实验中涉及。
