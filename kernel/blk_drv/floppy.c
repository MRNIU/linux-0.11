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


static int recalibrate=0;  // 标志：需要重新校正
static int reset=0;  // 标志：需要进行复位操作
static int seek=0; // 寻道

extern unsigned char current_DOR;  // 当前数字输出寄存器(digital Output Register)

#define immoutb_p(val,port)\ // 字节直接输入(嵌入汇编语言宏)
__asm__("outb %0,%1\n\tjmp 1f\n1:\tjmp 1f\n1:"::"a"((char)(val)),"i"(port))
// 这两个定义用于计算软驱的设备号。次设备号=TYPE*4+DRIVE。计算方法参见程序后。
#define TYPE(x) ((x)>>2) // 软驱类型(2---1.2 MB,7---1.44 MB)
#define DRIVE(x) ((x)&0x03)  // 软驱序号(0~3 对应 A~D)
 /*
  * Note that MAX_ERRORS=8 doesn't imply that we retry every bad read
  * max 8 times - some types of errors increase the errorcount by 2,
  * so we might actually retry only 5-6 times before giving up.
  */
/* 注意，下面定义 MAX_ERRORS=8 并不表示对每次读错误尝试最多 8 次，有些类型的错误将把出错
 * 值乘 2，所以我们实际上在放弃操作之前只需尝试 5～6 遍即可
 */
#define MAX_ERRORS 8

 /*
  * globals used by 'result()'
  */  // 下面是函数 'result()' 使用的全局变量
// 这些状态字节中各比特位的含义请参见 include/linux/fdreg.h 头文件
#define MAX_REPLIES 7  // FDC 最多返回 7 B 的结果信息
static unsigned char reply_buffer[MAX_REPLIES];  // 存放 FDC 返回的结果信息
#define ST0 (reply_buffer[0])  // 返回结果状态字节 0
#define ST1 (reply_buffer[1])  // 返回结果状态字节 1
#define ST2 (reply_buffer[2])  // 返回结果状态字节 2
#define ST3 (reply_buffer[3])  // 返回结果状态字节 3

/*
 * This struct defines the different floppy types. Unlike minix
 * linux doesn't have a "search for right type"-type, as the code
 * for that is convoluted and weird. I've got enough problems with
 * this driver as it is.
 *
 * The 'stretch' tells ifthe tracks need to be boubled for some
 * types (ie 360kB diskette in 1.2MB drive etc). Others should
 * be self-explanatory.
 */
/* 下面的软盘结构定义了不同的软盘类型。与 MINIX 不同的是，Linux 没有 "搜索正确的类型" 类型，
 * 因为对齐处理的代码令人费解。本程序已将让我遇到了许多问题了。
 * 对某些类型的软盘(例如在 1.2 MB 驱动器中的 360 KB 软盘等)，'stretch' 用于检测磁道是否需要
 * 特殊处理。其它参数应该是很容易理解的。
 */
// 软盘参数有：size-大小(扇区数)；sect-每磁道扇区数；head-磁头数；track-磁道数；stretch-
// 对磁道是否要特殊处理；gap-扇区间隙长度(字节数)；rate-数据传输速率；spec1-参数(高 4 位步进
// 速率，低 4 位磁头卸载时间)
static struct floppy_struct{
 	unsigned int size,sect,head,track,stretch;
 	unsigned char gap,rate,spec1;
}floppy_type[]=
 	{0,0,0,0,0,0x00,0x00,0x00},	// no testing
 	{720,9,2,40,0,0x2A,0x02,0xDF},	// 360kB PC diskettes
 	{2400,15,2,80,0,0x1B,0x00,0xDF},	// 1.2 MB AT-diskettes
 	{720,9,2,40,1,0x2A,0x02,0xDF},	// 360kB in 720kB drive
 	{1440,9,2,80,0,0x2A,0x02,0xDF},	// 3.5" 720kB diskette
 	{720,9,2,40,1,0x23,0x01,0xDF},	// 360kB in 1.2MB drive
  {1440,9,2,80,0,0x23,0x01,0xDF},	// 720kB in 1.2MB drive
  {2880,18,2,80,0,0x1B,0x00,0xCF},	// 1.44MB diskette
};
/*
 * Rate is 0 for 500kb/s, 2 for 300kbps, 1 for 250kbps
 * Spec1 is 0xSH, where S is stepping rate (F=1ms, E=2ms, D=3ms etc),
 * H is head unload time (1=16ms, 2=32ms, etc)
 *
 * Spec2 is (HLD<<1 | ND), where HLD is head load time (1=2ms, 2=4 ms etc)
 * and ND is set means no DMA. Hardcoded to 6 (HLD=6ms, use DMA).
 */
