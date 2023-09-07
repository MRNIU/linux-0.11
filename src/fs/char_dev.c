#include <errno.h>  // 错误号头文件。包含系统中各种出错号.(Linus 从 MINIX 中引进的)
#include <sys/types.h>  // 类型头文件。定义了基本的系统数据类型

#include <linux/sched.h> // 调度程序头文件,定义任务结构 task_struct、初始任务 0 的数据
#include <linux/kernel.h> // 内核头文件.含有一些内核常用函数的原形定义

#include <asm/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。
#include <asm/io.h> // io 头文件。定义硬件端口输入/输出宏汇编语句。

extern int tty_read(unsigned minor, char * buf, int count); // 终端读
extern int tty_write(unsigned minor, char * buf, int count);  // 终端写
// 定义字符设备读写函数指针类型
typedef (*crw_ptr)(int rw, unsigned minor, char * buf, int count, off_t * pos);

// 串口终端读写操作函数
// 参数：rw-读写命令；minor-终端子设备号；buf-缓冲区；cout-读写字节数
// pos-读写操作当前指针，对于终端操作，该指针无用。返回：实际读写的字节数
static int rw_ttyx(int rw, unsigned minor, char * buf, int count, off_t * pos){
  return ((rw==READ)?tty_read(minor, buf, count):tty_write(minor, buf, count));
}

// 终端读写操作函数。同上 rw_ttyx()，只是增加了对进程是否有控制终端的检测
static int rw_tty(int rw, unsigned minor, char * buf, int count, off_t * pos){
  if(current->tty<0)  // 若进程没有对应的控制终端，则返回出错号
    return -EPERM;
  // 否则调用终端度写函数 rw_ttyx(), 并返回实际读写字节数
  return rw_ttyx(rw, current->tty, buf, count, pos);
}

// 内存数据读写。未实现
static int rw_ram(int rw, char * buf, int count, off_t * pos){
  return -EIO;
}

// 内存数据读写操作函数。未实现
static int rw_mem(int rw, char * buf, int count, off_t * pos){
  return -EIO;
}

// 内核数据区读写函数。未实现
static int rw_kmem(int rw, char * buf, int count, off_t * pos){
  return -EIO;
}
// 端口读写操作函数
// 参数：rw-读写命令；buf-缓冲区；cout-读写字节数；pos-端口地址
// 返回：实际读写的字节数
static int rw_port(int rw, char * buf, int count, off_t * pos){
  int i = *pos;
  // 对于所要求读写的字节数，并且端口地址小于 64k 是，循环执行单个字节的读写操作
  while(count-->0 && i<65536){
    if(rw==READ)  // 若是读命令，则从端口 i 读取一字节并放到用户缓冲区中
      put_fs_byte(inb(i), buf++);
    else  // 若是写命令，则从用户数据缓冲区中取一字节输出到端口 i
      outb(get_fs_byte(buf++), i);
    i++;  // 前移一个端口[??]
  }
  i-=*pos;  // 计算读/写的字节数，并相应调整读写指针。
  *pos+=i;
  return i; // 返回读/写的字节数。
}

// 内存读写操作函数
static int rw_memory(int rw, unsigned minor, char * buf, int count, off_t * pos){
  switch (minor) {  // 根据内存设备子设备号，分别调用不同的内存读写函数
    case 0: return rw_ram(rw, buf, count, pos);
    case 1: return rw_mem(rw, buf, count, pos);
    case 2: return rw_kmem(rw, buf, count, pos);
    case 3: return (rw==READ)?0:count;  // rw_null
    case 4: return rw_port(rw, buf, count, pos);
    default:  return -EIO;
  }
}

// 定义系统中设备种数
#define NRDEVS ((sizeof(crw_table))/(sizeof(crw_ptr)))
// 字符设备读写函数指针表
static crw_ptr crw_table[]={
  NULL, // nodev 无设备(空设备)
  rw_memory,  // /dev/mem etc
  NULL, // /dev/fd
  NULL, // /dev/hd
  rw_ttyx,  // /dev/ttyx
  rw_tty, // /dev/tty
  NULL, // /dev/lp
  NULL  // unnames pipes
};

// 字符设备读写操作函数。
// 参数：rw-读写命令；dev-设备号；buf-缓冲区；count-读写字节数；pos-读写指针。
// 返回：实际读/写字节数
int rw_char(int rw, int dev, char * buf, int count, off_t * pos){
  crw_ptr call_addr;
  if(MAJOR(dev)>=NRDEVS)  // 如果设备号超出系统设备数，则返回出错码
    return -ENODEV;
  // 若该设备没有对应的读/写函数，则返回出错码
  if(!(call_addr=crw_table[MAJOR(dev)]))
    return -ENODEV;
  // 调用对应设备的读写操作函数，并返回实际读/写的字节数。
  return call_addr(rw, MAJOR(dev), buf, count, pos);
}
