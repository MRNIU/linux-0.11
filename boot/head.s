/*
 *  head.s contains the 32-bit startup code.
 *
 *  NOTE!!! Startup happends at absolute address 0x00000000,which is also
 *  where the page directory will exit. The startup code will be overwritten
 *  by the page directly.
 */
 /* head.s 含有 32 位启动代码。注意！！！ 32 位启动代码是从绝对地址 0x00000000 开始的，
 *  这里也同样是页目录将存在的地方，因此这里的启动代码将被页目录覆盖掉。 */
.text
.globl _idt,_pg_dir,_tmp_floppy_area
_pg_dir:  # 页目录将会存放在这里
startup_32:   # 14-28 行设置各个数据段寄存器
  movl $0x10,%eax # 对于 GNU 汇编来说，每个直接数以 '$' 开始，否则是表示地址。
  # 每个寄存器都要以 '%' 开头， eax 表示是 32 位的 ax 寄存器。
  # 再次注意！！！ 这里已经处于 32 位运行模式，因此这里的 $0x10 并不是，把地址 0x10 装入
  # 各个段寄存器，他现在其实是全局段描述符表中的偏移值，或者更准确的说是一个描述符表项的选择符。
  # 有关段选择符的说明请参见 setup.s 中 223 行下的说明。这里 $0x10 的含义是请求特权级 0
  # (位 0～1 =0)、选择全局描述符表(位 2=0)、选择表中第 2 项(位 3～15=2)。它正好指向表中的
  # 数据段描述符项。(描述符的具体数值参数见前面 setup.s 中 251-252 行)下面代码的含义是：
  # 置 ds,es,fs,gs 中的选择符为 setup.s 中构造的数据段(全局描述符表的第 2 项)=0x10，
  # 并将堆栈放置在 _stack_start 所指向的 user_stack 数组内，然后使用新的中断描述符表和
  # 全局段描述符表。新的全局段描述符表中初始内容与 setup.s 中的基本一样。变量 _stack_start
  # 定义在 kernel/sched.c 65 行
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%fs
    mov %ax,%gs
    lss _stack_start,%esp # 表示 _stack_start -> ss:esp，设置系统堆栈
    call setup_idt    # 调用设置中断描述符表子程序
    call setup_gdt    # 调用设置全局描述符表子程序
    movl $0x10,%eax   # reload all the segment registers
    mov %ax,%ds       # after changing gdt. CS was already
    mov %ax,%es       # reloaded in 'setup_gdt'
    mov %ax,%fs       # 因为修改了 gdt ，所以服药重新装载所有的段寄存器。
    mov %ax,%gs       # CS 代码段寄存器已经在 setup_gdt 中重新加载过了。
    lss _stack_start
# 41-45 行用于测试 A20 地址线是否已经开启。采用的方法是向内存地址 0x000000 处写入任意一个数值，
# 然后看内存地址 0x100000(1 M) 处是否也是这个值。如果一直相同的话，就一直比较下去，即死循环、
# 死机表示地址 A20 线没有想通，结果内核就不能使用 1 MB 以后是那个内存。
  xorl $eax,$eax
1:incl %eax             # check that A20 really IS enabled
  movl %eax,0x00000000  # loop forever if it isn't
  cmpl %eax,0x100000
  je 1b    # '1b' 表示向后(backwards)跳转到标号 1 去 (42 行)
           # 若是 '5f' 则表示向前(forword)跳转到标号 5 去。

/*
 * NOTE! 486 should set bit 16,to check for write-protect in supervisor mode.
 * Then it would be unnecessary with the "verify_area()" -calls.
 * 486 users probably want to set the NE(#5) bit also,so as to use int 16
 * for math errors.
 */
/* 注意！在下面这段程序中，486 应该将位 16 置位，以检查在超级用户模式下的写保护，
 * 此后 "verify_area()" 调用中就不需要了。 486 的用户也通常会想将 NE(#5) 置位，以便
 * 对数学协处理器的出错使用 int 16. */
# 下面这段程序(60-82 行)用于检查数学协处理器芯片是否存在。方法是修改控制寄存器 CR0，在
# 假设存在协处理器的情况下执行一个协处理器指令，如果出错的话则说明协处理器芯片并不存在，需要设置
# CR0 中的协处理器仿真位 EM(位 2)，并复位协处理器存在标志 MP(位 1)。
  movl %cr0,%eax    # check math chip
  andl $0x80000011,%eax   # Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good. */
  orl $2,%eax   # set MP
  movl %eax,%cr0
  call check_x87
  jmp after_page_tables   # 跳转到 176 行