/* 上面速率 rate:0 表示 500 KB/s,1 表示 300 KB/s，2 表示 250 KB/s。参数 spec1 是 0xSH，
 * 其中 S 是步进速率(F=1ms,E=2ms,D=3ms 等)，H 是磁头卸载时间(1=16 ms,2=32 ms 等)
 * spec2 是(HLD<<1|ND),其中 HLD 是磁头加载时间(1=2ms,2=4ms 等)
 * ND 置位表示不使用 DMA(No DMA),在程序中硬编码成 6(HLD=6 ms,使用 DMA)
 */

extern void floppy_interrupt(void); // system_call.s 文件中软驱中断过程号
extern char tmp_floppy_area[1024];  // bot/head.s 文件第 2222 行处它定义的软盘缓冲区

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */ // 下面是全局变量，因为这样是将信息传给中断程序最简单的方式。它们用于当前请求的数据
static int cur_spec1=-1;
static int cur_rate=-1;
static struct floppy_struct * floppy=floppy_type;
static unsigned char current_drive =0;
static unsigned char sector =0;
static unsigned char head =0;
static unsigned char track= 0;
static unsigned char seek_track =0;
static unsigned char current_track =255;
static unsigned char command =0;
unsigned char selected =0;
struct task_struct * wait_on_floppy_select = NULL;
// 释放(取消选定的)软驱。数字输出寄存器(DOR)的低 2 位用于指定选择的软驱(0-3 对应 A-D)

void floppy_deselect(unsigned int nr){
 	if(nr != (current_DOR & 3))
 		printk("floppy_deselect: drive not selected\n\r");
 	selected = 0;
 	wake_up(&wait_on_floppy_select);
}

/*
 * floppy-change is never called from an interrupt, so we can relax a bit
 * here, sleep etc. Note that floppy-on tries to set current_DOR to point
 * to the desired drive, but it will probably not survive the sleep if
 * several floppies are used at the same time: thus the loop.
 */
/* floppy-change() 不是从中断程序中调用的，所以这里我们可以轻松一下，例如睡觉。
 * 注意 floppy-on() 会尝试设置 current_DOR 指向所需的驱动器，但当挺尸使用几个软盘时不能睡眠：
 * 因此此时只能使用循环方式
 */
int floppy_change(unsigned int nr){
repeat:
 	floppy_on(nr);  // 开启指定软驱 nr(kernel/sched.c 252)
// 若当前选择的软驱不是指定软驱 ne，且已经选择了其他软驱，则让当前任务进入可中断等待状态。
 	while ((current_DOR & 3) != nr && selected)
 		interruptible_sleep_on(&wait_on_floppy_select);
// 若当前没有选择其它软驱或者当前任务被唤醒是，当前软驱仍然不是指定的软驱 nr，则循环等待
 	if((current_DOR & 3) != nr)
 		goto repeat;
// 取数字输入寄存器值，如果最高位(位 7)置位，则表示软盘已更换，此时关闭电动机并退出返回 1。
// 否则关闭电动机退出返回 0.
 	if(inb(FD_DIR) & 0x80){
 		floppy_off(nr);
 		return 1;
 	}
 	floppy_off(nr);
 	return 0;
}

// 复制内存缓冲块
#define copy_buffer(from,to)\
__asm__("cld ; rep ; movsl"\
        ::"c" (BLOCK_SIZE/4),"S" ((long)(from)),"D" ((long)(to))\
        :"cx","di","si")
