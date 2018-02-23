

/*
 *  system_call.s continue the system_call low-level handling routines.
 * This also continues the timer-interrupt handler,as some of the code is the
 * same. The hd- and floppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition,which happens every time after a
 * timer-interrupt and after each system call.Ordinary interrupts don't handle
 * signal-recognition,as that would clutter them up totally unnecessarily.
 *
 * Stack layout in 'ret_from_sys_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */
/* system_call.s 文件包含系统调用(system-call)底层处理程子序。由于有些代码比较类似，所以
 * 同时也包括时钟中断处理(timer-interrupt)句柄。硬盘和软盘的中断处理程序也在这里。
 * 注意：这段代码处理信号(signal)识别，在每磁时钟中断和系统调用之后都会进行识别。一般从系统
 * 调用返回('ret_from_system_call')时堆栈的内容见上面 14~25 行。
 */
SIG_CHLD=17 # 定义 SIG_CHLD 信号(子进程停止或结束)。

EAX=0x00
EBX=0x04
ECX=0x08
EDX=0x0c
FS=0x10
ES=0x14
DS=0x18
EIP=0x1c
CS=0x20
EFLAGS=0x24
OLDESP=0x28 # 当有特权级变化时
OLDSS=0x2c
# 以下这些是任务结构(task_struct)中变量的偏移值，参见 include/linux/sched.h 22222 行
state=0 # these are offest into the task-struct 进程状态码
counter=4 # 任务运行时间计数(递减)(滴答数)，运行时间片
priority=8  # 运行优先数。任务开始运行时 counter=priority,越大则运行时间越长
signal=12 # 是信号位图，每个比特位代表一种信号，信号值=位偏移值+1
sigaction=16  # MUST be 16 (=len of sigaction)  # sigaction 结构长度必须是 16 字节。
              # 信号执行属性结构数组的偏移值，对应信号将要执行的操作和标志信息。
blocked=(33*16) # 受阻塞信号位图的偏移量
# 以下定义在 sigaction 结构中的偏移量，参见 include/signal.h 2222 行
# offest within sigaction
sa_handler=0  # 信号处理过程的句柄(描述符)
sa_mask=4 # 信号量屏蔽码
sa_flags=8  # 信号集
sa_restorer=12  # 恢复函数指针，参见 linux/signal.c 程序
nr_system_calls=72  # Linux 0.11 版本内核中的系统调用总数

/*
 * Ok,I get parallel printer interrupts while using the floppy for some strange
 * reason.Urgel.Now I just ignore them.
 */
/* 在使用软驱时收到了并行打印机中断，但现在可以先不管它
*/
.globl _system_call , _sys_fork , _timer_interrupt , _sys_execve
.globl _hd_interrupt , _floppy_interrupt , _parallel_interrupt
.globl _device_not_available , _coprocessor_error

.align 2  # 内存 4 字节对齐
bad_sys_call: # 错误的系统调号从这里返回
  movl $-1,%eax # eax 中设置 -1 ，退出中断
  iret

.align 2
reschedule: # 重新执行调度程序入口。调度程序 schedule 在(kernel/sched.c 108)
  pushl $ret_from_sys_call  # 将 ret_from_sys_call 的地址入栈(107 行)
  jmp _schedule
.align 2
_system_call: # int 80--Linux 系统调用入口点(调用中断 int 0x80,eax 中是调用号)
  cmpl $nr_system_calls-1,%eax  # 调用号如果超出范围的话就在 eax 中置 -1 并退出
  ja bad_sys_call
  push %ds  # 保存原段寄存器值
  push %es
  push %fs
  pushl %edx  # ebx，ecx，edx 中放着系统调用相应的 C 语言的调用参数
  pushl %ecx  # push %ebx,5ecx,%edx as parameters
  pushl %ebx  # to the system call
  movl $0x10,%edx # set up ds,es to kernel space
  mov %dx,%ds # ds,es 指向内核数据段(全局描述符表中数据描述符段)
  mov %dx,%es
  movl $0x17,%edx # fs points to local data space
  mov %dx,%fs # fs 指向局部数据段(全局描述符表中数据段描述符)
# 下面操作数的含义是：调用地址=_sys_call_table+%eax*4.见文件后的说明对应 C 程序中的
# sys_call_table 在 include/linux/sys.h 中，其中定义了一个包括 72 条系统调用 C 处理
# 函数的地址数组表。
  call _sys_call_table(,%eax,4) # 把系统调用返回值入栈
  pushl %EAX,ovl _current],%eax # 取当前任务(进程)数据结构地址 -> eax.
