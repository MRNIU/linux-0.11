/*
 * 'sched.c' is the main kernel file.It contains scheduling primitives(sleep_on,
 * wakeup,schedule etc) as well as a number of simple system call functions(type
 * getpid(),which just extracts a field from current-task
 */
/* 'sched.c' 是主要的内核文件。其中包括有关调度的基本函数(sleep_on,wakeup,schedule 等)
 * 以及以下简单地系统调用函数(比如 getpid(),仅从当前任务中获取一个字段)
 */
#include<linux/sched.h> // 调度程序头文件。定义了任务结构 task_struct 等进程数据和函数
#include<linux/kernel.h>  // 内核头文件。含有一些内核常用函数的原形定义。
#include<linux/sys,h> // 系统调用头文件。含有 72 个系统调用 C 函数处理程序，以 'sys_' 开头
#include<linux/fdreg.h> // 软驱头文件。含有软盘控制器参数的一些定义。
#include<asm/system.h>  // 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏
#include<asm/io.h>  // io 头文件。定义硬件端口输入/输出宏汇编语句
#include<asm/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。

#include<signal.h>  // 信号头文件。定义信号符号常量，sigaction 结构，操作函数原型。
// 下面取信号 nr 在信号位图中对应位的二进制数值。信号编号 1~32.
#define _S(nr) (1<<((nr)-1))  // 比如信号 5 的位图数值=1<<(5-1)=16=00010000b
#define _BLOCKABLE (~(_S(SIGKILL)|_S(SIGSTOP))) // 除了 SIGKILL 和 SIGSTOP 信号外其它
                                                // 都是可阻塞的(...10111111111011111111b)
// 显示任务号 nr 的进程号、进程状态和内核堆栈空闲字节数(大约)。
void show_task(int nr,struct task_struct * p){
  int i,j=4096-sizeof(struct task_struct);

  printk("%d:pid= %d,state=%d,",nr,p->pid,p->state);
  i=0;
  while(i<j&&!((char *)(p+1))[i]) // 检测指定任务数据结构以后等于 0 的字节数
    i++;
  printk("%d(of %d)chars free in kernel stack\n\r",i,j);
}
// 显示所有任务的任务号、进程号、进程状态和内核堆栈空闲字节数(大约)。
void show_stat(void){
  int i;

  for(i=0;i<NR_TASKS;i++) // NR_TASKS 是系统能容纳的最大进程(任务)数量(64 个)
    if(task[i]) // 定义在 include/kernel/sched.h 第 4 行
      show_task(i,task[i]);
}
// 设置定时芯片 8253 的计数初值，参见 432 行
#define LATCH (1193180/Hz)

extern void mem_use(void);  // 没有任何地方定义和引用该函数

extern int timer_interrupt(void); // 时钟中断处理程序(kernel/system_call.s 204 行)
extern int system_call(void); // 系统调用中断处理程序(kernel/system_call.s 82)

union task_union{ // 定义一个任务联合(任务结构成员和 task 字符数组程序成员)
  struct task_struct task;  // 因为一个任务数据结构与其堆栈放在同一内存页中，所以从堆栈段寄存器
  char stack[PAGE_SIZE];  // ss 可以获得其数据段选择符。
};

static union task_union init_task={INIT_TASK,}; // 定义初始任务的数据(sched.h 中)
// 下面的限定符 volatile,英文解释是易变、不稳定的意思。这里是要求 gcc 不要对该变量进行优化
// 处理，也不要挪动位置，因为也许别的程序会来修改它的值。
long volatile jiffies=0;  // 从开机开始算起的滴答数时间值(10ms/滴答)
long startup_time=0;  // 开机时间。从 1970:0:0:0 开始计时的秒数
struct task_struct *current=&(init_task.task);  // 当前任务指针(初始化为初始任务)
struct task_struct *last_task_used_math=NULL; // 使用过协处理器任务的指针

struct task_struct *task[NR_TASKS]={&(init_task.task),};  // 定义任务指针数组