// 设置(初始化)软盘 DMA 通道
static void setup_DMA(void){
 	long addr = (long) CURRENT->buffer; // 当前请求项缓冲区所处内存中位置(地址)

 	cli();
// 若果缓冲区处于内存 1 MB 以上的地方，则将 DMA 缓冲区设在临时缓冲区域(tmp_floppy_area 数组)，
// 因为 8237A 芯片只能在 1 MB 范围内寻址。如果是写盘命令，则还需将数据复制到该临时区域。
 	if(addr >= 0x100000){
 		addr = (long) tmp_floppy_area;
 		if(command == FD_WRITE)
 			copy_buffer(CURRENT->buffer,tmp_floppy_area);
 	}
// mask DMA 2  屏蔽 DMA 通道 2
// 单通道屏蔽寄存器端口为 0x10.位 0-1 指定 DMA 通道(0~3),位 2:1 表示屏蔽，0 表示允许请求。
 	immoutb_p(4|2,10);
// output command byte. I don't know why, but everyone (minix,sanches & canton)
// output this twice, first to 12 then to 11
// 输出命令字节。我是不知道为什么，但是每个人(minix,sanches 和 canton) 都输出两次，首先是
// 12 口，然后是 11 口。
// 下面嵌入汇编代码向 DMA 控制器端口 12 和 11 薛方式字(读盘 0x46，写盘 0x4A)
  __asm__("outb %%al,$12\n\tjmp 1f\n1:\tjmp 1f\n1:\t"
 	"outb %%al,$11\n\tjmp 1f\n1:\tjmp 1f\n1:"::
 	"a" ((char) ((command == FD_READ)?DMA_READ:DMA_WRITE)));
// 8 low bits of addr  地址低 0~7 位
 	immoutb_p(addr,4);  // 向 DMA 通道 2 写入基/当前地址寄存器(端口 4)
 	addr >>= 8;
// bits 8-15 of addr  地址高 8~15 位
 	immoutb_p(addr,4);
 	addr >>= 8;
// bits 16-19 of addr  地址的高 16~19 位
// DMA 只可以在 1 MB 内存空间内寻址，其高 16~19 位地址需放入页面寄存器(端口 0x81)
 	immoutb_p(addr,0x81);
// low 8 bits of count-1 (1024-1=0x3ff)  计数器低 8 位(1024-1=0x3ff)
 	immoutb_p(0xff,5);  // 向 DMA 通道 2 写入基/当前字节计数器值(端口 5)
// high 8 bits of count-1  计数器高 8 位
 	immoutb_p(3,5); // 一次共传输 1024 B(两个扇区)
// activate DMA 2  开启 DMA 通道 2 的请求
 	immoutb_p(0|2,10);  // 复位对 DMA 通道 2 的屏蔽，开放 DMA2 请求 DREQ 信号
 	sti();
}

// 向软盘控制器输出一个字节数据(命令或参数)
static void output_byte(char byte){
 	int counter;
 	unsigned char status;

 	if(reset) return;
// 循环读取主状态控制器 FD_STATUS(0x3f4)的状态。如果状态是 STATUS_READY 并且 STATUS_DIR=0
// (CPU->FDC),则向数据端口输出指定字节
 	for(counter=0;counter < 10000;counter++){
 		status=inb_p(FD_STATUS)&(STATUS_READY|STATUS_DIR);
 		if(status==STATUS_READY){
 			outb(byte,FD_DATA);
 			return;
 		}
 	}
 	reset = 1;  // 如果到循环 1 万次结束还不能发送，则置复位标志，并打印出错信息
 	printk("Unable to send byte to FDC\n\r");
}

