#
# if you want the ram-disk device,define this to be the
# size in blocks
# 如果你要使用 RAM 盘设备的话，就定义块的大小
RAMDISK	= #-DRAMDISK=512

AS			= i386-elf-as				# GNU 汇编编译器(gas)和链接器(gld)，见后面的介绍
ASFLAGS = -march=i386

# 下一行是 GNU 链接器 gld 运行时用到的选项。含义是: -s 输出文件中省略所有的符号信息；-x删除所有
# 局部符号；-M 表示需要在标准输出设备(显示器)上打印连接映像(link map)，是指有链接程序产生的
# 一种内存地址映像，其中列出了程序段装入到内存中的位置信息。具体来讲有这些信息:
# a.目标文件及符号信息映射到内存中的未知；
# b.公共符号如何放置
# c.连接中班汉的所有文件成员及其引用的符号
LD			=  i386-elf-ld
LDFLAGS	= -A elf32_i386
# gcc 是 GNU C 程序编译器。对于 UNIX 类的脚本程序而言，在引用定义的标识符时，需要在前面
# 加上 $ 符号并用括号括住标识符

# 下两行是 gcc 的选项。前一行最后的 ‘\’ 标识符表示下一行是续行。选项含义为：
# -Wall 打印所有警告
# -O 指示对代码进行优化
# -fstrength-reduce 是优化循环语句
# -mstring-insns 是 Linus 自己为 gcc 增加的选项，用于对字符串指令优化程序，可以去掉。
CC			= i386-elf-gcc $(RAMDISK)
CFLAGS	=-g -m32 -fno-builtin -fno-stack-protector -fomit-frame-pointer \
         -fstrength-reduce #-Wall
# 下行 cpp 是 gcc 的前(预)处理程序。标志 -nostdinc 和 -Iinclude 的含义是不要搜索标准目录中的
# 头文件，而是使用 -I 选项指定目录或者是在当前目录里搜索头文件。
CPP			= i386-elf-cpp -nostdinc

# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of /dev/hd6 is used by 'build'
# ROOT_DEV 指定在创建内核映像(image)文件时所使用的默认根文件系统所在的设备，这可以是软盘(FLOPPY)、
# /dev/xxxx 或者干脆空着，空着时 build 程序(在 tools/ 目录中)就使用默认值 /dev/hd6.
ROOT_DEV	=/dev/hd6
# 下面是 kernel、mm 和 fs 目录所产生的目标代码文件。为方便引用，用 ARCHIVES 标识符表示
ARCHIVES	=kernel/kernel.o mm/mm.o fs/fs.o
DRIVERS		=kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
# 块和字符设备库文件。 .a 表示该文件是个归档文件，既包含许多可执行二进制代码自程序集合的库文件，
# 通常是用 GNU 的 ar 程序生成。 ar 是 GNU 的二进制文件处理程序，用于创建、修改以及从归档文
# 件中抽取文件
MATH	=kernel/math/math.a	#数学运算库文件

LIBS	=lib/lib.a	#由 lib/ 目录中的文件所编译生成的通用库文件

.c.s:
# make 老式的隐式后缀规则。该行指示 make 利用下面命令将所有的 .c 文件编译生成 .s 汇编程序
# ‘:’ 表示下面是该规则的命令。
			$(CC) $(CFLAGS) -nostdinc -Iinclude -S -o $*.s $<
			# 指示 gcc 采用前面 CFLAGS 所指定的选项以及仅使用 include/ 目录中的头文件，在适当地
			# 编译后就停止(-S)，从而产生与输入的各个 C 文件妒忌赢得汇编语言形式的代码文件。默认情况下
			# 所产生的汇编c程序文件是原 C 文件名去掉 .c 而加上 .s 后缀。 -o 表示其后是输出文件的形式。
			# 其中 $*.s(或 $@)是自动目标变量， $< 代表第一个先决条件，这里是符合条件 *.c  的文件。