long user_stack [PAGE_SIZE>>2]; // 定义堆栈任务 0 的用户态，4 K。
// 该结构用于设置堆栈 ss:esp(数据段选择符，指针)，见 head.s 第 29 行。指针在最后一项。
struct{
    long * a;
    short b;
} stack_start={&user_stack[PAGE_SIZE>>2],0x10};
/*
 *  'math_state_restore()' saves the current math information in the old math
 * state array,and gets the new ones from the current task.
 */
/* 将当前协处理器内容保存到老协处理器状态数组中，并将当前任务的协处理器内容加载斤协处理器
 */
// 当前任务被调度交换过以后，该函数用一保存原任务的协处理器状态(上下文)并恢复新调度进来的当前任务的
// 协处理器执行状态。
void math_state_restore(){
  if(last_task_used_math==current)  // 如果任务没变则返回(上一个任务是当前任务)
    return; // 这里所指的“上个任务”是刚被交换出去的任务
  __asm__("fwait"); // 在发送协处理器指令之前要先发 WAIT 指令
  if(last_task_used_math) // 如果上个任务使用了协处理器，则保存其状态
    __asm__("fnsave %0"::"m"(last_task_used_math->tss.i387));
    // 现在，last_task_used_math 指向当前任务
  last_task_used_math=current;  // 以备当前任务被交换出去时使用
  if(current->used_math)  // 如果当前任务用过协处理器，则恢复其状态
    __asm__("frstor %0"::"m"(current->tss.i387));
  else{ // 否则说明是第一次使用
    __asm__("fninit"::);  // 于是就向协处理器发初始化指令
    current->used_math=1; // 并设置使用了协处理器标志。
  }
}

/*
 *  'schedule()' is the scheduler function.This is GOOD CODE! There probably won't
 * be any reason to change this,as it should work well in all circumstances(ie gives
 * IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 * NOTE!! Task 0 is the 'idle' task,which gets called when no other tasks can
 * run,It can not be killed,and it cannot sleep.The 'state' information in task[0]
 * is never used.
 */
/* 'schedule()' 是调度函数。这是个很好的代码！没有任何理由对它进行修改，因为它可以在所有的环境下
 * 工作(例如能对 IO-边界处理很好的相应等)。只有一件事值得留意，那就是这里的信号处理代码。
 * 注意！！！ 任务 0 是个闲置('idle')任务，只有当没有其他任务可以运行时才调用它。它不能被杀死，也不能睡眠。
 * 任务 0 中的状态信息 'state' 是从来不用的
 */
