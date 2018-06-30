#include<errno.h> // 错误号头文件。包含系统中各种出错号(Linus 从 MINIX 中引进)
#include<signal.h>  // 信号头文件。定义信号符号常量，信号结构以及信号操作函数原型。
#include<sys/wait.h>  // 等待调用头文件。定义系统调用 wait() 和 waitpid() 及相关常数符号

#include<linux/sched.h> // 调度程序头文件，定义任务结构 task_struct、初始任务 0 的数据
#include<linux/kernel.h>  // 内核头文件。含有一些内核常用函数的原形定义。
#include<linux/tty.h> // tty 头文件，定义了有关 tty_io,串行通信方面的参数、常数。
#include<asm/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。

int sys_pause(void);
int sys_close(int fd);
// 释放指定进程(任务)。释放任务槽及任务数据结构所占用的内存。
void release(struct task_struct *p){
  int i;

  if(!p)  return;
  for(i=1;i<NR_TASKS;i++) // 扫描任务数组，寻找指定任务
    if(task[i]==p){
      task[i]=NULL; // 置空该任务项并释放相关内存页
      free_page((long)p);
      schedule(); // 重新调度
      return;
    }
  panic("trying to release non-existent task.");  // 指定任务不存在则死机。
}
// 向指定任务 (*p) 发送信号 (sig) ，权限为 priv
static inline int send_sig(long sig,struct task_struct *p,int priv){
  if(!p||sig<1||sig>32) return -EINVAL; // 若信号不正确或任务指针为空则出错退出。
// 若有权或进程有效用户标示符(euid)就是制动进程的 euid 过这事超级用户，则在进程位图中添加该信号，
// 否则出错退出。其中 suser() 定义为(current->euid==0),用于判断用户是否为超级用户
  if(priv||(current->euid==p->euid)||suser())
    p->signal|=(1<<(sig-1));
  else
    return -EPERM;
  return 0;
}
// 终止会话(session)
static void kill_session(void){
  struct task_struct * *p=NR_TASKS+task;  // 指针 *p 首先指向任务数组最末端。
// 对于所有的任务(除任务 0 意外)，如果其会话等于当前进程的会话就向它发送挂断进程信号。
  while(--p>&FIRST_TASK){
    if(*p&&(*p)->session==current->session)
      (*p)->signal|=1<<(SIGHUP-1);  // 发送挂断进程信号。
  }
}

/*
 * XXX need to check permissions needed to send signals to process groups,etc. etc.
 *  kill() permissions semantics are tricky!
 */
/* 为了向进程组等发送信号，XXX需要检查许可。kill() 的许可机制非常巧妙！
 */