// 读取 FDC 执行的结果信息
// 结果最多 7 B，存放在 reply_buffer[] 中。返回读入的结果字节数，若返回值 =-1 表示出错。
static int result(void){
 	int i=0,counter,status;

 	if(reset) return -1;
 	for (counter=0;counter<10000;counter++){
 		status=inb_p(FD_STATUS)&(STATUS_DIR|STATUS_READY|STATUS_BUSY);
 		if(status==STATUS_READY) return i;
 		if(status == (STATUS_DIR|STATUS_READY|STATUS_BUSY)){
 			if(i >= MAX_REPLIES) break;
 			reply_buffer[i++] = inb_p(FD_DATA);
 		}
 	}
 	reset = 1;
 	printk("Getstatus times out\n\r");
 	return -1;
}

// 软盘操作出错中断调用函数。由软驱中断处理程序调用
static void bad_flp_intr(void){
 	CURRENT->errors++;  // 当前请求出错项出错次数增 1
// 如果当前请求项出错系数大于最大允许出错次数，则取消选定当前软驱，并家属该请求项(不更新)
 	if(CURRENT->errors>MAX_ERRORS){
 		floppy_deselect(current_drive);
 		end_request(0);
 	}
 	if(CURRENT->errors >MAX_ERRORS/2) reset = 1;
 	else recalibrate = 1;
}

/*
 * Ok, this interrupt is called after a DMA read/write has succeeded,
 * so we check the results, and copy any buffers.
 */
// 下面复制该中断处理函数是在 DMA 读/写成功后调用的，这样我们就可以检查执行结果，并复制缓冲区中的
// 数据。
// 软盘读写操作成功中断函数调用
static void rw_interrupt(void){
// 如果返回结果字节数不等于 7，或者状态字节 0、1 或 2 中存在出错标志，若是写保护就显示出错信息，
// 释放当前驱动器，并结束当前请求项。否则执行出错计数处理。然后继续执行软盘请求操作。
// (0xf8=STO_INTR|STO_SE|STO_ECE|STO_NR)
// (0xbf=ST1_EOC|ST1_CRC|ST1_OR|ST1_ND|ST1_WP|ST1_MAM ,应该是 0xb7)
// (0x73=ST2_CM|ST2_CRC|ST2_WC|ST2_BC|ST2_MAM)
 	if(result() != 7 || (ST0 & 0xf8) || (ST1 & 0xbf) || (ST2 & 0x73)){
 		if(ST1 & 0x02){
 			printk("Drive %d is write protected\n\r",current_drive);
 			floppy_deselect(current_drive);
 			end_request(0);
 		}else
 			bad_flp_intr();
 		do_fd_request();
 		return;
 	}
// 如果当前请求项的缓冲区位于 1MB 地址以上，则说明此次软盘读操作的内容还放在临时缓冲区内，需要
// 复制到请求项的缓冲区中(因为 DMA 只能在 1MB 地址范围寻址)
 	if(command == FD_READ && (unsigned long)(CURRENT->buffer) >= 0x100000)
 		copy_buffer(tmp_floppy_area,CURRENT->buffer);
// 释放当前软盘，结束当前请求项(置更新标志)，再继续执行其他软盘请求项
 	floppy_deselect(current_drive);
 	end_request(1);
 	do_fd_request();
}

// 设置 DMA 并输出软盘操作命令和参数(输出 1 字节命令 + 0~7 字节参数)
inline void setup_rw_floppy(void){
 	setup_DMA();  // 初始化软盘 DMA 通道
 	do_floppy = rw_interrupt; // 置软盘中断调用函数指针
 	output_byte(command); // 发送命令字节
 	output_byte(head<<2 | current_drive); // 发送参数(磁头号+驱动器号)
 	output_byte(track); // 发送参数(磁道号)
 	output_byte(head);  // 发送参数(磁头号)
 	output_byte(sector);  // 发送参数(起始扇区号)
 	output_byte(2);		// sector size = 512  发送参数(字节数(N=2)512 B)
 	output_byte(floppy->sect);  // 发送参数(每磁道扇区数)
 	output_byte(floppy->gap); // 发送参数(扇区间隔长度)
 	output_byte(0xFF);	// sector size (0xff when n!=0 ?)  发送参数(当 N=0 时，扇区定义的
                      // 字节长度)，这里无用
 	if(reset)           // 若在发送命令和参数时发生错误，则继续执行下一软盘操作请求。
 		do_fd_request();
}

