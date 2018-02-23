//
#define __LIBRARY__ // 定义该变量是为了包括定义在 unistd.h 中的内嵌汇编代码等信息
#include<unistd.h>
// *.h 头文件所在的默认目录是 include/ ,则在代码中就不用明确指明位置。如果不是 UNIX 标准头
// 文件，则需要指明所在的目录，并用双引号括住。标准符号常数与文件类型：该文件中定义了各种符号
// 常数和类型，并申明了各种函数。如果定义了 __LIBRARY__ ，则还包括系统调用号和内嵌汇编 _syscall0() 等。
#include<time.h>  //时间类型头文件。其中最主要定义了 tm 结构和一些有关时间的函数原形。

/*
 * we need this inline - forking from kernel space will result in NO COPY ON WRITE(!!!),
 * until an execve is executed.This is no problem,but for the stack.This handled
 * by not letting main() use the stack at all after fork().Thus,no function calls -
 * which means inline code for fork too,as otherwise we would ise the stack upon
 * exit from 'fork()'
 *
 * Actually only pause and fork are needed inline,so that there won't be any messing
 * with the stack from main(),but we define some others too.
 */
/*
 * 我们需要下面这些内嵌语句--从内核空间创建进程将导致没有写时复制(COPY ON WRITE !!!),直到执行一个
 * execve 调用。这对堆栈可能带来问题。处理方法是在 fork() 调用之后不让 main() 使用堆栈。因此就不能
 * 有函数调用--这意味着 fork  也要使用内嵌代码，否则在从 fork() 退出时就要使用堆栈了。实际上
 * 只有 pause 和 fork 需要使用内嵌方式，以保证从 main() 中不会弄乱堆栈，但是我们还同时定义了
 * 其他一些内嵌宏函数。
 */
static inline _syscall0(int,fork)
// 是 unistd.h 中的内嵌宏。以嵌入汇编的形式调用 Linux 的系统调用中断 0x80.该中断是所有系统调用
// 的入口。该条语句实际上是 int fork() 创建进程系统调用。syscall0 名称中最后的 0 表示无参数，
// 1 表示 1 个参数。参见 include/unistd.h 22222 行。
static inline _syscall0(int,pause)  // pause 系统调用：暂停进程的执行，直到收到一个信号
static inline _syscall1(int,setup,void *,BIOS)  // int setup(void * BIOS) 系统调用
static inline _syscall0(int,sync) //int sync() 系统调用：更新文件系统

#include<linux/tty.h> // tty 头文件，定义了有关 tty_io,串行通信方面的参数、常数
#include<linux/sched.h> // 调度程序头文件，定义了任务结构 task_struct 、第 1 个初始任务的
                        // 数据。还有一些以宏的形式定义的有关描述符参数设置和获取的嵌入式汇编函数程序
#include<linux/head.h>  // head 头文件，定义了段描述符的简单结构，和几个选择符常量
#include<asm/system.h>  // 系统头文件。以宏的形式定义了嘻嘻嘻许多有关设置或修改描述符/中断门等的
                        // 嵌入式汇编子程序。
#include<asm/io.h>      // io 头文件。以宏的嵌入汇编程序形式定义对 io 端口操作的函数。

#include<stddef.h>  // 标准定义头文件。定义了 NULL、offsetof(TYPE,MEMBER)
#include<stdarg.h>  // 标准参数头文件。以宏的形式定义变量参数列表。主要说明了一个类型(va_list)
                    // 和三个宏(va_start,va_arg,va_end),vsprintf,vprintf,vfprintf.
#include<unistd.h>
#include<fcntl.h> // 文件控制头文件。用于文件及其描述符的操作控制常数符号的定义。
#include<sys/types.h> // 类型头文件。定义了基本的系统数据类型。

#include<linux/fs.h>  // 文件系统头文件。定义文件表结构(file,buffer_head,m_inode 等)。

static char printbuf[1024]; // 静态字符串数组，用作内核显示信息的缓存。

