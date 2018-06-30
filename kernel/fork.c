/*
 *  'fork.c' contains the help-routines for the 'fork' system call (see also system_call.s),
 * and some misc functions('verify_area').Fork is rather simple,once you get the
 * hang of it.
 * See 'mm/mm.c':'copy_page_tables()'
 */
/* ‘fork.c’ 中含有系统调用 'fork' 的辅助子程序(参见 system_call.s)，以及一些其他函数('verify_area').
 * 一旦你了解了 fork,就会发现它其实是非常简单的，但内存管理却有些难度。参见 'mm/mm.c' 中的
 * 'copy_page_tables()'
 */
#include<errno.h> // 错误号头文件。包含系统中各种出错号。(Linus 从 MIXIX 中引进的)

#include<linux/sched.h> // 调度程序头文件，定义任务结构 task_struct、初始任务 0 的数据
#include<linux/kernel.h>  // 内核头文件。含有一些内核常用函数的原形定义
#include<asm/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数
#include<asm/system.h>  // 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏。

extern void write_verify(unsigned long address);

long last_pid=0;
// 进程空间区域写前验证函数。对当前进程的地址 addr 到 addr+size 这一段进程空间以页为单位执行
// 写操作前的检测工作。若页面是只读的，则执行共享检验和复制页面操作。(写时复制)。
void verify_area(void *addr,int size){
  unsigned long start;

  start=(unsigned long)addr;  // 将起始地址 start 调整为其所在页面的做辩解开始位置，同时
  size+=start&0xfff;  //  相应地调整验证区域大小
  start&=0xfffff000;  // 此时 start 是当前进程空间中的线性地址
  start+=get_base(current->ldt[2]); // 此时 start 变成整个线性空间中的地址位置。
  while(size>0){
    size-=4096;
    write_verify(start);  // 写页面验证。若页面不可写，则复制页面。
    start+=4096;
  }
}
// 设置新任务代码和数据段基址、限长并复制页表。nr 为新任务号；p 是新任务数据结构的指针。
int copy_mem(int nr,struct task_struct *p){
  unsigned long old_data_base,new_data_base,data_limit;
  unsigned long old_code_base,new_code_base,code_limit;

  code_limit=get_limit(0x0f); // 取局部描述符表中代码段描述符项中段限长
  data_limit=get_limit(0x17); // 取局部描述符表中数据段描述符项中段限长
  old_code_base=get_base(current->ldt[1]);  // 取原代码段基址
  old_data_base=get_base(current->ldt[2]);  // 取原数据段基址
  if(old_data_base!= old_code_base) // 0.11 版不支持代码和数据段分立的情况
    panic("We don't support separate I&D");
  if(data_limit<code_limit) // 如果数据段长度 < 代码段长度也不对
    panic("Bad data_limit");
  new_data_base=new_code_base=nr*0x4000000; // 新基址=任务号*64 MB(任务大小)
  p->start_code=new_code_base;
  set_base(p->ldt[1],new_code_base);  // 设置代码段描述符中基址域
  set_base(p->ldt[2],new_data_base);  // 设置数据段描述符中基址域
// 设置新进程的页目录表项和页表项。即把新进程的线性地址内存页对应到实际物理地址内存页面上。
// 由于 Linux 采用了写时复制(Cppy on Write)技术，因此这里仅为新进程设置了自己的页目录表项
// 和页表项，而没有实际为新进程分配物理内存页面。此时新进程与其父进程共享所有物理页面。若出错
// 则释放申请的页表项。
  if(copy_page_tables(old_data_base,new_data_base,data_limit)){
    free_page_tables(new_data_base,data_limit);
    return -ENOMEM;
  }
  return 0;
}
// 复制进程。其中参数 none 是 system_call.s 中调用 sys_call_table 时压入堆栈的返回地址。
/*
 *  Ok,this is the main fork-routine.It copies the system process information (task[nr])
 * and sets up the necessary registers.It also copies the data segment in it's entirety.
 */
