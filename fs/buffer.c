/*
 *  'buffer.c' implements the buffer-cache functions.Race-conditions have been
 * avoided by NEVER letting a interrupt change a buffer(except for the data,of course),
 * but instead letting the caller do it.NOTE! As interrupts can wake up a caller,
 * some cli-sti sequences are needed to check for sleep-on-calls.These should
 * be extremely quick,though(I hope)
 */
/* 'buffer.c'用于实现缓冲区高速缓冲功能。通过不让中断过程改变缓冲区，而是让调用者来执行，避免了
 * 竞争条件(当然除改变数据以外)。注意！由于中断可以换行一个调用者，因此姐需要开关中断指令(cli-sti)
 * 序列来检测等待调用返回。但需要非常快(希望是这样)
 */
/*
 * NOTE! There is one discordant note here:checking floppies for disk change.
 * This is where it fits best,I think,as it should invalidate changed floppy-disk-caches.
 */
/* 注意！有一个程序应不属于这里：检测软盘是否更换。但我想这里是放置该程序最好的地方了，因为它需要
 * 使已更换软盘缓冲失败
 */
#include <sedarg.h> // 标准参数头文件。以宏的形式定义变量参数列表

#include <linux/config.h> // 内核配置头文件。定义键盘语言和硬盘类型(HD_TYPE)可选项
#include <linux/sched.h>  // 调度程序头文件，定义任务结构 task_struct、初始任务 0 的数据
#include <linux/kernel.h> // 内核头文件。含有一些内核常用函数的原形定义
#include <asm/system.h> // 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏
#include <asm/io.h> // io 头文件。定义硬件端口输入/输出宏汇编语句

extern int end; // 又链接程序 ls 生成的表明程序末端的变量
struct buffer_head * start_buffer=(struct buffer_head)&end;
struct buffer_head * hash_table[NR_HASH]; // NR_HASH=307 项
static struct buffer_head * free_list;
static struct task_struct * buffer_wait=NULL;
int NR_BUFFERS=0;

// 等待指定缓冲区解锁
static inline void wait_on_buffer(struct buffer_head * bh){
  cli();  // 关中断
  while(bh->b_lock) // 如果已被上锁，则进程进入睡眠，等待其解锁
    sleep_on(&bh->b_wait);
  sti();  // 开中断
}

// 系统调用。同步设备和内存高速缓冲中数据
int sys_sync(void){
  int i;
  struct buffer_head * bh;

  sync_inodes();  // write out inodes into buffers 将 i 节点写入告诉缓冲
// 是从喵所有高速缓冲区，对于已被修改的缓冲块产生写盘请求，将缓冲中数据与设备中同步。
  bh=start_buffer;
  for(i=0;i<NR_BUFFERS;i++,bh++){
    wait_on_buffer(bh); // 等待缓冲区解锁(如果已上锁的话)
    if(bh->b_dirt)
      ll_rw_block(WRITE,bh);  // 产生写设备块请求
  }
  return 0;
}

// 对指定设备进行告诉缓冲数据与设备上数据的同步操作