void schedule(void){
  int i,next,c;
  struct task_struct * * p;
// 检测 alarm (进程的警报定时值),唤醒任何已得到信号的可中断任务
// check alarm,wake up any interruptible tasks that ahve got a signal
// 从任务数组中最后一个任务开始检测 alarm
  for(p=&LAST_TASK;p>&FIRST_TASK;--p)
    if(*p){
// 如果没有设置过任务的 alarm 并且任务的 alarm 时间已经过期(alarm<jiffies)，则在信号位图中
// 置 SIGALRM 信号，然后清 alarm。jiffies 是系统从开机算起的滴答数(10ms/滴答)。定义在 sched.h
// 第 22222 行。
      if((*p)->alarm && (*p)->alarm<jiffies){
          (*p)->signal |=(1<<(SIGALRM-1));
          (*p)->alarm=0;
      }
// 如果信号位图中除被阻塞的信号外还有其他信号，并且任务处于可中断状态，则置任务为就绪状态。
// 其中 '~(_BLOCKABLE &(*p)->blocked)' 用于忽略被阻塞的信号，但 SIGKILL 和 SIGSTOP
// 不能被阻塞。
      if(((*p)->signal & ~(_BLOCKABLE & (*p)->blocked))&&(*p)->state==TASK_INTERRUPTIBLE)
        (*p)->state=TASK_RUNNING; // 置为就绪
  }
// this is the schedule proper:
// 这里是调度程序的主要部分
  while(1){
    c=-1;
    next=0;
    i=NR_TASKS;
    p=&task[NR_TASKS];
// 这段代码也是从任务数组最后任务开始循环处理，并跳过不含任务的槽。比较每个就绪状态任务的 counter
// (任务运行时间的递减计数)值，哪个值大，运行时间还不长，next 就指向哪个任务号。
    while(--i){
      if(!*--p))
        continue;
      if((*p)->state==TASK_RUNNING && (*p) -> counter>c)
        c=(*p)->counter,next=i;
    }
// 如果比较得出有 counter 值大于 0 的结果，则退出 131 行开始的循环，执行任务切换 152 行
    if(c) break;
// 否则就根据每个任务的优先权值，更新每一个任务的 counter 值，然后回到 132 行重新进行比较。
// counter 值的计算方式为 counter=counter/2+priority。这里不考虑进程的状态。
    for(p=&LAST_TASK;p>&FIRST_TASK;--p)
      if(*p)
        (*p)->counter=((*p)->counter>>1)+(*p)->priority;
  }
  switch_to(next);  // 切换到任务号为 next 的任务，并运行之。若无其他任务则运行任务 0.
}
// pause() 系统调用。转换当前任务的状态为可中断的等待状态，并重新调度。
// 该系统调用将导致进程进入睡眠状态，直到收到一个信号。该信号用于中止进程或者使进程调用一个
// 信号捕获函数。只有当捕获了一个信号，并且信号捕获处理函数返回，pause() 才会返回。此时 pause()
// 返回值应该是 -1 ，并且 errno 被置为 EINTR.这里还没有完全实现 (直到 0.95 版)
int sys_pause(void){
  current->state=TASK_INTERRUPTIBLE;
  schedule();
  return 0;
}
// 把任务置为不可中断的等待状态，并让睡眠队列头的指针指向当前任务。只有明确地唤醒时才会返回。
// 该函数提供了进程与中断处理程序之间的同步机制。函数参数 *p 是放置等待任务的队列头指针。
void sleep_on(struct task_struct * * p){
  struct task_struct * tmp;
// 若指针无效，则退出(指针所指的对象可以是 NULL，但指针本身不会为 0)。
  if(!p)  return;
  if(current== &(init_task.task)) // 如果当前任务是任务 0，则死机(不可能).
    panic("task[0] trying to sleep");
  tmp=*p; // 让 tmp 指向已经在等待队列上的任务(如果有的话)
  *p=current; // 将睡眠队列头的等待指针指向当前任务。
  current=->state=TASK_UNINTERRUPTIBLE; // 将当前任务置为不可中断的等待状态
  schedule(); // 重新调度
// 只有当这个等待任务被唤醒时，调度程序才又返回到这里，则表示进程已被明确地唤醒。既然大家都在等待
// 相同的资源，那么在资源可用时，就有必要唤醒所有等待该资源的进程。该函数嵌套调用，也会嵌套唤醒
// 所有等待该资源的进程。
  if(tmp) // 若还存在等待的任务，则也将其置为就绪状态(唤醒)
    tmp->state=0;
}
// 将当前任务置为可中断的等待状态，并放入 *p 指定的等待队列中。
void interruptible_sleep_on(struct task_struct * * p){
  struct task_struct * tmp;
  if(!p) return;
  if(current==&(init_task.task))
    panic("task[0] trying to sleep");
  tmp=*p;
  *p=current;
repeat: current->state=TASK_INTERRUPTIBLE;
  schedule();
// 如果等待队列中还有任务，并且队列头指针所指向的任务不是当前任务时，则将该等待任务置为就绪状态，
// 并重新执行调度程序。当指针 *p 所指向的不是当前任务时，表示在当前任务被放入队列后，又有新的
// 任务被插入等待队列中，因此，就应该将所有其它等待任务也置为可运行状态。
  if(*p && *p != current){
    ( * * p).state=0;
    goto repeat;
  }
// 下面一句代码有误，应该是 *p=tmp ，让队列头指针指向其余等待任务，否则在当前任务之前插入等待队列
// 的任务均被抹掉了。当然，同时也需要删除 208 行上的语句
  *p=NULL;
  if(tmp)
    tmp->state=0;
}
// 唤醒指定任务 *p ，通常 *p 是任务等待队列头指针。
void wake_up(struct task_struct * * p){
  if(p && *p){
    (**p).state=0;  // 置为就绪(可运行)状态
    *p=NULL;
  }
}