# 下面 103～106 行查看当前任务的运行状态。如果不在就绪状态(state 不等于 0)就去执行调度程序。
# 如果该任务在就需状态但时间片已经用完(counter 值等于 0)，则也去执行调度程序。
  cmpl $0,state(%eax)  # state
  jne reschedule
  cmpl $0,counter(%eax) # counter
  je reschedule
ret_from_sys_call:  # 以下这段代码执行从系统调用 C 函数返回后，对信号量进行识别处理。
                    # 首先判别当前任务是否是初始任务 task0 ,如果不是则不必对其进行信号量方面的处理，
                    # 直接返回。
                    # 113 行上的 _task 对应 C 程序中的 task[] 数组，直接引用 task 相当于
                    # 引用 task[0]
  movl _current,%eax  # task[0] cannot have signals
  cmpl _task,%eax
  je 3f # 向前跳转到标号 3
# 通过对原调用程序代码选择符的检查来判断调用程序是否是内核中任务(例如任务 0)。如果是就直接对退出中断，
# 否则需进行信号量的处理。这里比较选择符是否为普通用户代码段的选择符 0x000f(RPL=3 ,局部表，第一个
# 段(代码段))，如果不是则跳转退出中断程序。
  cmpw $0x0f,CS(%esp) # was old code segment supervisor?
  jne 3f
# 如果原堆栈段选择符不为 0x17(即原堆栈不在用户数据段中)，则也退出。
  cmpw $0x17,OLDSS(%esp)  # was stack segment=0x17?
  jne 3f
# 下面这段代码(127~140)的用途是首先取当前任务结构中的信号位图(32 位，每位代表 1 种信号)，然后用
# 人物结构中的信号阻塞(屏蔽)码，阻塞不允许的信号位，取得数值最小的信号值，再把原信号位途中该信号
# 对应的位复位(置 0)，最后将该信号值作为参数之一调用 do_signal(). do_signal() 在(kernel/signal.c 75)
# 其参数包括 13 各入栈信息。
  movl signal(%eax),%ebx  # 取信号位图->ebx，每 1 位代表 1 种信号，共 32 个信号
  movl blocked(%eax),%ecx # 取阻塞(屏蔽)信号位图->ecx
  notl %ecx # 每位取反
  andl %ebx,%ecx  # 获得许可的信号位图
  bsfl %ecx,%ecx  # 从低位(位 0)开始扫描位图，看是否有 1 的位，
                  # 若有，则 ecx 保留改为的偏移值(即第几位 0-31)。
  je 3f  # 如果没有信号则向前跳转退出
  btrl %ecx,%ebx  # 复位该信号(ebx 含有原 signal 位图)
  movl %ebx,%signal(%eax) # 重新保存 signal 位图信息-> current->signal
  incl %ecx # 将信号调整为从 1 开始的数(1-32).
  pushl %ecx  # 信号值入栈作为调用 do_signal 的参数之一
  call _do_signal # 调用 C 函数信号处理程序(kernel/signal.c 2222)
  popl %eax # 弹出信号值
3:popl %eax
  popl %ebx
  popl %ecx
  popl %edx
  pop %fs
  pop %es
  pop %ds
  iret
# int 16 -- 下面这段代码处理协处理器发出的出错信号。跳转执行 C 函数 _math_error()
# (kernel/math/_math_emulate.c 2222)，返回后将跳转到 ret_from_sys_call 处继续执行。
.align 2
_coprocessor_error:
  push %ds
  push %es
  push %fs
  pushl %edx
  pushl %ecx
  pushl %ebx
  pushl %eax
  movl $0x10,%eax # ds,es 置为指向内核数据段
  mov %ax,%ds
  mov %ax,%es
  movl $0x17,%eax # fs 置为指向局部数据段(出错程序的数据段)
  mov %ax,%fs
  pushl $ret_from_sys_call  # 把下面调用返回的地址入栈
  jmp _math_error # 执行 C 函数 _math_error() (kernel/math/math_emulate.c 2222).
