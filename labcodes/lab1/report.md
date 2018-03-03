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
