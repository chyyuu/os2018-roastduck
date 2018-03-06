# Lab 1 Report

## 练习1

### 1. 操作系统镜像文件ucore.img是如何一步一步生成的？

#### a. 将原代码编译为“.o”可重定位文件。

`ucore.img`中使用到的源码来源于`kern/`、`boot/`和`libs/`三个文件夹。其中`kern/`中是内核，`boot/`中是bootloader，内核和bootloader独立编译，最后置于`ucore.img`的不同位置。`libs/`中是内核和bootloader中都需要复用的公共代码，内核中使用了`libs/`中定义的大部分内容，而bootloader只使用了`libs/`中定义的少许宏和别名，所以编译`kern/`和`boot/`文件夹时都需将`libs/`添加到头文件文件夹，但从`libs/`编译产生的可重定位文件只需与内核链接在一起，而不用和bootloader链接在一起，其所使用的编译参数也是与内核一致的。

本Makefile中大量使用了函数来添加具体的编译规则。其中`cc_compile`函数接受源文件、编译器、编译选项、头文件文件夹四个参数，通过`eval`Makefile语句，添加编译该源文件到“.o”文件的规则。编译内核（`kern/`和`libs/`）时，Makefile中进一步定义了`add_files_cc`函数，确定了编译参数`CFLAGS`如下：

- `-fno-builtin`：不为内建函数产生特殊的优化代码；
- `-Wall`：开启所有警告；
- `-ggdb`：产生针对gdb的调试用符号表；
- `-m32`：设置目标机器为32位机器；
- `-gstabs`：产生stabs格式的调试信息；
- `-nostdinc`：不在默认文件夹中搜索头文件；
- `-fno-stack-protector`：不产生用于嗅探栈溢出的保护代码。

按以上参数，分别将`kern/`和`libs/`中的源文件编译到`obj/kern/`和`obj/libs/`：

```makefile
$(call add_files_cc,$(call listf_cc,$(KSRCDIR)),kernel,$(KCFLAGS))
$(call add_files_cc,$(call listf_cc,$(LIBDIR)),libs,)
```

其中`KCFLAGS`是编译`kern/`时需要额外搜索的头文件文件夹。

编译`boot/`时则增加了`-Os`选项，令编译器尽量压缩目标文件的大小。将`boot/`中的源文件编译到`obj/boot/`：

```makefile
$(foreach f,$(bootfiles),$(call cc_compile,$(f),$(CC),$(CFLAGS) -Os -nostdinc))
```

#### b. 链接“.o”可重定位文件

如前所述，首先将`obj/kern/`中和`obj/libs/`中的“.o”文件链接为`kernel`目标文件（以下为节选代码）：

```makefile
KOBJS   = $(call read_packet,kernel libs)
kernel = $(call totarget,kernel)
$(kernel): tools/kernel.ld
$(kernel): $(KOBJS)
    $(V)$(LD) $(LDFLAGS) -T tools/kernel.ld -o $@ $(KOBJS)
```

其中连接参数`LDFLAGS`定义如下：

```makefile
LDFLAGS := -m $(shell $(LD) -V | grep elf_i386 2>/dev/null)
LDFLAGS += -nostdlib
```

`-m 目标机器`参数设置目标机器，`-nostdlib`参数设置不链接标准库。

链接时使用的具体规则定义于`tools/kernel.ld`文件。

同理，将`obj/boot/`中的“.o”文件链接为`bootblock`目标文件（以下为节选代码）：

```makefile
bootblock = $(call totarget,bootblock)
$(bootblock): $(call toobj,$(bootfiles)) | $(call totarget,sign)
    $(V)$(LD) $(LDFLAGS) -N -e start -Ttext 0x7C00 $^ -o $(call toobj,bootblock) # ①
    @$(OBJCOPY) -S -O binary $(call objfile,bootblock) $(call outfile,bootblock) # ②
    @$(call totarget,sign) $(call outfile,bootblock) $(bootblock)                # ③
```

如①处，链接时参数如下：

- `-N`：设置代码段和数据段均可读写；
- `-e start`：设置`start`为程序入口；
- `-Ttext 0x7C00`：设置代码段起始地址为`0x7C00`。

链接完成后的文件仍是ELF格式，但bootloader是要被直接执行的，所以还需将其转换为只包含机器指令的格式。如②处，使用`objcopy`完成此操作。`-S`参数表示不保留重定向信息和符号表，`-O binary`参数表示目标格式为二进制机器指令。