# int 7 -- 设备不存在或协处理器不存在(Coprocessor not available)
# 若控制寄存器 CR0 的 EM 标志置位，则当 CPU 执行一个转移指令时就会引发该中断，这样就可以有机会让这个
# 中断处理程序模拟转义指令(194 行)。CR0 的 TS 标志是在 CPU 执行任务转换时设置的。TS 可以用来
# 确定什么时候协处理器中的内容与 CPU 正在执行的任务不匹配了。当 CPU 在运行一个转义指令时发现 TS
# 置位了，就会引发该中断。此时就应该恢复新任务的协处理器执行状态(190 行)。参见(kernel/sched.c 77)
# 中的说明。该中断最后将转移到标号 ret_from_sys_call 处执行(检测并处理信号)。
.align 2
_device_not_available:
  push %ds
  push %es
  push %fs
  pushl %edx
  pushl %ecx
  pushl %ebx
  pushl %eax
  movl $0x10,%eax # ds,es 置为指向内核数据段
  mov %ax,%ds
  mov %ax,%es
  movl $0x17,%eax # fs 置为指向局部数据段(出错程序的数据段)
  mov %ax,%fs
  pushl $ret_from_sys_call  # 把下面跳转或调用的返回地址入栈
  clts  # clear TS so that we can use math
  movl %cr0,%eax
  testl $0x4,%eax # EM (math emulation bit)
  je _math_state_restore  # 如果不是 EM 引起的中断，则恢复新任务协处理器状态
  pushl %ebp  # 执行 C 函数 math_state_restore() (kernel/sched.c 77)
  pushl %esi
  pushl %edi
  call _math_emulate  # 调用 C 函数 math_emulate (kernel/math/math_emulate.c 2222)
  popl %edi
  popl %esi
  popl %ebp
  ret # 这里的 ret 将跳转到 ret_from_sys_call (107 行)
# int32 -- (int 0x20)时钟中断处理程序。中断频率被设置为 100Hz (include/linux/sched.h 222)
# 定时芯片 8253/8254 是在(kernel/sched.c 431)处初始化的。因此这里 jiffies 每 10 毫秒
# 加 1.这段代码将 jiffies 增 1，发送结束中断指令给 8259 控制器，然后用当前特权级作为参数调用
# C 函数 do_timer(long CPL)。当调用返回时转去检测并处理信号
.align 2
_timer_interrupt:
  push %ds  # save ds,es and put kernel data space
  push %es  # into them. %fs is used by _system_call
  push %fs
  pushl %edx  # we save %eax,%ecx,%edx as gcc doesn't save those across function
  pushl %ecx  # calls. %ebx is saved as we use that in ret_sys_call
  pushl %ebx
  pushl %eax
  movl $0x10,%eax # ds,es 置为指向内核数据段
  mov %ax,%ds
  mov %ax,%es
  movl $0x17,%eax # fs 置为指向局部数据段(出错程序的数据段)
  mov %ax,%fs
  incl _jiffies
# 由于初始化中断控制芯片是没有采用自动 EOI，所以这里需要发指令结束该硬件中断。
  movb $0x20,%al  # EOI to interrupt controller #1
  outb %al,$0x20  # 操作命令字 OCW2 送 0x20 端口
# 下面 3 句从选择符中取出当前特权级别(0 或 3)并压入堆栈，作为 do_timer 的参数
  movl CS(%esp),%eax
  andl $3,%eax
  pushl %eax
# do_timer(CPL) 执行任务切换、计时等工作，在 kernel/sched.c 329 行实现
  call _do_timer  # 'do_timer(long CPL)'does everything from
  addl $4,%esp  # task switching to accounting
  jmp ret_from_sys_call
# 这是 sys_execve() 系统调用。去中断调用程序的代码指针作为参数调用 C 函数 do_execve().
# do_execve() 在 (fs/exec.c 2222)
.align 2
_sys_execve:
  lea EIP(%esp),%eax
  pushl %eax
  call _do_execve
  addl $4,%esp  # 丢弃调用时压入栈的 EIP 值
  ret
# sys_fork() 调用，用于创建子进程，是 system_call 功能 2.原形在 include/linux/sys.h 中
# 首先调用 C 函数 find_empty_process(),取得一个进程号 pid。若返回负数则说明目前任务数组
# 满。然后调用 copy_process() 复制进程。
.align 2
_sys_fork:
  call _find_empty_process
  testl %eax,%EAXjs 1f
  push %gs
  pushl %esi
  pushl %edi
  pushl %ebp
  pushl %eax
  call _copy_process  # 调用 C 函数  copy_process() (kernel/fork.c 2222)
  addl $20,%esp # 丢弃这里的所有的压栈内容。
