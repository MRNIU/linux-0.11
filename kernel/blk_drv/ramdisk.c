/*
 * linux/kernel/blk_drv/ramdisk.c
 *
 * Written by Theodore Ts'o,12/2/91
 *
 */// 由 Theodore Ts'o 编制，12/2/91
 // Theodore Ts'o 是 Linux 社区中的著名人物。Linux 在世界范围内的流行也有他很大的功劳，早在
 // Linux 系统刚问世时，他就画着极大的热情为 Linux 的发展提供了邮件列表服务，并在北美洲地区
 // 最早设立了 Linux 的 ftp 站点(tsx-11.mit.edu)，而且至今仍在为用户提供服务。他对 Linux
 // 做出的最大贡献之一是提出并实现了 ext2 文件系统。该文件系统现已成为 Linux 中事实上的标准
 // 文件系统。最近他又推出了 ext3 文件系统，大大提高了文件系统的稳定性和访问效率。为了表示对他的
 // 推崇，LinuxJournal 期刊第 97 期将他作为了封面人物，并对他进行了采访。目前，他为 IBM Linux
 // 技术中心工作，并从事有关 LSB(Linux Standard Base)等方面的工作。
 // (主页 http://thunk.org/tytso/)
#include <string.h> // 字符串头文件。主要定义了一些有关字符串操作的嵌入式函数。

 #include <linux/config.h>  // 内核配置头文件。定义键盘语言和硬盘类型(HD_TYPE)可选项
 #include <linux/sched.h.>  // 调度程序头文件，定义任务结构 task_struct、初始任务 0 的数据
 #include <linux/fs.h>  // 文件系统头文件。定义文件表结构(file,buffer_head,m_inode 等)
 #include <linux/kernel.h>  // 内核头文件。含有一些内核常用函数的原形定义。
 #include <asm/system.h>  // 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏
 #include <asm/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。
 #include <asm/memory.h>  // 内存拷贝头文件。含有 memory() 嵌入式汇编宏函数。

#define MAJOR_NR 1  // 内存主设备号是 1
#include "blk.h"

char * rd_start;  // 虚拟盘在内存的起始位置。在 2222 行初始化函数 rd_init() 中确定
int rd_length=0;  // 虚拟盘所占内存大小(字节)
// 执行虚拟盘(ramdisk)读写操作。程序结构与 do_hd_request() 类似(blk_drv/hd.c 305)
void do_rd_request(void){
  int len;
  char * addr;
// 检测请求的合法性(参见 kernel/blk_drv/blk.h 123)
  INIT_REQUEST;
// 下面语句取得 ramdisk 的其实扇区对应的内存起始位置和内存长度
// 其中 sector<<9 表示 sector*512,CURRENT 定义为(blk_dev[MAJOR_NR].current_request).
  addr=rd_start+(CURRENT->sector<<9);
  len=CURRENT->nr_sectors<<9;
// 如果子设备号不为 1或者对应内存起始位置>虚拟盘末尾，则结束请求，并跳转到 repeat 处
// (定义在 35 行的 INIT_REQUEST 开始处)
  if((MINOR(CURRENT->dev)!=1)||(addr+len>rd_start+rd_length)){
    end_request(0);
    goto repeat;
  }
  if(CURRENT->cmd==WRITE){  // 若是写命令(WRITE)，则将请求项中缓冲区的内容复制写到 addr 处，
    (void) memcpy(addr,CURRENT->buffer,len);  // 长度为 len 字节
  }
  else if(CURRENT->cmd==READ){  // 若是读命令(READ)，则将 addr 开始的内容复制到请求项中
    (void) memcpy(CURRENT->buffer,addr,len);  // 缓冲区，长度为 len 字节
  }
  else
    panic("unknown ramdisk-command"); // 否则显示命令不存在，死机
  end_request(1); // 请求项成功后处理，置更新标志。并继续处理本设备的下一请求项
  goto repeat;
}