/*
 * We depend on ET to be correct.This checks for 287/387.
 */
/*
 * 我们依赖于 ET 标志的正确性来检测 287/387 的存在与否。
 */
check_x87:
  fninit
  fstsw %ax
  cmpb $0,%al
  je 1f           /* no coprocessor: have to set bits */
  movl %cr0,%eax  # 如果存在则向前跳转到标号 1 处，否则改写 cr0
  xorl $6,%eax    /* reset MP,set EM */
  movl %eax,%cr0
  ret
.align 2  # 这里 '.align 2' 的含义是指存储边界对齐调整。“2”表示调整到地址最后两位为 0
          # 即按 4 字节方式对齐内存地址。对齐的主要作用是提高 CPU 寻址运行效率。
1:.byte 0xDB,0xE4   /* fsetpm for 287,ignored by 387 , 287 协处理器码 */
  ret

/*
 * setup_idt
 *
 * sets up a idt with 256 entries pointing to ignore_int,interrupt gates.
 * It then loads idt.Everything that wants to install itself in the idt_table
 * may do so themselves.Interrupts are enabled elsewhere,when we can be
 * relatively sure everything is ok.This routine will be overwritten
 * by the page tables.
 */
/* 下面这段是设置中断描述符表子程序 setup_idt，将中断描述符表 idt 设置成具有  256 个项，并
 * 都指向 ignore_int 中断门。然后加载中断描述符表寄存器(用 lidt 指令)。真正实用的中断门
 * 以后再安装。当我们在其它地方认为一切都正常时再开启中断。该子程序会被页表覆盖掉。 */
# 中断描述符表中的项虽然也是 8B 组成，但其格式与全局表中的不同，被称为门描述符
# (Gate Descriptor)。它的 0～1，6～7 字节是偏移量，2～3B 是选择符，4～5B 是一些标志。
setup_idt:
  lea ignore_int,%edx     # 将 ignore_int 的有效地址(偏移值)值 -> edx 寄存器
  movl $0x00080000,%eax   # 将选择符 0x0008 置入 eax 的高 16 位中。
  movw %dx,%ax      /* selector = 0x0008 = cs */
# 偏移值的低 16 位置入 eax 的低 16 位中。此时 eax 含有门描述符低 4B 的值。
  movw $0x8E00,%dx  /* interrupt gate-dpl=0,present */
                    # 此时 eax 含有门描述符高 4B 的值
  lea _idt,%edi     # _idt 是中断描述符表的地址
  mov $256,%ecx
rp_sidt:
  movl %eax,(%edi)  # 将哑中断门描述符存入表中
  movl %edx,4(%edi)
  addl $8,%edi      # edi 指向表中下一项
  dec %ecx
  jne rp_sidt
  lidt idt_descr    # 加载中断描述符表寄存器值
  ret

/*
 * setup_gdt
 *
 * This routines sets up a new gdt and loads it.
 * Only two entries are currently built,the same ones that were built in init.s
 * The routine is VERY complicated at two whole lines,so this rather long comment
 * is certainly needed :-).
 * This routine will beoverwritten by the page table.
 */
/*
 * 设置全局描述符表项 setup_gdt
 * 这个子程序设置一个新的全局描述符表 gdt，并加载。此时仅创建了两个表项，与前面的一样。该子程序只有
 * 两行，"非常的" 复杂，所以需要这么长的注释。
 */
setup_gdt:
  lgdt gdt_descr # 加载全局描述符表寄存器(内容已设置好，见 287-296 行)
  ret
/*
 * I put kernel page tables right after the page directory,using 4 of them to span
 * 16 MB of physicial memory.People with more than 16 MB will have to expand this.
 */
/*
 * Linus 将内核的内存页表直接放在页目录之后，使用了 4 个表来寻址 16 MB 的物理内存。
 * 如果你有多于 16 MB 的内存，就需要在这里进行扩充修改。
 */
# 每个页表长为 4 KB 字节，而每个页表项需要 4 个字节，因此一个页表共可以存放 1024 个表项，如果
# 一个表项寻址 4 KB 的地址空间，则一个页表就可以寻址 4 MB 的物理内存。页表项的格式为:
# 项的前 0～11 位存放一些标志，如是否在内存中(P 位 0)、读写许可(R/W 位 1)、普通用户还是超级用户
# 使用(U/S 位 2)、是否修改过(是否脏了)(D 位 6)等；表项的位 12～31 是页框地址，用于指出一页内存的
# 物理起始地址。
.org 0x1000  # 从偏移 0x1000 处开始是第一个页表(偏移 0 开始处将存放页表目录)。
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000  # 定义下面的内存数据块从偏移 0x5000 处开始
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot reach to a
 * buffer-block.It needs to be aligned,so that it isn't on a 64 KB border.
 */
