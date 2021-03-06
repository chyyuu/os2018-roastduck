# Lab 5 Report

## 练习1

`load_icode`中，需要实现的是设置`trapframe`的部分。`trapframe`中设置的内容是当进程返回（实际是进入）用户态后的上下文，所以代码段`tf_cs`设置为用户代码段`USER_CS`，数据段`tf_ds`、扩展段`tf_es`和栈段`tf_ss`都设置为用户数据段`USER_DS`，栈顶指针设置为用户栈的最高地址`USTACKTOP`，程序计数器`eip`设置为ELF中指定的程序入口，并设置标记为`IF`开中断。

### 请在实验报告中描述当创建一个用户态进程并加载了应用程序后，CPU是如何让这个应用程序最终在用户态执行起来的。即这个用户态进程被ucore选择占用CPU执行（RUNNING态）到具体执行应用程序第一条指令的整个经过

“从进程A中创建进程B，并于进程B中执行应用程序”这一过程，是通过先`fork`再`execve`实现的。其中，`fork`创建新进程，然后`execve`将新进程的上下文替换为所执行的应用程序的上下文。此处分为两种情况：

- 若进程A执行的是用户态程序（例如sh），进程A通常会调用系统调用`fork`，先进入内核态执行`fork`函数，产生的新进程返回到用户态，接着再调用系统调用`exec`，再次进入内核态执行`execve`函数，最后再次返回用户态执行用户程序；
- 若进程A执行的是内核态程序，则上述过程被简化。进程A直接执行`fork`函数，产生的新进程不返回用户态，而是直接调用系统调用`exec`，异常服务例程执行`execve`函数，最后“返回”用户态执行用户程序。

具体到Lab5，则只涉及后一种情况。母进程`init`执行`fork`后产生新进程，新进程的程序计数器被设置在函数`user_main`处，等待执行。当调度器`schedule`函数选择执行这一新进程，并使用`proc_run`函数将CPU切换到该进程的上下文时，新进程开始占用CPU执行函数`user_main`。

函数`user_main`调用系统调用`exec`，异常服务根据异常编号和参数，执行函数`do_execve`。

`do_execve`中，首先暂时将进程的页表设置为内核页表，按所要运行的程序设置好进程名，然后调用`load_icode`加载被执行的应用程序。`load_icode`执行如下流程：

- 建立并初始化该进程专用的内存管理数据结构、初始化页表；
- 读取ELF格式的应用程序，按ELF头的说明，逐页将各段放置在虚存中的指定位置上，并设置好页权限；
- 建立该进程的栈；
- 通过设置CR3寄存器，切换使用新页表；
- 设置上述`trapframe`为新加载的应用程序入口处的上下文。

设置好`trapframe`后，系统调用看起来就像是从被加载的用户程序调用的一样。异常处理程序逐级返回，首先μCore在`__trapret`标签处“恢复”（实为加载）了软件负责恢复的寄存器，然后执行指令`iret`，CPU“恢复”（实为加载）了硬件负责恢复的寄存器。最终“返回”到新的应用程序入口处。

## 练习2

本练习中`copy_range`需要实现的部分只是获取源页和目的地页的虚拟地址，再通过`memcpy`复制其中的所有内容，最后在页表中建立线性地址到物理地址的映射即可。

### 简要说明如何设计实现“Copy on Write 机制”

在`copy_range`，并不复制物理页，而是复制页表（将新页表指向原物理页），并将物理页的引用计数加一。在新旧页表上，均需要置写入位为零，以在写入时产生异常。在`do_pgfault`中检查“某页写入位为零但尝试写入”的异常，分以下两种情况处理：

- 若相应物理页的引用计数大于1，说明需要进行Copy on Write。此时调用`alloc_page`分配一个新的物理页，像`copy_range`原来做的那样复制所有内容到新物理页，并更新页表指向新物理页，最后将原物理页的引用计数减一；
- 若相应物理页的引用计数等于1，说明Copy on Write已经进行完毕，将页表中的写入位置1即可。

当然，每次更新页表后需要刷新TLB。

## 练习3

本节中，我首先给出进程的执行状态生命周期图如下，然后再回答关于`fork/exec/wait/exit`的分析：

```
                                          +-- RUNNING <-+
                                          |    proc_run |
         alloc_proc          wakeup_proc  v proc_run    | do_wait 
创建进程 -----------> UNINIT --------------+-> RUNNABLE -+--------> SLEEPING -+
                                                 |   ^                       |
                   父进程释放资源         do_exit |   |                       | wakeup_proc
           中止进程 <----------- ZOMBIE <---------+   +-----------------------+
```

注1：第一个进程`idle`是在函数`init_proc`中手工构造的，直接进入RUNNING状态，而不遵循上述流程；

注2：进程管理数据结构中并不区分RUNNING状态与RUNNABLE状态，每次`proc_run`被执行时，会切换到某个RUNNALBE的进程，使其占用CPU，此进程的状态为RUNNING状态，原来占用CPU时间的进程回到RUNNABLE状态。

- `fork`（于函数`do_fork`执行）在保持当前进程运行状态为RUNNING不变的情况下，通过`alloc_proc`创建新的进程，此时新进程状态为UNINIT。`fork`对其进行初始化后，通过调用`wakeup_proc`置新的进程的运行状态为RUNNABLE；
- `exec`（于函数`do_execve`执行）保持当前进程运行状态为RUNNING不变，只是设置进程的上下文。`exec`执行的具体事项见练习1的分析；
- `wait`（于函数`do_wait`执行）会检查子进程的状态，若有ZOMBIE状态的子进程则释放该子进程的资源并返回，保持自身RUNNING状态不变；否则进入SLEEPING状态等待子进程唤醒，唤醒后再释放子进程资源；
- `exit`（于函数`do_exit`执行）会释放自己的大部分资源，将所有子进程的父进程设为`init`进程，通过`wakeup_proc`唤醒SLEEPING状态的父进程（若有）进入RUNNABLE态，然后自身进入ZOMBIE态。

## 总结

### 完成实验后，请分析ucore_lab中提供的参考答案，并请在实验报告中说明你的实现与参考答案的区别

由于本实验需要实现的代码较少，故没有区别。

### 重要知识点

本实验涉及的原理课知识点有：用户进程管理、进程状态模型、包括进程的创建、加载、切换、等待与退出的整个生命周期（以阅读或编写代码的形式体现，各有侧重）。本实验还涉及：执行ELF格式的二进制代码（实验中仅需阅读）、进程复制、Copy on Write。

本实验未涉及的知识点有：进程与线程的区别。