/*
 * Ok,here are some floppy things that shouldn't be in the kernel proper.They are
 * here because the floppy needs a timer,and this was the easiest way of doing it.
 */
/* 好了，从这里开始是一些有关软盘的子程序，本不应该放在内核的主要部分中的。将它们放在这里是因为
 * 软驱需要定时操作，而放在这里是最方便的方法。
 */
static struct task_struct * wait_motor[4]={NULL,NULL,NULL,NULL};  // 等待电动机加速进程的指针数组
static int mon_timer[4]={0,0,0,0};  // 存放软驱启动加速所需时间值(滴答数)。
static int moff_timer[4]={0,0,0,0}; // 存放软驱电动机停转之前需维持 L 时间，默认为 10000 滴答
unsigned char current_DOR=0x0C; // 数字输出寄存器(初值:允许 DMA 和请求中断、启动 FDC)
// 指定软盘到正常运转状态所需延迟滴答数(时间)。 nr -- 软驱号(0-3)，返回值为滴答数。
int ticks_to_floppy_on(unsigned int nr){
  extern unsigned char selected;  // 当前选中的软盘号(blk_drv/floppy.c 22222)
  unsigned char mask=0x10<<nr;  // 所选软驱对应数字输出寄存器中启动马达比特位。

  if(nr>3)
    panic("floppy_on: nr>3"); // 最多 4 个软驱
  moff_timer[nr]=10000; // 100 s =very big :-)
  cli();
  mask|=current_DOR;
  if(*selected){  // 若非当前软驱，则先复位其它软驱选择位，然后置对应软驱选择位
    mask&=0xFC;
    mask|=nr;
  }
// 如果数字输出寄存器的当前值与要求的值不同，则向 FDC 数字输出端口输出新值(mask)。并且如果要求启动
// 的电动机还没有启动，则置相应软驱的电动机启动定时器值(HZ/2=0.5 秒或 50 个滴答)
// 此后更新当前数字输出寄存器值 current_DOR.
  if(mask!=current_DOR){
    outb(mask,FD_DOR);
    if((mask^current_DOR)&0xf0)
      mon_timer[nr]=HZ/2;
    else if(mon_timer[nr]<2)
      mon_timer[nr]=2;
    current_DOR=mask;
  }
  sti();
  return mon_timer[nr];
}
// 等待指定软驱电动机启动所需时间
void floppy_on(unsigned int nr){
  cli();  // 关中断
  while(ticks_to_floppy_on(nr)) // 如果电动机启动定时还没到，就一直把当前进程置为不可中
    sleep_on(nr+wait_motor);  // 断睡眠状态并放入等待电动机运行的队列中。
  sti();  // 开中断。
}
// 置关闭相应软驱电动机停转定时器(3 秒)
void floppy_off(unsigned int nr){
  moff_timer[nr]=3*HZ;
}
// 软盘定时处理子程序。更新电动机启动定时器值和电动机关闭停转计时值。该子程序是在时钟定时中断
// 中被调用，因此每一个滴答(10 ms)被调用一次，更新电动机开启或停转定时器的值。如果某一个电
// 动机停转时间到，则将数字输出寄存器电动机启动位复位。
void do_floppy_timer(void){
  int i;
  unsigned char mask=0x10;

  for(i=0;i<4;i++.mask<<=1){
    if(!(mask & current_DOR)) // 如果不是 DOR 指定的电动机则跳过
      continue;
    if(mon_timer[i]){
      if(!--mon_timer[i])
        wake_up(i+wait_motor);  // 如果电动机启动定时到则唤醒进程。
    }
    else if(!moff_timer[i]){  // 如果电动机停转定时到则复位相应电动机启动位，并更新数字输出寄存器
      current_DOR &= ~mask;
      outb(current_DOR,FD_DOR);
    }
    else
      moff_timer[i]--;  // 电动机停转计时递减。
  }
}