/* 当 DMA (直接储存器访问) 不能访问缓冲块时，线面的 tmp_floppy_area 内存块就可供软盘驱动程序
 * 使用。其地址需要对齐调整，这样就不会跨越 64 KB 边界。
 */
_tmp_floppy_area:
  .fill 1024,1,0   # 共保留 1024 项，每项 1 字节，填充数值 0.
# 下面几个入栈操作(pushl)用于为调用 /init/main.c 程序和返回作准备。180 行的入栈操作是模拟调用
# main.c 程序时首先将返回地址入栈的操作，所以如果 main.c 程序真的退出时，就会返回到这里的标号
# L6 处去继续执行下去，即死循环。181 行将 main.c 的地址压入堆栈，这样，在设置分页处理(setup_paging)
# 结束后执行 'ret' 返回指令时就会将 main.c 程序的地址弹出堆栈，并去执行 main.c 程序去了
after_page_tables:
  pushl $0  # These are the parameters to main :-)
  pushl $0  # 这里是调用 main 函数的参数(指 init/main.c)
  pushl $0
  pushl $L6 # return address for main,if it decides to.
  pushl $_main  # '_main' 是编译程序对 main 的内部表示方法
  jmp setup_paging  # 跳转至第 242 行。

  jmp L6  # main should never return here,but just in case,we know what happens.

/* This is the default interrupt "headler" :-) */   /* 下面是默认的中断“向量句柄” */
int_msg:
  .asciz "Unknown interrupt\n\r"  # 定义字符串“未知中断(回车换行)”。