由于bootblock是作为磁盘主引导扇区的形式存在的，所以还需在③处使用`tools/sign`工具将其补全到512字节，并置最后两个字节为`0x55 0xAA`标记主引导扇区结束。其中`tools/sign`在之前编译生成，但它是在编译器所在机器上运行的编译工具，而不是ucore的组成部分。

#### c. 将上述文件写入`ucore.img`

```makefile
UCOREIMG    := $(call totarget,ucore.img)
$(UCOREIMG): $(kernel) $(bootblock)
    $(V)dd if=/dev/zero of=$@ count=10000
    $(V)dd if=$(bootblock) of=$@ conv=notrunc
    $(V)dd if=$(kernel) of=$@ seek=1 conv=notrunc
```

产生`ucore.img`分三步进行：

1. 写入10000字节的0，给后续写入预留空间；
2. 将`bootblock`写入镜像起始处（第一个扇区），其中`conv=notrunc`参数表示不对写入内容进行截断；
3. 将内核写入镜像的第二个扇区开始的若干个扇区（`seek=1`参数表示跳过第一个扇区）。

### 2. 一个被系统认为是符合规范的硬盘主引导扇区的特征是什么？

如1.b所述，主引导扇区长度需为恰好512字节，并且最后两个字节为`0x55 0xAA`。

## 练习2

### 1. 从CPU加电后执行的第一条指令开始，单步跟踪BIOS的执行

若直接执行`make debug`可以即可使用gdb调试，但第一处断点将被设置在`kern_init`而不是第一条指令。为了从第一条指令开始即单步跟踪，可以仿照Makefile中指令执行：

```sh
qemu-system-i386 -S -s -serial mon:stdio -hda bin/ucore.img -nographic
gdb bin/kernel
```

然后在gdb中执行下述指令以开始调试

```
target remote :1234
set arch i8086
```

正常情况下，可通过`layout regs`打开寄存器查看窗口、通过`layout asm`查看指令（需通过`fs CMD`使焦点重新回到命令输入窗口）。然而，指令窗口显示的指令地址是错误的。80368启动后执行地第一条指令位于`0xfffffff0`（由于A20被关闭，实际与`0xffff0`等价），但gdb误以为位于`0xfff0`。可以通过以下命令手动查看程序的第一条指令：

```
x /2i 0xfffffff0
```

结果为：

```
0xfffffff0:  ljmp   $0xf000,$0xe05b
0xfffffff5:  xor    %dh,0x322f
```

通过`si`单步执行一条命令，此时在寄存器窗口观察到`eip`变为`0xe05b`，即跳转到`0xffffe05b`，即成功进行了跳转。

### 2. 在初始化位置0x7c00设置实地址断点,测试断点正常

通过`b *0x7c00`设置断点，再通过`c`恢复运行，程序即运行到`0x7c00`位置。通过指令窗口，可以查看此时待运行的指令为（`cs=0`时gdb的指令窗口显示无误）：