/* 下面是主要的 fork 子程序。它复制系统进程信息(task[n])并且设置必要的寄存器。
 * 它还整个地复制数据段。
 */
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
                 long ebx,long ecx,long edx,
                 long fs,long es,long ds,
                 long eip,long cs,long eflags,long esp,long ss){
  struct task_struct *p;
  int i;
  struct file *f;

  p=(struct task_struct*)get_free_page(); // 为新任务数据结构分配内存
  if(!p)  return -EAGAIN; // 如果内存分配出错，则返回出错码并退出
  task[nr]=p; // 将新任务指针放入数组中，其中 nr 为任务号
  *p= *current; // NOTE! this doesn't copy the supervisor stack
                // 注意！这样做不会复制超级用户的堆栈 只复制当前进程内容
  p->state=TASK_UNINTERRUPTIBLE;  // 将新进程的状态先置为不可中断等待状态
  p->pid=last_pid;  // 新进程号。由前面调用 find_empty_process() 得到
  p->father=current->pid; // 设置父进程号
  p->counter=p->priority;
  p->signal=0;  // 信号位图置 0
  p->alarm=0; // 进程的领导权是不能继承的
  p->leader=0;  // process leadership doesn't inherit
  p->utime=p->stime=0; // 初始化用户态时间和核心时间
  p->cutime=p->cstime=0;  // 初始化子进程用户态和核心态时间
  p->start_time=jiffies;  // 当前滴答数时间
  p->tss.back_link=0; // 以下设置任务状态段 TSS 所需的数据(参见列表后说明)
  p->tss.esp0=PAGE_SIZE+(long)p;  // 堆栈指针。由于是给任务结构 p 分配了 1 页新内存，所以此时
                                  // esp0 正好指向该页顶端. ss0:esp0 用于作为程序在内核态
                                  // 执行时的堆栈
  p->tss.ss0=0x10;  // 堆栈段选择符(内核数据段)
  p->tss.eip=eip; // 指令代码指针
  p->tss.eflags=eflags; // 标志寄存器
  p->tss.eax=0; // 这是当 forK() 返回时，新进程会返回 0 的原因所在。
  p->tss.ecx=ecx;
  p->tss.edx=edx;
  p->tss.ebx=ebx;
  p->tss.esp=esp; // 新进程完全复制了父进程的堆栈内容，因此要求 task0 的堆栈比较 "干净"
  p->tss.ebp=ebp;
  p->tss.esi=esi;
  p->tss.edi=edi;
  p->tss.es=es&0xffff;  // 段寄存器仅 16 位有效
  p->tss.cs=cs&0xffff;
  p->tss.ss=ss&0xffff;
  p->tss.ds=ds&0xffff;
  p->tss.fs=fs&0xffff;
  p->tss.gs=gs&0xffff;
  p->tss.ldt=_LDT(nr);  // 该新任务 nr 的局部描述符表选择符(LDT 的描述符在 GDT 中)
  p->tss.trace_bitmap=0x80000000; // (高 16 位有效)
  if(last_task_used_math==current)  // 若当前任务使用了协处理器，就保存其上下文
    __asm__("clts;fnsave %0"::"m"(p->tss.i387));
// 设置新任务的代码和数据段基址、限长并复制页表。如果出错(返回值不是 0)，则复位任务数组中相应项
// 并释放为该新任务分配的内存页
  if(copy_mem(nr,p)){ // 返回不为 0 则表示出错
    task[nr]=NULL;
    free_page((long)p);
    return -EAGAIN;
  }
  for(i=0;i<NR_OPEN;i++)  // 若父进程中有文件是打开的，则将对应文件打开次数增 1
    if(f=p->filp[i])
      f->f_count++;
  if(current->pwd)
    current->pwd->i_count++;
  if(current->root)
    current->root->i_count++;
  if(current->executable)
    current->executable->i_count++;
// 在 GDT 中设置新任务的 TSS 和 LDT 描述符项，参见 include/asm/system.h 2222-2222
// 在任务切换时，任务寄存器哦 tr 由 CPU 自动加载。
  set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
  set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
  p->state=TASK_RUNNING;  // do this last,just in case 最后再将新任务设置成可运行状态，以防万一
  return last_pid;  // 返回新进程号(与任务号是不同的)
}
// 为新进程取得不重复的进程号 last_pid ,并返回在任务数组中的任务号(数组 index)
int find_empty_process(void){
  int i;

repeat:
  if((++last_pid)<0)  last_pid=1;
  for(i=0;i<NR_TASKS;i++)
    if(task[i] && task[i]->pid==last_pid) goto repeat;
  for(i=1;i<NR_TASKS;i++) // 任务 0 排除在外
    if(!task[i])
      return i;
  return -EAGAIN;
}