#define TIME_REQUESTS 64  // 最多可有 64 个定时器
// 定时器链表结构和定时器数组
static struct timer_list{
  long jiffies; // 定时器滴答数
  void (*fn)(); // 定时处理程序
  struct timer_list *next;  // 下一个定时器
} timer_list[TIME_REQUESTS], *next_timer=NULL;
// 添加定时器。输入参数为指定的定时值(滴答数)和相应的处理程序指针。
// jiffies - 以 10 毫秒计的滴答数； *fn() - 定时时间到时执行的函数
void add_timer(long jiffies,void (*fn)(void)){
  struct timer_list *p;
// 如果定时处理程序指针为空，则退出。
  if(!fn) return;
  cli();
  if(jiffies<=0)  // 若定时值 <=0 ，则立刻调用其它处理程序。且该定时器不加入链表中。
    (fn)();
  else{ // 从定时器数组中，找一个空闲项
    for(p=timer_list;p<timer_list+TIME_REQUESTS;p++)
      if(!p->fn)  break;
    if(p>=timer_list+TIME_REQUESTS) // 若已经用完定时器数组则系统崩溃
      panic("No more time requests free");
    p->fn=fn;
    p->jiffies=jiffies;
    p->next=next_timer;
    next_timer=p;
// 链表项按定时值从小到大排序。在排序时减去排在前面需要的滴答数，这样在处理定时器时只要查看链表头的
// 第一项的定时是否到期即可。[这段程序好像没有考虑周全。如果新插入的定时器值小于原来头一个
// 定时器值时，也应该将其后面的定时值减去新的第 1 个的定时值。]
    while(p->next && p->next->jiffies<p->jiffies){
      p->jiffies-=p->next->jiffies;
      fn=p->fn;
      p->fn=p->next->fn;
      p->next->fn=fn;
      jiffies=p->jiffies;
      p->jiffies=p->next->jiffies;
      p->next->jiffies=jiffies;
      p=p->next;
    }
  }
  sti();
}
// 时钟中断 C 函数处理程序，在 kernel/system_call.s 中的 _timer_interrupt(204 行)被调用
// 参数 cpl 是当前特权级 0 或 3 ，0 表示内核代码在执行。
// 对于一个进程由于执行时间片用完时，则进行任务切换。并执行一个计时更新工作。
void do_timer(long cpl){
  extern int beepcount; // 扬声器发声时间滴答数 (kernel/chr_drv/console.c 22222)
  extern void sysbeepstop(void);  // 关闭扬声器 (kernel/chr_drv/console.c 2222)

  if(beepcount) // 如果发生计数次数到，则关闭发声
    if(!--beepcount)  // 向 0x61 口发送命令，复位位 0 和 1.
      sysbeepstop();  // 位 0 控制 8253 计数器 2 的工作，位 1 控制扬声器。
// 如果当前特权级(cpl)为 0 (最高，表示是内核程序在工作) ，则将内核程序运行时间 stime 递增；
// 如果 cpl>0 ,则表示是一般用户程序在工作，增加 utime.
    if(cpl)
      current->utime++;
    else
      current->stime++;
// 如果有软驱操作定时器存在，则将链表第 1 个定时器的值减 1。如果已等于 0，则调用相应的处理程序，
// 并将该处理程序指针置为空。然后去掉该项定时器。
    if(next_timer){
      next_timer->jiffies--;
      while(next_timer && next_timer->jiffies <=0){
        void (*fn)(void); // 这里插入了一个函数指针定义！！！

        fn=next_timer->fn;
        next_timer->fn=NULL;
        next_timer=next_timer->next;
        (fn)(); // 调用处理函数
      }
    }
// 如果当前软盘控制器 FDC 的数字输出寄存器中电动机启动位有置位的，则执行软盘定时程序(265 行)
    if(current_DOR & 0xf0)
      do_floppy_timer();
    if((--current->counter)>0)  return; // 如果进程运行时间还没完，则退出
    current->counter=0;
    if(!cpl) return;  // 对于内核程序，不依赖 counter 值进行调度
    schedule();
}
// 系统调用功能 - 设置报警定时时间z值(秒)。若已设置过 alarm 值，则返回旧值，否则返回 0
int sys_alarm(long seconds){
  int old=current->alarm;

  if(old)
    old=(old-jiffies)/HZ;
  current->alarm=(seconds>0)?(jiffies+HZ(seconds):0);
  return(0);
}
// 取当前进程号 pid
int sys_getpid(void){
  return current->pid;
}
// 取父进程号 ppid
int sys_getppid(void){
  return current->father;
}
// 取用户号 uid
int sys_getuid(void){
  return current->uid;
}
// 取有效用户号 euid
int sys_geteuid(void){
  return current->euid;
}
// 取组号 gid
int sys_getgid(void){
  return current->gid;
}
// 取有效组号 egid
int sys_getegid(void){
  return current->egid;
}
// 系统调用功能 - 降低对 CPU 的使用优先权。应该限制 increment>0,否则可使优先权增大！！
int sys_nice(long increment){
  if(current->priority-increment>0)
    current->priority-=increment;
  return 0;
}
// 调度程序的初始化子程序
void sched_init(void){
  int i;
  struct desc_struct *p;  // 描述符表结构指针

  if(sizeof(struct sigaction)!=16)  // sigaction 是存放有关信号状态的结构
    panic("Struct sigaction MUST be 16 bytes");
// 设置初始任务(任务 0)的任务状态段描述符和局部数据表描述符(include/asm/system.h,2222)
  set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.ass));
  set_ldt_desc(ldt+FIRST_LDT_ENTRY,&(init_task.task.ldt));