/*
 * This is the routine called after every seek (or recalibrate) interrupt
 * from the floppy controller. Note that the "unexpected interrupt" routine
 * also does a recalibrate, but doesn't come here.
 */ // 该子程序是在每次软盘控制器寻道(或重新校正)中断后被调用的。注意 "unexpected_interrupt"
// (意外中断)子程序也会执行重新校正操作，但不在此地。
// 寻道处理中断调用函数。首先发送检测中断状态命令，获得状态信息 STO 和磁头所在磁道信息。若出错
// 则执行错误技术检测处理或取消本次软盘操作请求项。否则根据状态信息设置当前磁道变量，然后调用函数
// setup_rw_floppy() 设置 DMA 并输出软盘读写命令和参数。
static void seek_interrupt(void){
// sense drive status  检测中断状态
// 发送检测中断状态命令，该命令不带参数。返回结果信息两个字节:STO 和磁头当前磁道号。
 	output_byte(FD_SENSEI);
// 如果返回结果字节数不等于 2，或者 STO 不为寻道结束，或者磁头所在磁道(ST1)不等于设定磁道，则
// 说明发生了错误，于是执行检测错误计数处理，然后继续执行软盘请求项，并退出
 	if(result()!=2||(ST0&0xF8)!= 0x20||ST1!=seek_track){
 		bad_flp_intr();
 		do_fd_request();
 		return;
 	}
 	current_track = ST1;  // 设置当前磁道
 	setup_rw_floppy();  // 设置 DMA 并输出软盘操作命令和参互
}

/*
 * This routine is called when everything should be correctly set up
 * for the transfer (ie floppy motor is on and the correct floppy is
 * selected).
 */ // 该函数是在传输操作的所有信息都正确设置好后被调用的(即软驱电动机已经开启并且已选择了正确的
    // 软盘(软驱))
// 读写数据传输函数
static void transfer(void){
// 首先看当前驱动器参数是否就是指定驱动器的参数，若不是就发送设置驱动器参数命令及相应参数
// (参数 1:高 4 位步进速率，低 4 位磁头卸载时间；参数 2：磁头加载时间)。
 	if(cur_spec1 != floppy->spec1){
 		cur_spec1 = floppy->spec1;
 		output_byte(FD_SPECIFY); // 发送设置磁盘参数命令
 		output_byte(cur_spec1);		// hut etc  发送参数
 		output_byte(6);			// Head load time =6ms, DMA
 	}
// 判断当前数据传输速率是否与指定驱动器的一致，若不是就发送指定软驱的速率值到数据传输速率控制
// 寄存器(FD_DCR)
 	if(cur_rate != floppy->rate)
 		outb_p(cur_rate = floppy->rate,FD_DCR);
 	if(reset){  // 若返回结果信息表明出错，则再调用软盘请求函数，并返回
 		do_fd_request();
 		return;
 	}
// 若寻道标志为零(不需要寻道)，则设置 DMA 并发送相应的读写操作命令和参数，然后返回
 	if(!seek){
 		setup_rw_floppy();
 		return;
 	}
// 否则执行寻道处理。置软盘中断处理调用函数为寻道中断函数
 	do_floppy = seek_interrupt;
 	if(seek_track){ // 如果起始磁道号不等于零则发送磁头寻道命令和参数
 		output_byte(FD_SEEK);  // 发送磁头寻道命令
 		output_byte(head<<2 | current_drive);  // 发送参数：磁头号+当前软驱号
 		output_byte(seek_track); // 发送参数：磁道号
 	}else{
 		output_byte(FD_RECALIBRATE); // 发送重新校正命令
 		output_byte(head<<2 | current_drive);  // 发送参数：磁头号+当前软驱号
 	}
 	if(reset) // 如果复位标志已置位，则继续执行软盘请求项
 		do_fd_request();
}