extern int vsprintf();  // 送格式化输出到一字符串中(在 kernel/vsprintf.c 103 行)
extern void init(void); // 函数原形，初始化(在 198 行)。
extern void blk_dev_init(void); // 块设备初始化子程序(blk_drv/ll_rw_blk.c 168 行)
extern void chr_dev_init(void); // 字符设备初始化(chr_drv/tty_io.c 399 行)
extern void hd_init(void);  // 硬盘初始化程序(blk_drv/hd.c 361 行)
extern void floppy_init(void);  // 软驱初始化程序(blk_drv/floppy.c 524 行)
extern void mem_init(long start,long end);  // 内存管理初始化(mm/memory.c 2222 行)
extern long rd_init(long mem_start,long length);  // 虚拟盘初始化(blk_drv/ramdisk.c 61 行)
extern long kernel_mktime(struct tm* tm); // 建立内核时间(秒)
extern long startup_time; // 内核启动时间(开机时间)(秒)

/*
 * This is set up by the setup-routine at boot-time
 */
/* 以下这些数据是由 setup.s 程序在引导时间设置的(参见 boot/setup.s)
 */

#define EXT_MEM_K(*(unsigned short *)0x90002) // 1 MB 以后的扩展内存大小(KB)
#define DRIVE_INFO(*(struct drive_info *)0x90080) // 硬盘参数表所在地址
#define ORIG_ROOT_DEV(*(unsigned short *)0x901FC) // 根文件系统所在设备号

/*
 * Yeah,yeah,it's ugly,but I cannot find how to do this correctly and this seems to
 * work.If anybody has more info on the real-time clock I'd be interested.Most of
 * this was trial and error,and some bios-listing reading.Urghh.
 */
/* 下面这段程序很差劲，但我不知道如何正确实现，而且好象它还能运行。如果有关于实时时钟更多的资料，
 * 那我很感兴趣。这些都是试探出来的买另外还看了一些 BIOS 程序。
 */
#define CMOS_READ(addr) ({\ // 这段宏读取 CMOS 实时时钟信息
  outb_p(0x80 | addr,0x70);\  // 0x70 是写端口号，0x80 |addr 是要读取的 CMOS 内存地址
  inb_p(0x71);\ // 0x71 是读端口号
  })

#define BCD_TO_BIN(val) ((val)=((val)&15)+((val)>>4)*10)  // 将 BCD 码转换成数字

static void time_init(void) // 该子程序取 CMOS 时钟，并设置开机时间-> startup_time(秒)
{
  struct tm time;
  // 以下循环操作用于控制时间误差在 1 秒之内
  do{
    time.tm_sec=CMOS_READ(0);
    time.tm_min=CMOS_READ(2);
    time.tm_hour=CMO_READ(4);
    time.tm_mday=CMOS_READ(7);
    time.tm_mon=CMOS_READ(8);
    time.tm_year=CMOS_READ(9);
  } whlie(time.tm_sec != CMOS_READ(0));
  BCD_TO_BIN(time.tm_sec);
  BCD_TO_BIN(time.tm_min);
  BCD_TO_BIN(time.tm_hour);
  BCD_TO_BIN(time.tm_mday);
  BCD_TO_BIN(time.tm_mon);
  BCD_TO_BIN(time.tm_year);
  time.tm_mon--;  // tm_mon 中月份范围是 0～11
  startup_time=kernel_mktime(&time);
}

static long memory_end=0; // 机器具有的内存(字节数)
static long buffer_memory_end=0;  // 高速缓冲区末端地址
static long main_memory_start=0;  // 主内存(将用于分页)开始的位置

struct drive_info{char dummy[32];} drive_info;  // 用于存放硬盘参数表信息

