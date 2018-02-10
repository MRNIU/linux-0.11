/*
 * 02.12.91 - Changed to static variables to indicate need for reset and recalibrate.
 * This makes some things easier (output_byte reset checking etc),and means less
 * interrupt jumping in case of errors,so the code is hopefully easier to understand.
 */
/* 02.12.91 - 修改成静态变量，以适应复位和重新校正操作。这是的某些事情做起来较为方便
 * (output_byte 复位检查器等)，并且意味着在出错时中断跳转要少些，所以希望代码能更容易被理解。
 */
/*
 * This file is certainly a mess.I've tried my best to get it working, but I don't
 * like programming floppies,and I have only one anyway. Urgel. I should check for
 * more errors,and do more graceful erroe recovery.Seems there are problem with
 * serveral drives.I've tried to correct them.No promises.
 */
/* 这个文件当然比较混乱。我已经尽我所能使其能够工作，但我不喜欢软驱编程，而且我也只有一个软驱。
 * 另外，我应该做更多的查错工作，以及改正更多的错误。对于某些软盘驱动器好像还存在一些问题。我已经尝试
 * 着进行纠正了，但不能保证问题已消失。
 */
/*
 * As with hd.c, all routines within this file can (and will) be called by interrupts,
 * so extreme caution is needed.A hardware interrupt handler may not sleep,or a
 * kernel panic will happen.Thus I cannot call "floppy-on" directly,but have to
 * set a special timer interrupt etc.
 *
 * Also,I'm not certain this works on more tha 1 floppy.Bugs may abund.
 */
/* 如同 hd.c 文件一样，该文件中的所有子程序都能够被中断调用，所以需要特别地小心。硬件中断处理程序
 * 是不能睡眠的，否则内核就会傻掉(死机)。因此不能直接调用 "floppy-on" ，而只能设置一个特殊的
 * 时间中断等。
 * 另外，我不能保证该程序能在多于 1 个软驱的系统上工作，有可能存在错误。
 */
 #include <linux/sched.h.>  // 调度程序头文件，定义任务结构 task_struct、初始任务 0 的数据
 #include <linux/fs.h>  // 文件头文件。定义文件表结构(file,buffer_head,m_inode 等)
 #include <linux/kernel.h>  // 内核头文件。含有一些内核常用函数的原形定义。
 #include <linux/fdreg.h> // 软驱头文件。含有软盘控制器参数的一些定义
 #include <asm/system.h>  // 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏
 #include <asm/io.h>  // io 头文件。定义硬件端口输入/输出宏汇编语句
 #include <asm/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。

 #define MAJOR_NR 2 // 软驱的主设备号是 2
 #include "blk.h" // 块设备头文件。定义请求数据结构、块设备数据结构和宏函数等信息。

 