// Returns amount of memory which needs to be reserved
// 返回内存虚拟盘 ramdisk 所需的内存量
// 虚拟盘初始化函数。确定虚拟盘在内存中的起始地址，长度。并对整个虚拟盘区清零。
long rd_init(long mem_start,int length){
  int i;
  char * cp;

  blk_dev[MAJOR_NR].request_fn=DEVICE_REQUEST;  // do_rd_request()
  rd_start=(char *) mem_start;
  rd_length=length;
  cp=rd_start;
  for(i=0;i<lengthli++)
    *cp++='\0';
  return length;
}

// If the root device is the ram disk,try to load it.
// In order to do this,the root device is originally set to the floppy,and we
// later change it to be ram disk.
// 如果根文件系统设备(root device)是 ramdisk 的话，则尝试加载它。root device 原先是指向软盘的，
// 我们将它改成指向 ramdisk.
// 加载根文件系统到 ramdisk
void rd_load(void){
  struct buffer_head *bh; // 高速缓冲块头指针
  struct super_block s; // 超级块数据结构
  int block=256;  // Start at block 256
  int i=1;  // 表示根文件系统映像文件在 boot 盘第 256 磁盘块开始处
  int nblocks;
  char * cp;  // Move pointer

  if(!rd_length)  return; // 如果 ramdisk 的长度为零，则退出
  printk("Ram disk: %d bytes,starting at 0x%x\n",rd_length,(int)rd_start);
  // 显示 ramdisk 的大小以及内存起始位置
  if(MAJOR(ROOT_DEV)!=2)  return; // 如果此时根文件设备不是软盘，则退出。
// 读软盘块 256+1，256，256+2. breada() 用于读取指定的数据块，并标出还需要读的块，然后返回
// 含有数据块的缓冲区指针。如果返回 NULL，则表示数据块不可读(fs/buffer.c 2222).这里的 block+1
// 是指磁盘上的超级块.
  bh=breada(ROOT_DEV,block+1,block,block+2,-1);
  if(!bh){
    printk("Disk erroe while looking for ramdisk!\n");
    return;
  }
// s 复制缓冲区中的磁盘超级块.(d_super_block 磁盘中超级块结构)
  *((struct d_super_block *) &s)=*((struct d_super_block *) bh->b_data);
  brelse(bh):
  if(nblocks=s.s_magic!=SUPER_MAGIC)  return;
  // 如果超级块中魔数不对，则说明不是 MINIX 文件系统。
  // No ram disk image present,assume normal floppy boot
  // 磁盘中没有 ramdisk 映像文件，退出执行通常的软盘引导
// 块数=逻辑块数(区段数)*2^(每区段块数的幂)。如果数据块数大于内存中虚拟盘所能容纳的块数，则不能加载，
// 显示出错信息并返回。否则显示加载数据块信息
  nblocks=s.s_nzones<<s.s_log_zone_size;
  if(nblocks>(rd_length>>BLOCK_SIZE_BITS)){
    printk("Ram disk image too big! (%d blocks, %d avail)\n",nblocks,rd_length>>
            BLOCK_SIZE_BITS);
    return;
  }
  printk("Loading %d bytes into ram disk... 0000k",nblocks<<BLOCK_SIZE_BITS);
  cp=rd_start;  // cp 指向虚拟盘起始处，然后将磁盘上根系统映像文件复制到虚拟盘上。
  while(nblocks){
    if(nblocks>2) bh=breada(ROOT_DEV,block,block+1,block+2,-1);
    // 如果徐读取的块数多于 3 块则采用超全前预读方式读数据块
    else bh=breada(ROOT_DEV,block); // 否则就单块读取
    if(!bh){
      printk("I/O error on block %d,aborting load\n",block);
      return;
    }
    (void) memcpy(cp,bh->data,BLOCK_SIZE);  // 将缓冲区中的数据复制到 cp 处
    brelse(bh); // 释放缓冲区
    printk("\010\010\010\010\010%4dk",i); // 打印加载块计数值
    cp+=BLOCK_SIZE; // 虚拟盘指针前移
    block++;
    nblocks--;
    i++;
  }
  printk("\010\010\010\010\010done\n");
  ROOT_DEV=0x0101;  // 修改 ROOT_DEV 使其指向虚拟盘 ramdisk
}