.s.o:
# 指示将所有 .s 汇编程序编译成 .o 目标文件。西安一行是实现该操作的具体命令，即使用 gas 将
# 汇编程序编译成 .o 目标文件。
			$(AS) -c -o $.o $<	# gas 标志 -c 表示只编译或汇编但不做连接操作。

.c.o:
# 类似上面，*.c 文件 -> *.o 目标文件
			$(CC) $(CFLAGS) -nostdinc -Iinclude -c o $*.o $<
			# 使用 gcc 将 C 语言文件编译成目标文件但不连接

all: Image		# all 表示创建 Makefile 所知的最顶层目标。这里是 Image 文件
# 下句说明目标(Image)是由分号后的 4 个元素产生，分别是 boot/ 中的 bootsect 和 setup 文件、
# tools/ 目录中的 system 和 build 文件。72、73 两行是执行的命令。第一行表示使用 tools 目录下的
# 映像文件 Image。第二行的 sync 同步命令是迫使缓冲块数据立即写盘并更新超级块。

Image: boot/bootsect boot/setup tools/system tools/build
				tools/build boot/bootsect boot/setup tools/system $(ROOT_DEV) > Image
				sync
# 下一行表示 disk 这个目标要由 Image 产生。，命令 dd 为 UNIX 标准命令：复制一个文件，根据
# 选项进行转换和格式化。'bs=' 表示一次读/写的字节数。 'if=‘ 表示输入的文件，'of=' 表示输出到
# 的文件。这里 /dev/PS0 是指第一个软盘驱动器(设备文件)。
disk: Image
				dd bs=8192 if=Image of=/dev/PS0

tools/bulid: tools/bulid.c	# 由 tools 目录下的 build.c 程序生成执行程序 build。
				$(CC) $(CFLAGS) -o tools/build tools/build.c	# 编译生成执行程序 build 的命令。

boot/head.o: boot/head.s	# 利用上面给出的 .s.o 规则生成 head.o 目标文件
# 下句表示 tools/system 文件要由分号右边元素生成。222222 行是生成 system 的具体命令。最后的
# > System.map 表示 gld 需要将连接映像重定向保存在 System.map 文件中。关于 System.map 文件的
# 用途参见注释后的说明

tools/system:	boot/head.o init/main.o\
								$(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS)
						$(LD) $(LDFLAGS) boot/head.o init/main.o\
						$(ARCHIVES)\
						$(DRIVERS)\
						$(MATH)\
						$(LIBS)\
						-o tools/system > System.map
kernel/math/math.a:	# 数学协处理器函数文件 math.a 由下一行上的命令实现
							(cd kernel/math;make)
							# 进入 kernel/math/ 目录;运行 make 工具程序。下面 101-110 行的含义与此处类似。
kernel/blk_drv/blk_drv.a:	# 块设备函数文件 blk_drv.a
							(cd kernel/blk_drv;make)
kernel/chr_drv/chr_drv.a:	# 字符设备函数文件 chr_drv.a
							(cd kernel/chr_drv;make)
kernel/kernel.o:	# 内核目标模块 kernel.o
							(cd kernel;make)
mm/mm.o:	# 内存管理模块 mm.o
							(cd mm;make)
fs/fs.o:	# 文件系统目标模块 fs.o
							(cd fs;make)
lib/lib.a:	# 库函数 lib.a
							(cd lib;make)

boot/setup: boot/setup.s											# 这里开始的三行是使用 8086 汇编和连接器
				$(AS) -o boot/setup.o boot/setup.s	# 对 setup.s 文件进行编译生成 setup 文件
				$(LD) -s -o boot/setup boot/setup.o	# -s 选项表示要除去目标文件中的符号信息

boot/bootsect: boot/bootsect.s	# 同上，生成 bootsect.o 磁盘引导模块
				$(AS) $(ASFLAGS) -o boot/bootsect.o boot/bootsect.s
				$(LD) -s -o boot/bootsect boot/bootsect.o
