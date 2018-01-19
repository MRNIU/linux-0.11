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
  
}
