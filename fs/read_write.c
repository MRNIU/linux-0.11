#include <sys/stat.h> // 文件状态头文件。含有文件或文件系统状态结构 stat{} 和常量
#include <errno.h>  // 错误号头文件。包含系统中各种出错号.(Linus 从 MINIX 中引进的)
#include <sys/types.h>  // 类型头文件。定义了基本的系统数据类型

#include <linux/kernel.h> // 内核头文件.含有一些内核常用函数的原形定义
#include <linux/sched.h> // 调度程序头文件,定义任务结构 task_struct、初始任务 0 的数据
#include <asm/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。

extern int rw_char(int rw, int dev, char * buf, int count, off_t * pos);  // 字符设备读写函数
extern int read_pipe(struct m_inode * inode, char * buf, int count);  // 读管道操作函数
extern int write_pipe(struct m_inode * inode, char * buf, int count); // 写管道操作函数
extern int block_read(int dev, unsigned long * posm char * buf, int count); // 块设备读操作函数
extern int block_write(int dec, long * pos, char * buf, int count); // 块设备写操作函数
extern int file_read(struct m_inode * inode, struct file * filp, char * buf, int count);  // 读文件操作函数
extern int file_write(struct m_inode * inode, struct file * filp, char * buf, int count); // 写文件操作函数

// 重定位文件读写指针系统调用函数
// 参数 fd 是文件句柄，offset 是新的文件读写指针偏移值，origin 是偏移的起始位置，是 SEEK_SET(0,
// 从文件开始处)、SEEK_CUR(1，从当前读写位置)、SEEK_END(2，从文件尾处)三者之一。
int sys_lseek(unsigned inf fd, off_t offset, int origin){
  struct file * file;
  int tmp;
  // 如果文件句柄值大于程序最多打开文件数 NR_OPEN(20)，或者该句柄的文件结构指针为空，
  // 或者对应文件结构的 i 节点字段为空，或者指定设备文件指针是不可定位的，则返回出错码并退出。
  if(fd>=NR_OPEN||!(file=current->filp[fd])||
          !(file->f_inode)||!IS_SEEKABLE(MAJOR(file->f_inode->i_dev)))
    return -EBADF;
  // 如果文件对应的 i 节点是管道节点，则返回出错码，退出。管道头尾指针不可随意移动
  if(file->f_inode->i_pipe)
    return -ESPIPE;
  switch (origin) { // 根据设置的定位比熬制，分别重新定位文件读写指针
    // origin=SEEK_SET，要求以文件起始处作为远点设置文件读写指针。若偏移值小于零，则出错返回
    // 错误码。否则设置文件读写指针等于 offset
    case 0:
      if(offset<0)  return -EINVAL;
      file->f_pos+=offset;
      break;
    // origin=SEEL_CUR，要求以文件当前读写指针处作为原点重定位读写指针，如果文件当前指针加上
    // 偏移值小于 0，则返回出错码退出。否则在当前读写指针上加上偏移值
    case 1:
      if(file->f_pos+offswr<0)  return -EINVAL;
      file->f_pos+=offset;
      break;
    // origin=SEEK_END，要求以文件末尾作为原点重定位读写指针。此时若文件大小加上偏移值小于 0，
    // 则返回出错码退出。否则重定位读写指针为文件长度加上偏移量。
    case 2:
      if((tmp=file->f_inode->i_size+offset)<0)
        return -EINVAL;
      file->f_pos=tmp;
      break;
    default:  // origin 设置出错，返回出错码退出
      return -EINVAL;
  }
  return file->f_pos; // 返回重定位后的文件读写指针值
}

// 读文系统调用函数。参数 fd 是文件句柄，buf 是缓冲区，count 是欲读字节数
int sys_read(unsigned int fd, char * buf, int count){
  struct file * file;
  struct m_inode * inode;
  // 如果文件句柄值大于程序最多打开文件数 NR_OPEN，或者需要读取的字节计数值小于 0，或者该句柄
  // 的文件结构指针为空，则返回出错码并退出。
  if(fd>NR_OPEN||count<0||!(file=current->filp[fd]))
    return -EINVAL;
  if(!count)  // 若需读取的字节数 count 等于 0，则返回 0，退出
    return 0;
  verify_area(buf, count);  // 验证存放数据的缓冲区内存限制
  // 取文件对应的 i 节点。若是管道文件，并且是读管道文件模式，则进行读管道操作，若成功则返回读取的
  // 字节数，否则返回出错码，退出
  inode->file->f_inode;
  if(inode->i_pipe)
    return (file->f_model &1)? read_pipe(inode, buf, count):-EIO;
  // 如果是字符型文件，则进行读字符设备操作，返回读取的字节数。
  if(S_ISCHR(inode->i_mode))
    return rw_char(READ, inode->i_zone[0], buf, count, &file->f_pos);
  if(S_ISBLK(inode->i_mode))
    return block_read(inode->i_zone[0], &file->f_pos, buf, count);
  // 如果是目录文件或者是常规文件，则首先验证读取数 count 的有效性并进行调查(若读取字节数加上
  // 文件当前读写指针值大于文件大小，则重新设置读取字节数为文件长度-当前读写指针值，若读取数
  // 等于 0，则返回 0 退出)，然后执行文件读操作，返回读取的字节数并退出
  if(S_ISDIR(inode->i_mode)||S_ISREG(inode->i_mode)){
    if(count+file->f_pos>inode->i_size)
      count=inode->i_size-file->f_pos;
    if(count<=0)
      return 0;
    return file_read(inode, file, buf, count);
  } // 否则打印节点文件属性，并返回出错码退出
  printk("(Read)inode->i_mode=%06o\n\r", inode->i_mode);
  return -EINVAL;
}

// 文件系统调用的 C 函数
int sys_write(unsigned int fd, char * buf, int count){
  struct file * file;
  struct m_inode * inode;
  // 如果文件举兵值大于程序最多打开文件数 NR_OPEN，或者需要写入的字节计数小于 0，或者该句柄的
  // 文件结构指针为空，则返回出错码并退出。
  if(fd>=NR_OPEN||count<0||!(file=current->filp[fd]))
    return -EINVAL;
  if(!count)  // 若需读取的字节数 count 等于 0，则返回 0，退出
    return 0;
  // 取文件对应的 i 节点。若是管道文件，并且是写管道文件模式，则进行写管道操作，若成功则返回
  // 写入的字节数，否则返回出错码，退出
  inode=file->f_inode;
  if(inode->i_pipe)
    return (file->f_mode & 2)?write_pipe(inode, buf, count):-EIO;
  // 如果是字符型文件，则进行写字符设备操作，返回写入的字符数，退出
  if(S_ISCHR(inode->i_mode))
    return rw_char(WRITE, inode->i_zone[0], buf, count, &file->f_pos);
  // 如果是块设备文件，则进行块设备写操作，并返回写入的字节数，退出
  if(S_ISBLK(inode->i_mode))
    return block_write(inode->i_zone[0], &file->f_pos, buf, count);
  // 若是常规文件，则执行文件写操作，并返回写入的字节数，退出
  if(S_ISREG(inode->i_mode))
    return file_write(inode, file, buf, count);
  // 否则，显示对应节点的文件模式，返回出错码，退出
  printk("(Write)inode->i_mode=%06o\n\r", inode->i_mode);
  return -EINVAL;
}