# 129-132 这四行的作用是在 bootsect.s 程序开头添加一行有关 system 文件长度的信息。方法是首先
# 生成含有 "SYSSIZE=system 文件实际长度" 一行信息的 tmp.s 文件，然后将 bootsect.s 文件添加在
# 其后。取得 system 长度的方法是：首先利用命令 ls 对 system 文件进行长列表显示，用 grep 命令
# 取得列表行上文件字节数字段信息，并定向保存在 tmp.s 临时文件中。cut 命令用于剪切字符串，tr 用
#  于去除行尾的回车符。其中：(实际长度+15)/16 用于获得用 ‘节’ 表示的长度信息。1 节 = 16 字节。
tmp.s:	boot/bootsect.s tools/system
	 			(echo -n "SYSSIZE = (";ls -l tools/system | grep system\
									| cut -c25-31 | tr '\012' ' '; echo "+ 15 ) / 16") > tmp.s
				cat boot/bootsect.s >> tmp.s
# 当执行命令 'make claen' 时，就会执行 136-141 行上的命令，去除所有编译连接生成的文件。'rm' 是文件
# 删除命令，选项 -f 含义是忽略不存在的文件，并且不限时删除信息。
clean:
		rm -f Image System.map tmp_make core boot/bootsect boot/setup
		rm -f init/*.o tools/system tools/build boot/*.o
		(cd mm;make clean)	# 进入相应目录，执行该目录 Makefile 文件中的 clean 规则
		(cd fs;make clean)
		(cd kernel;make clean)
		(cd lib;make clean)
# 下面该规则将首先执行上面的 clean 规则，然后对 linux/ 目录进行压缩，生成 backup.Z 压缩文件。
# 'cd ..' 表示退到 linux/ 的上级目录；'tar cf - linux' 表示对 linux/ 目录执行 tar 归档程序
# -cf 表示需要创建新的归档文件； ‘|compress -’ 表示将 tar 程序的执行结果通过管道操作('|')传递给
# 压缩程序 compress ，并将压缩程序的输出存成 backup.Z 文件
backup: clean
			(cd ..;tar cf - linux|compress -> backup.Z)
			sync	#迫使缓冲块数据立即写盘并更新磁盘超级块
# 下面目标或规则用于各文件之间的依赖关系。创建的这些依赖关系是为了让 make 确定是否需要重建一个
# 目标对象。比如当某个头文件被改动过后，make 就通过生成的依赖关系，重新编译与改头文件有关的所有 *.c
# 文件。具体方法如下：使用字符串编辑程序 sed 对 Makefile 文件(这里即自己)进行处理，输出为删除
# Makefile 文件中 '### Dependencies' 行后面的所有行(下面从 167 开始的所有行)，并生成 tmp_make
# 临时文件(也即 159 行 的作用)。然后对 init/ 目录下的每一个 C 文件(实际只有一个文件)执行 gcc
# 预处理操作，-M 标志告诉预处理器程序输出一个 make 规则，其结果形式是相应源程序文件的目标文件名
# 加上其依赖关系——该源文件中包含的所有头文件列表。 160 行中的 $$i 实际上是 $($i) 的意思。这里
# $i 是这句前面的 shell 变量的值。最后把预处理结果都添加到临时文件 tmp_make 中，并将该临时文件复制成
# 新的 Makefile 文件。
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile
	(cd fs; make dep)
	(cd kernel; make dep)
	(cd mm; make dep)
### Dependencies:
init/main.o : init/main.c include/unistd.h include/sys/stat.h \
  include/sys/types.h include/sys/times.h include/sys/utsname.h \
  include/utime.h include/time.h include/linux/tty.h include/termios.h \
  include/linux/sched.h include/linux/head.h include/linux/fs.h \
  include/linux/mm.h include/signal.h include/asm/system.h include/asm/io.h \
  include/stddef.h include/stdarg.h include/fcntl.h