1:ret
#### int 46 -- (int 0x2E) 硬盘中断处理程序，相应硬件中断请求 IRQ14.
# 当硬盘操作完成或出错就会发出此中断信号。(参见 kernel/blk_drv/hd.c)。首先向 8259A 中断控制
# 从芯片发送结束硬件中断指令(EOI)，然后取变量 do_hd 中的函数指针放入 edx 寄存器中，并置 do_hd
# 为 NULL，接着判断 edx 函数指针是否为空。如果为空，则给 edx 赋值指向 unexpected_hd_interrupt(),
# 用于显示出错信息。随后向 8259A 主芯片送 EOI 指令，并调用 edx 中指针指向的函数：read_intr()、
# write_intr() 或 unexpected_hd_interrupt().
_hd_interrupt:
  pushl %eax
  pushl %ecx
  pushl %edx
  push %ds
  push %es
  push %fs
  movl $0x10,%eax # ds,es 置为内核数据段
  mov %ax,%ds
  mov %ax,%es
  movl $0x17,%eax # fs 置为调用程序的局部数据段
  mov %ax,%fs
# 由于初始化中断控制芯片时没有采用自动 EOI，所以这里需要发指结束该硬件中断
  movb $0x20,%al
  outb %al,$0xA0  # EOI to interrupt controller #1  # 送从 8259A
  jmp 1f  # give port chance to breathe
1:jmp 1f  # 延时作用
1:xorl %edx,%edx
  xchgl _do_hd,%edx # do_hd 是函数指针，将被赋值 read_intr() 或 write_intr()
                    # 函数地址(blk_drv/hd.c)，放到 edx 后就将 do_hd 置为 NULL
  testl %edx,%edx   # 测试函数指针是否为 Null
  jne 1f  # 若空，则使指针指向 C 函数 unexpected_hd_interrupt()
  movl $_unexpected_hd_interrupt,%edx # (kernel/blk_drv/hd.c 250)
  outb %al,$0x20  # 送主 8259A 中断控制器 EOI 指令(结束硬件中断)
  call * %edx # "interesting"way of handling intr.
  pop %fs # 上句调用 do_hd 指向的 C 函数
  pop %es
  pop %ds
  popl %edx
  popl %ecx
  popl %eax
  iret
# int38 -- (int 0x26)软盘驱动器中断处理程序，响应硬件中断请求 IRQ6
# 其处理过程与上面对硬盘的处理基本一样 (kernel/blk_drv/floppy,c).首先向 8259A 中断控制器
# 主芯片发送 EOI 指令，然后取变量 do_floppy 中函数指针放入 eax 寄存器中，并置 do_floppy 为
# NULL，接着判断 eax 函数指针是否为空。如为空，则给 eax 赋值指向 unexpected_floppy_interrupt(),
# 用于显示出错信息。随后调用 eax 指向的函数:rw_interrupt、seek_interrupt、recal_interrupt、
# reset_interrupt 或 _unexpected_floppy_interrupt.
_floppy_interrupt:
  pushl %eax
  pushl %ecx
  pushl %edx
  push %ds
  push %es
  push %fs
  movl $0x10,%eax # ds,es 置为内核数据段
  mov %ax,%ds
  mov %ax,%es
  movl $0x17,%eax # fs 置为调用程序的局部数据段
  mov %ax,%fs
  movb $0x20,%al  # 送主 8259A 中断控制器 EOI 指令(硬件中断)。
  outb %al,$0x20  # EOI to interrupt controller #1
  xorl %eax,%eax  # 下句 do_floppy 为一函数指针，将被赋值实际处理 C 函数程序，
  xchgl _do_floppy,%eax # 放到 eax 寄存器后就将 do_floppy 指针变量置空。
  testl %eax,%eax # 测试函数指针是否=NULL？
  jne 1f  # 若空，则使指针指向 C 函数 unexpected_floppy_interrupt()
  movl $_unexpected_floppy_interrupt,%eax
  call * %eax # "interesting"way of handling intr.
  pop %fs # 上句调用 do_floppy 指向的函数
  pop %es
  pop %ds
  popl %edx
  popl %ecx
  popl %eax
  iret
# int 39 -- (int 0x27)并行口中断处理程序，对应硬件中断请求号 IRQ7.
_parallel_interrupt:  # 本版本内核还未实现，这里只是发送 EOI 指令。
  pushl %eax
  movb $0x20,%al
  outb %al,$0x20
  popl %eax
  iret