// Special case - used after a unexpected interrupt (or reset)
// 特殊情况 - 用于意外中断(或复位)处理后
// 软驱重新校正中断调用函数。首先发送检测中断状态命令(无参数)，如果返回结果表明出错，则置复位
// 标志，否则复位重新校正标志。然后再次执行软盘请求。
static void recal_interrupt(void){
 	output_byte(FD_SENSEI); // 发送检测中断状态命令
 	if(result()!=2 || (ST0 & 0xE0) == 0x60) // 如果返回结果字节数不等于 2 或命令异常结束，
 		reset = 1; // 则置复位标志
 	else  // 否则复位重新校正标志
 		recalibrate = 0;
 	do_fd_request();  // 执行软盘请求项
}

// 意外软盘中断请求中断调用函数。
// 首先发送检测中断状态命令，如果返回结果表明出错，则置复位标志，否则置重新校正标志。
void unexpected_floppy_interrupt(void){
 	output_byte(FD_SENSEI); // 发送检测中断状态命令
 	if(result()!=2 || (ST0 & 0xE0) == 0x60) // 如果返回结果字节数不等于 2 或命令异常结束，
 		reset = 1; // 则置复位标志。
 	else  // 否则置重新校正标志。
 		recalibrate = 1;
}

// 软盘重新校正处理函数
// 向软盘控制器 FDC 发送重新校正后命令和参数，并复位重新校正标志
static void recalibrate_floppy(void){
 	recalibrate = 0;  // 复位重新校正标志
 	current_track = 0;  // 当前磁道号归零
 	do_floppy = recal_interrupt;  // 置软盘中断调用函数指针指向重新校正调用函数
 	output_byte(FD_RECALIBRATE);  // 发送命令：重新校正
 	output_byte(head<<2 | current_drive); // 发送参数：(磁头号加)当前驱动器号
 	if(reset) // 如果出错(复位标志被置位)则继续执行软盘请求。
 		do_fd_request();
}

// 软盘控制器 FDC 复位中断调用函数。在软盘中断处理程序中调用。
// 首先发送检测中断状态命令(无参数)，然后独处返回的结果字节。接着发送设定软驱参数命令和相关参数，
// 最后在此调用执行软盘请求。
static void reset_interrupt(void){
 	output_byte(FD_SENSEI); // 发送检测中断状态命令
 	(void) result();  // 读取命令执行结果字节
 	output_byte(FD_SPECIFY);  // 发送设定软驱参数命令
 	output_byte(cur_spec1);		// hut etc 发送参数
 	output_byte(6);			// Head load time =6ms, DMA
 	do_fd_request();  // 调用执行软盘请求
}

// reset is done by pulling bit 2 of DOR low for a while.
// FDC 复位是通过将数字输出寄存器(DOR)位 2 置 0 一定时间实现的
// 复位软盘控制器
static void reset_floppy(void){
 	int i;

 	reset = 0;  // 复位标志置 0
 	cur_spec1 = -1;
 	cur_rate = -1;
 	recalibrate = 1;  // 重新校正标志置位
 	printk("Reset-floppy called\n\r");  // 显示执行软盘复位操作信息
 	cli();  // 关中断
 	do_floppy = reset_interrupt;  // 设置在软盘中断处理程序中调用的函数
 	outb_p(current_DOR & ~0x04,FD_DOR); // 对软盘控制器 FDC 执行复位操作
 	for (i=0;i<100;i++) // 空操作，延迟
 		__asm__("nop");
 	outb(current_DOR,FD_DOR); // 在启动软盘控制器
 	sti();  // 开中断
}
// 软驱启动定时中断调用函数。首先检查数字输出寄存器(DOR)，使其选择当前指定的驱动器。然后调用
// 执行软盘读写传输函数 transfer().
static void floppy_on_interrupt(void){
// We cannot do a floppy-select, as that might sleep. We just force it
// 我们不鞥任意设置选择的软驱，因为这样做可能会引起进程睡眠。我们只是迫使它自己选择
 	selected = 1; // 置已选择当前驱动器标志
// 如果当前驱动器号与数字输出寄存器 DOR 中的不同，则重新设置 DOR 为当起啊驱动器 current_drive.
// 定时延迟 2 个抵达时间，然后调用软盘读写传输函数 transfer().否则直接调用软盘读写传输函数
 	if(current_drive!=(current_DOR&3)){
 		current_DOR&=0xFC;
 		current_DOR|=current_drive;
 		outb(current_DOR,FD_DOR);  // 想数字输出寄存器输出当前 DOR
 		add_timer(2,&transfer);  // 添加定时器并执行传输函数
 	}else
 		transfer();  // 执行软盘读写传输函数
}

