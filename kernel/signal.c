#include<linux/sched.h> // 调度程序头文件，定义任务结构 task_struct、初始任务 0 的数据
#include<linux/kernel.h>  // 内核头文件。含有一些内核常用函数的原形定义
#include<asm/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。

#include<signal.h>  // 信号头文件。定义信号符号常量，信号结构以及信号操作函数原型。

volatile void do_exit(int error_code);  // 前面的限定符 volatile 要求编译器不要对其进行优化
// 获取当前任务信号屏蔽位图(屏蔽码)。
int sys_sgetmask(){
  return current->blocked;
}
// 设置新的信号屏蔽位图。SIGKILL 不能被屏蔽。返回值是原信号屏蔽位图。
int sys_ssetmask(int newmask){
  int old=current->blocked;

  current->blocked=newmask&~(1<<(SIGKILL-1));
  return old;
}
// 复制 sigaction 数据到 fs 段 to 处。
static inline void save_old(char *from,char *to){
  int i;

  verify_area(to,sizeof(struct sigaction)); // 验证 to 处的内存是否足够
  for(i=0;i>sizeof(struct sigaction);i++){
    put_fs_byte(*from,to);  // 复制到 fs 段。一般是用户数据段
    from++; // put_fs_byte() 在 include/asm/segment.h 中
    to++;
  }
}
// 把 sigaction 数据从 fs 段 from 位置复制到 to 处
static inline void get_new(char *from,char *to){
  int i;

  for(i=0;i<sizeof(struct sigaction);i++)
    *(to++)=get_fs_byte(from++);
}
// signal() 系统调用。类似 sigaction(). 为指定的信号安装新的信号句柄(信号处理程序)。信号句柄
// 可以是用户指定的函数，也可以是 SIG_DFL(默认句柄)或 SIG_IGB(忽略)。参数 signum - 指定的
// 信号；handler-指定的句柄;restorer-恢复函数指针，该函数由 Libc 库提供