// 清任务数组和描述符表项(注意 i=1 开始，所以出事任务的描述符还在)。
  p=gdt+2+FIRST_TSS_ENTRY;
  for(i=1;i<NR_TASKS;i++){
    task[i]=NULL;
    p->a=p->b=0;
    p++;
    p->a=p->b=0;
    p++;
  }
// Clear NT,so that won't have trouble with that later on
// 清除标志寄存器中的位 NT，这样以后就不会有麻烦
// NT 标志用于控制程序的递归调用(Nested Task).当 NT 置位时，那么当前中断任务执行 iret 指令
// 时就会引起用户切换。NT 指出 TSS 中的 back_link 字段是否有效。
  __asm__("pushfl;andl $0xffffbfff,(%esp);popfl");  // 复位 NT 标志
  ltr(0); // 将任务 0 的 TSS 加载到任务寄存器 tr
  lldt(0);  // 将局部描述符表加载到局部描述符表寄存器
// 注意！！ 是将 GDT 中相应 LDT 描述符表的选择符加载到 ldtr。只明确加载这一次，以后新任务 LDT
// 的加载，是 CPU 根据 TSS 中的 LDT 项自动加载。
// 下面代码用于初始化 8253 定时器
  outb_p(0x36,0x43);  // binary,mode 3,LSB/MSB.ch 0
  outb_p(LATCH & 0xff,0x40);  // LSB  定时值低字节
  outb_p(LATCH>>8,0x40);  // MSB  定时值高字节
  set_intr_gate(0x20,&timer_interrupt); // 设置时钟中断处理句柄(设置时钟中断门)
  outb(inb_p(0x21)&~0x01,0x21); // 修改中断控制器屏蔽码，允许时钟中断
  set_system_gate(0x80,&system_call); // 设置系统调用中断门
}