// kill() 系统调用可用于向任何进程或进程俎发送任何信号。
// 若 pid 值>0,则信号被发送给 pid；若 pid=0，那么信号就会被发送给当前进程俎中的所有进程；若
// pid=-1 ，则信号 sig 就会发送给除第一个进程外的所有进程；若 pid<-1 ，则信号 sig 将发送给
// 进程组 -pid 的所有进程；若信号 sig 为 0，则不发送信号，但仍会进行错误检查。若成功则返回 0.
int sys_kill(int pid,int sig){
  struct task_struct **p=NR_TASKS+task;
  int err,retval=0;

  if(!pid)
    while(--p>&FIRST_TASK){
      if(*p&&(*p)->pgrp==current->pid)
        if(err=send_sig(sig,*p,1))  // 强制发送信号
          retval=err;
  }
  else if(pid>0)
    while(--p> &FIRST_TASK){
      if(*p && (*p)->pid==pid)
        if(err=send_sig(sig,*p,0))
          retval=err;
    }
  else if(pid==-1)
    while(--p>&FIRST_TASK)
      if(err=send_sig(sig,*p,0))
        retval=err;
  else
    while(--p>&FIRST_TASK)
      if(*p&&(*p)->pgrp==-pid)
        if(err=send_sig(sig,*p,0))
          retval=err;
  return retval;
}
// 通知父进程 - 向父进程发送信号 SIGCHLD: 子进程将停止或终止。若未找到父进程，则自己释放.
static void tell_father(int pid){
  int i;

  if(pid)
    for(i=0;i<NR_TASKS;i++){
      if(!task[i])  continue;
      if(task[i]->pid!=pid) continue;
      task[i]->signal|=(1<<(SIGCHLD-1));
      return;
    }
// if we don't find any fathers,we just release ourselver
// This is not really OK.Must change it to make father 1
// 如果没有找到任何父进程，就需要自己释放。这其实并不妥帖，应该使其父进程为 1
  printk("BAD BAD - no father found\n\r");
  release(current); // 如果没有找到父进程，则自己释放。
}
// 程序退出处理程序。在系统调用的中断处理程序中被调用。
int do_exit(long code){  // code 是错误代码。
  int i;
// 释放当前进程代码段和数据段所占的内存页(free_page_tables() 在 mm/memory.c 2222 行)
  free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
  free_page_tables(get_vase(current->ldt[2]),get_limit(0x17));
// 如果当前进程有子进程，就将子进程的 father 置为 1(其父进程改为进程 1).如果该子进程已经处于僵死
// (ZOMBIE)状态，则向进程 1 发送子进程终止信号 SIGCHLD.
  for(i=0;i<NR_TASKS;i++)
    if(task[i]&&task[i]->father==current->pid){
      task[i]->father=1;
      if(task[i]->state==TASK_ZOMBIE)
        // assumption task[1] is always init
        (void)send_sig(SIGCHLD,task[1],1);
    }
  for(i=0;i<NR_OPEN;i++)  // 关闭当前进程打开着的所有文件
    if(current->filp[i])
      sys_close(i);
  iput(current->pwd); // 这段对当前进程工作目录 pwd、根目录 root 以及运行程序的 i 节点进行同步
  current->pwd=NULL;  // 操作，并分别置空。
  iput(current->root);
  current->root=NULL;
  iput(current->executable);
  current->executable=NULL;
  if(current->leader&&current->tty>=0)  // 如果当前进程是领头(leader)进程，并且有其控制的
    tty_table[current->tty].pgrp=0; // 终端，则释放该终端。
  if(last_task_used_math=current) // 如果当前进程上次使用过协处理器，则将 last_task_used_math
    last_task_used_math=NULL; // 置空
  if(current->leader) // 如果当前进程是 leader 进程，则终止所有相关进程。
    kill_session();
  current->state=TASK_ZOMBIE; // 把当前进程置为僵死状态，并设置退出代码。
  current->exit_code=code;
  tell_father(current->father); // 即向父进程发信号 SIGCHLD -子进程将停止或终止
  schedule(); // 重新调度进程的运行
  return(-1); // just to suppress warnings
}
// 系统调用 exit()。终止进程。
int sys_exit(int error_code){
  return do_exit((error_code&0xff)<<8);
}
// 系统调用 waitpid().挂起当前进程，直到 pid 指定的子进程退出(终止)或者收到要求终止该进程的信号，
// 或者是需要调用一个信号句柄(信号处理程序)。如果 pid 所指的子进程早已退出(已成所谓的僵死进程)，
// 则本调用将立即返回。子进程使用的所有资源将释放。
// 若 pid>0 ,表示等待进程号等于 pid 的子进程，若 pid=0，表示等待进程组信号等于当前进程的任何子进程；
// 若 pid<-1 ，表示等待进程组号等于 pid 绝对值的任何子进程；若 pid=-1 ,表示等待任何子进程；
// 若 options=WUNTRACED, 表示如果子进程是停止的，也马上返回；若 options=WNOHANG,表示如果
// 没有子进程退出或终止就马上返回；若 stat_addr 不为空，则就将状态信息保存到那里。
int sys_waitpid(pid_t pid,unsigned long * stat_addr,int options){
  int flag,code;
  struct task_struct ** p;

  verify_area(stat_addr,4);
repeat:
  flag=0;
  for(p=&LAST_TASK;p>&FIRST_TASK;--p){  // 从任务数组末端开始扫描所有任务
    if(!*p||*p==current)  continue; // 跳过空项和本项进程。
    if((*p)->father!=current->pid)  continue; // 如果不是当前进程的子进程则跳过。
    if(pid>0){  // 如果指定的 pid>0,但扫描的进程 pid 与之不等，则跳过
      if((*p)->pid!=pid)  continue;
    }
    else if(!pid){  // 如果指定的 pid=0 ，但扫描的进程组号与当前进程的组号不等，则跳过
      if((*p)->pgrp!=current->pgrp) continue;
    }
    else if(pid!=-1){ // 如果指定的 pid<-1 ,但扫描的进程组号与其绝对值不等，则跳过
      if((*p)->pgrp!=-pid)  continue;
    }
    switch ((*p)->state){
      case TASK_STOPPED:
        if(!(options&WUNTRACED))  continue;
        put_fs_long(0x7f,stat_addr);  // 置状态信息为 0x7f
        return (*p)->pid; // 退出，返回子进程的进程号
      case TASK_ZOMBIE:
        current->cutime+=(*p)->utime; // 更新当前进程子进程用户态和核心态运行时间。
        current->cstime+=(*p)->stime;
        flag=(*p)->pid;
        code=(*p)->exit_code; // 取子进程的退出码
        release(*p);  // 释放该子进程
        put_fs_long(code,stat_addr);  // 置状态信息为退出码值
        return flag;  // 退出，返回子进程的 pid
      default:
        flag=1; // 如果子进程不在停止或僵死状态，则 flag=1
        continue;
    }
  }
  if(flag){ // 如果子进程没有处于退出或僵死状态，并且 options=WNOHANG,则立刻返回
    if(options&WNOHANG) return 0;
    current->state=TASK_INTERRUPTIBLE;  // 置当前进程为可中断等待状态
    schedule(); // 重新调度
    if(!(current->signal&=~(1<<(SIGCHLD-1)))) // 又开始执行本进程时，如果进程没有收到
      goto repeat;  // 除 SIGCHLD 的信号，则还是重复处理
    else
      return -EINTR;  // 退出，返回出错码。
  }
  return -ECHILD;
}