// 软盘读写请求项处理函数
void do_fd_request(void){
 	unsigned int block;

 	seek = 0;
 	if(reset){  // 如果复位标志已置位，则执行软盘复位操作，并返回
 		reset_floppy();
 		return;
 	}
 	if(recalibrate){  // 如果重新校正标志已置位，则执行软盘重新校正操作，并返回
 		recalibrate_floppy();
 		return;
 	}
 	INIT_REQUEST; // 检测请求项的合法性(参见 kernel/blk_drv/blk.h 123)
// 将请求项结构中软盘设备号中的软盘类型(MINOR(CURRENT->dev)>>2)作为索引取得软盘参数块
 	floppy = (MINOR(CURRENT->dev)>>2) + floppy_type;
// 如果当前驱动器不是请求项中指定的驱动器，则置标志 seek，标志需要进行寻道操作。然后置请求项设备
// 为当前驱动器
 	if(current_drive != CURRENT_DEV) seek = 1;
 	current_drive = CURRENT_DEV;
// 设置读写起始扇区。因为每次读写是以块为单位(1 块 2 个扇区)，所以起始扇区需要起码比磁盘总扇区数
// 小 2 个扇区。否则结束该次软盘请求项，执行下一个请求项
 	block = CURRENT->sector;  // 去当前软盘请求项中起始扇区号-->block
 	if(block+2 > floppy->size){ // 如果 block+2 大于磁盘扇区总数，则
 		end_request(0);  // 结束本次软盘请求项
 		goto repeat;
 	}
// 球队赢才磁道上的扇区号，磁头号，磁道号，搜寻磁道号(对于软驱读不同格式的盘)
 	sector = block % floppy->sect;  // 起始扇区对每磁道扇区数取模，得磁道上扇区号
 	block /= floppy->sect;  // 起始扇区对每磁道扇区数取整，得起始磁道数
 	head = block % floppy->head;  // 起始磁道数对磁头数取模，得操作的磁头号
 	track = block / floppy->head; // 起始磁道数对磁头数取整，得操作的磁道号
 	seek_track = track << floppy->stretch;  // 相应于驱动器中盘类型进行调整，的寻道号。
// 如果寻道号与当前磁头所在磁道不同，则置需要寻道标志 seek
 	if(seek_track != current_track)  seek = 1;
 	sector++; // 磁盘上世纪扇区计数是从 1 算起
 	if(CURRENT->cmd == READ)  // 如果请求项中是读操作，则置软盘读命令码
 		command = FD_READ;
 	else if(CURRENT->cmd == WRITE)  // 如果请求项中是写操作，则置软盘写命令码
 		command = FD_WRITE;
 	else
 		panic("do_fd_request: unknown command");
// 添加定时器，用于指定驱动器到能正常运行所诉掩饰的时间(滴答数)，当定时时间到就调用函数 floppy_on_interrupt().
 	add_timer(ticks_to_floppy_on(current_drive),&floppy_on_interrupt);
}
// 软盘系统初始化。
// 设置软盘块设备的请求处理函数(do_fd_request()),并设置软盘中断门(int 0x26,对应硬件中断请
// 求信号 IRQ6)，然后取消对该中断信号的屏蔽，允许软盘控制器 FDC 发送中断请求信号
void floppy_init(void){
  blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;  // =do_fd_request()
 	set_trap_gate(0x26,&floppy_interrupt);  // 设置软盘中断门 int 0x26(38)
	outb(inb_p(0x21)&~0x40,0x21);  // 复位软盘的中断请求屏蔽位，允许软盘控制器发送中断请求信号。
}