void main(void){  // This really IS void,no error here. The startup routine assumes
                  // (well,...) this
                  // 这里确实是 void 没错。在 startup 程序(head.s)中就是这样假设的
                  // 参见 head.s 程序第 180 行开始的几行代码。
/*
 * Interrupts are still disabled.Do necessary setups,then enable them.
 */
/*
 * 此时中断仍被禁止着，做完必要的设置后就将其开启。
*/
// 这段代码用于保存：根设备号-> ROOT_DEV; 高速缓存末端地址-> buffer_memory_end;
// 机器内存数-> memory_end; 主存开始地址-> main_memory_start.
  ROOT_DEV=ORIG_ROOT_DEV; // ROOT_DEV 定义在 super.c 2222 行。
  drive_info=DRIVE_INFO;
  memory_end=(1<<20)+(EXT_MEM_K<<10); // 内存大小=1 MB+扩展内存(KB)*1024
  memory_end&=0xfffff000; // 忽略不到 4 KB (1 页)的内存数
  if(memory_end>16*1024*1024) // 如果内存超过 16 MB，则按照 16 MB 计。
    memory_end=16*1024*1024;
  if(memory_end>12*1024*1024) // 如果内存 > 12 MB，则设置缓冲区末端=4 MB
    buffer_memory_end=4*1024*1024;
  else if(memory_end>6*1024*1024) // 否则如果内存 > 6 MB，则设置缓冲区末端=2 MB
    buffer_memory_end=2*1024*1024;
  else
    buffer_memory_end=1*1024*1024;  // 否则设置缓冲区末端=1 MB
  main_memory_start=buffer_memory_end;  // 主内存起始位置=缓冲区末端
#ifdef RAMDISK  // 如果定义了虚拟盘，则初始化虚拟盘。此时主内存将减少
  main_memory_start += rd_init(main_memory_start,RAMDISK*1024);
#endif
// 以下是内核进行所有初始化工作。阅读钱最好跟着调用的程序深入进去看，若实在看不下去了，就先放一放，
// 继续看下一个初始化调用--这是经验之谈。
  mem_init(main_memory_start,memory_end);
  trap_init();  // 陷阱门(中断向量)初始化。(kernel/traps.c 159 行)
  blk_dev_init(); // 块设备初始化(kernel/blk_drv/ll_rw_blk.c 168 行)
  chr_dev_init(); // 字符设备初始化(kernel/chr_dev/tty_io.c 107 行)
  tty_init(); // tty 初始化(kernel/chr_drv/tty_io.c 107 行)
  time_init();  // 设置开机启动时间-> startup_time (见 89 行)
  sched_init(); // 调度程序初始化(加载了任务 0 的 tr ，ldtr )(kernel/sched.c 403 行)
  buffer_init();  // 缓冲管理初始化，建内存链表等(fs/buffer.c 2222 行)
  hd_init();  // 硬盘初始化(kernel/blk_drv/hd.c 361 行)
  floppy_init();  // 软驱初始化(kernel/blk_drv/floppy.c 524 行)
  sti();  // 所有初始化工作都完成了，开启中断
// 下面过程通过在堆栈中设置的参数，利用中断返回指令启动第一个任务(task0)
  move_to_user_mode();  // 移到用户模式下运行(include/asm/system.h 第 1 行)
  if(!fork()){  // we count on this going ok   我们全靠它了
    init(); // 在新建的子进程(任务 1)中执行
  } //下面的代码开始以任务 0 的身份运行。
/*
 *  NOTE!! For any other task 'pause()' would mean we have to get a signal to
 * awaken,but task0 is the sole exception (see 'schedule()') as task0 gets
 * activated at every idle moment (when no other tasks can run).For task0 'pause()'
 * just means we go check if some other task can run,and if not we return here.
 */
/* 注意！！对于任何其他的任务，'pause()' 将意味着我们必须等待收到一个信号才会返回就绪运行，
 * 但任务 0 (task0) 是唯一的例外情况 (参见 'schedule()')，因为任务 0 在任何空闲时间里都会
 * 被激活(当没有其他任务在运行时)，因此对于任务 0 'pause()' 仅意味着我们返回来查看是否有其他任务
 * 可以运行，如果没有的话我们就回到这里，一直循环执行 'pause()'.
 */
 for(;;)pause();
}
// 下面函数产生格式化信息并输出到标准输出设备 stdout(1),这里是指在屏幕上显示。参数 '* fmt'
// 指定输出将采用的格式，参见各种标准 C 语言书籍。该子程序正好是 vsprintf 如何使用的一个例子。
// 该程序使用 vsprintf() 将格式化的字符串放入 printbuf 缓冲区，然后用 write() 将缓冲区的
// 内容输出到标准设备。参见程序 kernel/vsprintf.c
static int printf(const char *fmt,...){
  va_list args;
  int i;

  va_start(args,fmt);
  write(1,printbuf,i=vsprintf(printbuf,fmt,args));
  va_end(args);
  return i;
}