```
   ┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
B+>│0x7c00  cli                                                                                                                       │
   │0x7c01  cld                                                                                                                       │
   │0x7c02  xor    %ax,%ax                                                                                                            │
   │0x7c04  mov    %ax,%ds                                                                                                            │
   │0x7c06  mov    %ax,%es                                                                                                            │
   │0x7c08  mov    %ax,%ss                                                                                                            │
   │0x7c0a  in     $0x64,%al                                                                                                          │
   │0x7c0c  test   $0x2,%al                                                                                                           │
   │0x7c0e  jne    0x7c0a                                                                                                             │
   │0x7c10  mov    $0xd1,%al                                                                                                          │
   │0x7c12  out    %al,$0x64                                                                                                          │
   └──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 3. 从0x7c00开始跟踪代码运行,将单步跟踪反汇编得到的代码与`bootasm.S`和`bootblock.asm`进行比较

对比上述反汇编与`bootasm.S`和`bootblock.asm`，可以看出是一致的（不影响语义的ANSI汇编的后缀被省略了）。

### 4. 自己找一个bootloader或内核中的代码位置，设置断点并进行测试

通过`b kern_init`在`kern_init`函数处设置断点，然后通过`c`继续执行程序，程序即运行到`kern_init`处。通过`l`可查看此处前后的代码为：

```
12      int kern_init(void) __attribute__((noreturn));
13      void grade_backtrace(void);
14      static void lab1_switch_test(void);
15
16      int
17      kern_init(void) {
18          extern char edata[], end[];
19          memset(edata, 0, end - edata);
20
21          cons_init();                // init the console
```

## 练习3

首先关中断，并将DF标志和各段寄存器置零：

```gas
.code16                                             # Assemble for 16-bit mode
    cli                                             # Disable interrupts
    cld                                             # String operations increment

    # Set up the important data segment registers (DS, ES, SS).
    xorw %ax, %ax                                   # Segment number zero
    movw %ax, %ds                                   # -> Data Segment
    movw %ax, %es                                   # -> Extra Segment
    movw %ax, %ss                                   # -> Stack Segment
```

然后使能A20。由于A20是使用8042键盘控制器控制的，所以需要先通过向`0x64`写入`0xd1`以进入写模式，然后再向`0x60`写入`0xdf`以使能A20。每次写之前还需读`0x64`以等待键盘控制器准备好：

```gas
seta20.1:
    inb $0x64, %al                                  # Wait for not busy(8042 input buffer empty).
    testb $0x2, %al
    jnz seta20.1

    movb $0xd1, %al                                 # 0xd1 -> port 0x64
    outb %al, $0x64                                 # 0xd1 means: write data to 8042's P2 port

seta20.2:
    inb $0x64, %al                                  # Wait for not busy(8042 input buffer empty).
    testb $0x2, %al
    jnz seta20.2

    movb $0xdf, %al                                 # 0xdf -> port 0x60
    outb %al, $0x60                                 # 0xdf = 11011111, means set P2's A20 bit(the 1 bit) to 1
```

然后通过`lgdt`指令加载位于bootloader中的GDT：

```gas
    lgdt gdtdesc
```

其中`gdtdesc`处存有GDT的长度和位置：

```gas
gdtdesc:
    .word 0x17                                      # sizeof(gdt) - 1
    .long gdt                                       # address gdt
```

GDT内容如下，偏移量`0x0`、`0x8`和`0x10`处分别为空段、代码段和数据段。代码段和数据段起始地址均为零，长度均为最大长度：

```gas
.p2align 2                                          # force 4 byte alignment
gdt:
    SEG_NULLASM                                     # null seg
    SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)           # code seg for bootloader and kernel
    SEG_ASM(STA_W, 0x0, 0xffffffff)                 # data seg for bootloader and kernel
```

然后通过将cr0寄存器PE位置1以开启保护模式：

```gas
    movl %cr0, %eax
    orl $CR0_PE_ON, %eax
    movl %eax, %cr0
```

接下来设置各段寄存器。其中代码段寄存器`cs`是通过跳转指令设置的，其余段寄存器是通过直接`movw`设置的。代码段被设置到GDT偏移量`0x8`的位置，而数据段被设置到GDT偏移量`0x10`的位置，与上述GDT一致。因为代码段描述符中说明了代码段中存储的是32位指令，所以跳转后切换到32位指令集：

```gas
    ljmp $PROT_MODE_CSEG, $protcseg

.code32                                             # Assemble for 32-bit mode
protcseg:
    # Set up the protected-mode data segment registers
    movw $PROT_MODE_DSEG, %ax                       # Our data segment selector
    movw %ax, %ds                                   # -> DS: Data Segment
    movw %ax, %es                                   # -> ES: Extra Segment
    movw %ax, %fs                                   # -> FS
    movw %ax, %gs                                   # -> GS
    movw %ax, %ss                                   # -> SS: Stack Segment
```

最后初始化栈指针，然后调用C函数`bootmain`：

```gas
    movl $0x0, %ebp
    movl $start, %esp
    call bootmain