.align 2    # 按 4 字节方式对齐内存地址
ignore_int:
  pushl %eax
  pushl %ecx
  pushl %edx
  push %ds    # 这里请注意！！ ds,es,fs,gs 等虽然是 16 位的寄存器，但入栈后仍然会以 32 位
  push %es    # 的形式入栈，即需要占用 4 个字节的堆栈空间
  push %fs
  movl $0x10,%eax # 置段选择符(使 ds,es,fs 指向 gdt 表中的数据段)
  mov %ax,%ds
  mov %ax,%es
  mov %ax,%fs
  pushl $int_msg  # 把调用 printk 函数的参数指针(地址)入栈
  call _printk    # "_printk" 是 printk 编译后模块中的内部表示法
  popl %eax
  pop %fs
  pop %ds
  popl %edx
  popl %ecx
  popl %eax
  iret    # 中断返回(把中断调用时压入栈的 CPU 标志寄存器(32 位)值也弹出)。


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit in cr0.The page tables are
 * set up,identity-mapping the first 16 MB. The pager assumes that no illegal addresses
 * are produced(ie > 4 MB in a 4 MB machine).
 *
 * NOTE! Although
 * all physicial memory should be identity mapped by this routine,only the kenel page
 * functions use the > 1 MB addresses directly.All "normal" functions use just
 * the lower 1 MB ,or the local data space,which will be mapped to some other place
 * - mm keeps track of that.
 *
 * For those with more memory than 16 MB - tough luck.I've not got it,why hould you :-)
 * The source is here.Change it.(Serious - it shouldn't be too difficult.Mostly change
 * some constants etc. I left it at 16 MB ,as my machine even cannot be extended
 * past that (ok,but it was cheap :-)
 * I've tried to show which constants to change by having some kind of marker at them
 * (search fot "16 MB"),but I won't guarantee that's all :-( )
 */
/* 这个子程序通过设置控制寄存器 cr0 的标志(PG 位31)来启动对内存的分页处理功能，并设置各个页表项的
 * 内容，以恒等映射前 16 MB 的物理内存。分页器假定不会产生非法的地址映射(即在只有 4 MB 的机器上
 * 设置出大于 4 MB 的内存地址)。注意！尽管所有的物理地址都应该由这个子程序进行恒等映射，但只有内核
 * 页面管理函数能直接使用大于 1 MB 的地址。所有 “一般” 函数仅使用低于 1 MB 的地址空间，或者是使用
 * 局部数据空间，地址空间将被映射到其他一些地方去--mm(内存管理程序)会管理这些事的。对于那些有
 * 多于 16 MB 内存的机器，代码就在这里，可对它进行修改。(实际上，这并不太困难的。通常只需修改一些
 * 常数等。我把它们设置为 16 MB ，因为我的机器再怎么扩充都不能超过这个界限(当然，我的机器很便宜的)。
 * 我已经通过设置某类标志来给出需要改动的地方(搜索 “16 MB”)，但我不能保证作这些改动就行了)。
 */
.align 2  # 按 4 字节方式对齐内存地址边界
setup_paging: # 首先对 5 页内存(1 页  目录+ 4 页页表)清零。
  movl $1024*5,%eax  # 5 pages - pg_dir+4 page tables
  xorl %eax,%eax
  xorl %edi,%edi  # pg_dir is at 0x00  页目录从 0x00 地址开始
  cld;rep;stosl
# 下面 4 句设置页目录中的项，我们共有 4 个页表所以只需设置 4 项。页目录项的结构与页表中项的结构一样，
# 4 个字节为 1 项。参见上面 141 行下的说明。 "$pg0+7" 表示: 0x00001007，是页目录表中的第 1 项。
# 则第 1 个页表所在的地址= 0x00001007 & 0xfffff000 =0x1000 ;
# 第一个页表的属性标志= 0x00001007 & 0x00000fff = 0x07 ，表示该页存在、用户可读写
  movl $pg0+7,_pg_dir   # set present bit/user r/w
  movl $pg1+7,_pg_dir+4
  movl $pg2+7,_pg_dir+8
  movl $pg3+7,_pg_dir+12
# 下面 6 行填写 4 个页表中所有项的内容，共有: 4 (页表)*1024(项/页表)=4096 项(0-0xfff)，即
# 能映射物理内存 4096*4 KB=16 MB。 每项的内容是: 当前项所映射的物理内存地址 + 该页的标志(这里均为 7)
# 使用的方法是从最后一个页表的最后一项开始按倒退顺序填写。一个页表的最后一项在页表中的位置是1023*4=4092
# 因此最后一页的最后一项的位置就是 $pg3+4092
  movl $pg3+4092,%edi # edi -> 最后一页的最后一项
  movl $0xfff007,%eax # 16 MB-4096+7(r/w user,p)
# 最后 1 项对应物理内存页面的地址是 0xfff000,加上属性标志 7 ，即为 0xfff007
  std # 方向位置位， edi 值递减(4 B)
  stosl # fill pages backwards - more efficient :-)
  sub $0x1000,%eax  # 每天填写好一项，物理地址减 0x1000
  jge 1b  # 如果小于 0 则说明全填好了
# 下面设置页目录基址寄存器 cr3 的值，指向页目录表
  xorl %eax,%eax  # pg_dir is at 0x0000 页目录表在 0x0000 处
  movl %eax,$cr3  # cr3 - page directory start
  movl %cr0,%eax  # 设置启动使用分页标志(cr0 的 PG 标志，位 31)
  orl $0x80000000,%eax  # 添上 PG 标志
  movl $eax,%cr0  # set paging (PG) bit
  ret # this also flushes prefetch-queue
# 在改变分页处理标志后，要求使用转移指令刷新预取指令队列，这里用的是返回指令 ret。该返回指令的
# 另一个作用是将堆栈中的 main 程序的地址弹出，并开始运行 /init/main.c 程序。本程序到此真正结束了
.align 2  # 按 4 字节方式对齐内存地址边界
.word 0
idt_descr:  # 下面两行是 lidt 指令的 6 B 操作数: 长度：基址
  .word 256 * 8-1 # idt contains 256 entries
  .long_idt
.align 2
.word 0
gdt_descr:  # 下面两行是 lgdt 指令的 6 B 操作数： 长度:基址
  .word 256*8-1 # so does gdt(not that that's any magic number,but it works for me :^)
  .long_gdt

  .align 3  # 按 8 字节方式对齐内存地址边界
_idt:  .fill 256,8,0  # idt is uninitialized  256 项，每项 8 字节，填 0
# 全局表。前 4 项分别是空项(不用)、代码段描述符、数据段描述符、系统段描述符，其中系统段描述符
# Linux 没有派用处。后面还预留了 252 项的空间，用于放置所创建任务的局部描述符(LDT)和对应的
# 任务状态段 TSS 的描述符。(0-nuo,1-cs,2-ds,3-sys,4-TSS0,5-LDT0,6-TSS1,7-LDT1,8-TSS2...)
_gdt:
  .quad 0x0000000000000000  # NULL descriptor
  .quad 0x00c09a0000000fff  # 16 MB  代码段最大长度 16 MB
  .quad 0x00c0920000000fff  # 16 MB  数据段最大长度 16 MB
  .quad 0x0000000000000000  # TEMPORARY - don't use
  .fill 252,8,0   # space for LDT's and TSS's etc