static char * argv_rc[]={ "/bin/sh",NULL};  // 调用执行程序时参数的字符串数组。
static char * envp_rc[]={"HOME=/",NULL};  // 调用执行程序时的环境字符串数组。

static char * argv[]={ "-/bin/sh",NULL};  // 同上
static char * envp[]={ "HOME=/usr/root",NULL};

// init() 函数运行在任务 0 创建的子进程(任务 1)中。它首先对第一个要执行的程序(shell)的环境
// 进行初始化，然后加载该程序并执行之。
void init(void){
  int pid,i;
// 读取硬盘参数包括分区表信息并建立虚拟盘和安装根文件系统设备。该函数是在 31 行上的宏定义的，
// 对应函数是 sys_setup(),在 kernel/blk_drv/hd.c 74 行
  setup((void *) &drive_info);
  (void)open("/dev/tty0",O_RDWR,0); // 用读写访问方式打开设备 "dev/tty0",这里对应
                                    // 终端控制台。返回的句柄号 0 --stdin 标准输入设备
  (void)dup(0);  // 复制句柄，产生句柄 1 号-- stdout 标准输出设备
  (void)dup(0);  // 复制句柄，产生句柄 2 号-- stderr 标准出错输出设备
  printf("%d buffers= %d bytes\n\r",NR_BUFFERS,NR_BUFFERS*BLOCK_SIZE);
    // 打印缓冲区块数和总字节数，每块 1024 字节。
  printf("Free mem: %d bytes\n\r",memory_end - main_memory_start);  // 空闲内存字节数
// 下面 fork() 用于创建一个子进程(子任务)。对于被创建的子进程，fork() 将返回 0 值，对于原(父进程)
// 将返回子进程的进程号。所以 215-219 句是子进程执行的内容。该子进程关闭了句柄 0(stdin),
// 以只读方式打开 /etc/rv 文件，并执行 /bin/sh 程序，所带参数和环境变量分别由 argv_rc 和
// envp_rc 数组给出。参见后面的描述。
  if(!(pid=fork())){
    close(0);
    if (open("/etc/rc",O_RDONLY,0))
      _exit(1); // 如果打开文件失败,则退出(/lib/_exit.c,2222)
    execve("/bin/sh",argv_rc,envp_rc);  // 装入 /bin/sh 程序并执行
    _exit(2); // 若 execve() 执行失败则退出(出错码 2,"文件或目录不存在")
  }
// 下面是父进程执行的语句.wait() 是等待子进程停止或终止,其返回值应是子进程的进程号(pid).
// 这三句的作用是父进程等待子进程的结束. &i 是存放返回状态信息的位置.如果 wait() 返回值不等于
// 子进程号,则继续等待.
  if(pid>0)
    while(pid!=wait(&i))
      ;// nothing
// 如果执行到这里，索命刚创建的子进程的执行已停止或终止了。下面循环中首先再创建一个子进程，如果出错，
// 则显示 "初始化程序创建子进程失败" 的信息并继续执行。对于所创建的子进程关闭所有以前还遗留的句柄
// (stdin,stdout,stderr),新创建一个会话并设置进程组号，然后重新打开 /dev/tty0 作为 stdin,
// 并复制成 stdout 和 stderr 。再次执行系统解释程序 /bin/sh.但这次执行所选用的参数和环境
// 数组另选了一套(见上面 193-195 行)。然后父进程再次运行 wait() 等待。如果子进程又停止了执行，
// 则在标准输出上显示出错信息“子进程 pid 停止了运行，返回码是 i”，然后继续重试下去。。。，形成
// “大”死循环。
  while(1){
    if((pid=fork())<0){
      printf("Fork failed in init\r\n");
      continue;
    }
    if (!pid){
      colse(0);close(1);colse(2);
      setsid();
      (void) open("/dev/tty0",O_RDWR,0);
      (void) dup(0);
      (void) dup(0);
      _exit(execve("/bin/sh",argv,envp));
    }
    while(1)
      if(pid==wait(&i))
        break;
    printf("\n\rchild %d died with code %04x\n\r",pid,i);
    sync();
  }
  _exit(0); // NOTE! _exit,not exit()
}