```

## 练习4

`bootmain.c`中首先定义了`readsect`用于读取一个扇区：

```c
static void
readsect(void *dst, uint32_t secno) {
    // wait for disk to be ready
    waitdisk();

    outb(0x1F2, 1);                         // count = 1
    outb(0x1F3, secno & 0xFF);
    outb(0x1F4, (secno >> 8) & 0xFF);
    outb(0x1F5, (secno >> 16) & 0xFF);
    outb(0x1F6, ((secno >> 24) & 0xF) | 0xE0);
    outb(0x1F7, 0x20);                      // cmd 0x20 - read sectors

    // wait for disk to be ready
    waitdisk();

    // read a sector
    insl(0x1F0, dst, SECTSIZE / 4);
}
```

读取时，首先向`0x1f2`写入1表示读取一个扇区；然后向`0x1f3~0x1f6`写入一个32位字，其中29-31位强制设为1，28位为零表示读取主盘而非从盘，0-27位表示扇区编号；接下来向`0x1f7`写入`0x20`表示读取。待磁盘准备好数据后，从`0x1f0`连续读取要读取扇区中的数据。

`bootmain.c`中将`readsect`函数进一步封装为`readseg`函数，用于从磁盘中读取连续任意长度的数据。

主函数`bootmain`函数首先调用`readseg`将ELF文件的头读入`0x10000`开始的临时空间，并根据头的内容检查ELF的合法性。然后，根据头中描述的各段的偏移量和长度，反复调用`readseg`将各段读入内存中相应位置。最后，跳转到ELF头中指定的程序入口：

```c
void
bootmain(void) {
    // read the 1st page off disk
    readseg((uintptr_t)ELFHDR, SECTSIZE * 8, 0);

    // is this a valid ELF?
    if (ELFHDR->e_magic != ELF_MAGIC) {
        goto bad;
    }

    struct proghdr *ph, *eph;

    // load each program segment (ignores ph flags)
    ph = (struct proghdr *)((uintptr_t)ELFHDR + ELFHDR->e_phoff);
    eph = ph + ELFHDR->e_phnum;
    for (; ph < eph; ph ++) {
        readseg(ph->p_va & 0xFFFFFF, ph->p_memsz, ph->p_offset);
    }

    // call the entry point from the ELF header
    // note: does not return
    ((void (*)(void))(ELFHDR->e_entry & 0xFFFFFF))();

bad:
    outw(0x8A00, 0x8A00);
    outw(0x8A00, 0x8E00);

    /* do nothing */
    while (1);
}
```

## 练习5

实现了`print_stackframe`函数。其中，打印每一层栈帧信息时，可通过`ebp`确定栈帧位置以从中获取调用参数，并通过`eip`确定当前所调用函数的信息。打印完每层栈帧的信息后，从`ss:[ebp+4]`处即可取得上层函数旧的`eip`，从`ss:[ebp]`处即可取得旧的`ebp`，从而继续打印上层函数的栈帧，直到`ebp`指向空地址。

运行结果如下：

```
ebp:0x00007b38 eip:0x00100a28 args:0x00010094 0x00010094 0x00007b68 0x0010007f
    kern/debug/kdebug.c:305: print_stackframe+22
ebp:0x00007b48 eip:0x00100d04 args:0x00000000 0x00000000 0x00000000 0x00007bb8
    kern/debug/kmonitor.c:125: mon_backtrace+10
ebp:0x00007b68 eip:0x0010007f args:0x00000000 0x00007b90 0xffff0000 0x00007b94
    kern/init/init.c:48: grade_backtrace2+19
ebp:0x00007b88 eip:0x001000a1 args:0x00000000 0xffff0000 0x00007bb4 0x00000029
    kern/init/init.c:53: grade_backtrace1+27
ebp:0x00007ba8 eip:0x001000be args:0x00000000 0x00100000 0xffff0000 0x00100043
    kern/init/init.c:58: grade_backtrace0+19
ebp:0x00007bc8 eip:0x001000df args:0x00000000 0x00000000 0x00000000 0x00103280
    kern/init/init.c:63: grade_backtrace+26
ebp:0x00007be8 eip:0x00100050 args:0x00000000 0x00000000 0x00000000 0x00007c4f
    kern/init/init.c:28: kern_init+79
ebp:0x00007bf8 eip:0x00007d6e args:0xc031fcfa 0xc08ed88e 0x64e4d08e 0xfa7502a8
    <unknow>: -- 0x00007d6d --
```

其中，打印的最后一个栈帧是`bootmain.c`中的`bootmain`函数。各参数解释如下：

- `ebp = 0x7bf8`：Bootloader中初始化`ebp`为`0x7c00`，`bootmain`是栈中第一个函数，故其栈帧基地址为`0x7c00 - 4 - 4 = 0x7bf8`。减去的两个四字节分别是旧`ebp`（为空地址）和返回地址（实际上永不返回）；
- `eip = 0x7d63`：此地址指向此栈帧中将要执行的下一条指令的地址（实际上永不执行），为`bootmain.c:110`处的`outw`；
- 调用参数：`bootmain`函数没有参数。此处的“参数”实际上是`0x7c00`处的四个32位字，位于栈外，存储的是bootloader中第一条指令。

