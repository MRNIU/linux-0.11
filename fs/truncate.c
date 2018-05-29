#include <linux/sched.h> // 调度程序头文件,定义任务结构 task_struct、初始任务 0 的数据

#include <sys/stat.h> // 文件状态有文件。文件或文件系统状态结构 stat{} 和常量

// 释放一次间接块
static void free_ind(int dev, int block){
  struct buffer_head * bh;
  unsigned short * p;
  int i;
  if(!block)  // 如果逻辑块号为 0
    return;
  // 读取一次简介块，并释放其商标名使用的所有逻辑块，然后释放该一次间接块的缓冲区
  if(bh=bread(dev, block)){
    p=(unsigned short *) bh->b_data;  // 指向数据缓冲区
    for(i=0;i<512;i++, p++) // 每个逻辑块上可有 512 个块号
      if(*p)
        free_block(dev, *p);  // 释放指定的逻辑块
    brelse(bh); // 释放缓冲区
  }
  free_block(dev, block); // 释放设备上的一次间接块
}

// 释放二次间接块
static void free_dind(int dev, int block){
  struct buffer_head * bh;
  unsigned short *p;
  int i;
  // 如果逻辑块号为 0，则返回
  if(!block)
    return;
  // 读取二次间接块的一级块，并释放其上表明使用的所有逻辑块，然后释放该一级块的缓冲区
  if(bh=bread(dev, block)){
    p=(unsigned short *)bh->b_data;
    for(i=0;i<512;i++, p++)
      if(*p)
        free_ind(dev, *p);
    brelse(bh);
  }
  free_block(dev, block);
}

// 将节点对应的文件长度截为 0，并释放占用的设备空间
void truncate(struct m_inode * inode){
  int i;
  // 如果不是常规文件或者是目录文件，则返回
  if(!(S_ISREG(inode->i_mode)||S_ISDIR(inode->i_mode)))
    return;
  // 释放 i 节点的 7 个直接逻辑块，并将这 7 个逻辑块项全置零
  for(i=0;i<7;i++)
    if(inode->i_zone[i]){ // 如果块号不为 0，则释放之。
      free_block(inode->i_dev, inode->i_zone[i]);
      inode->i_zone[i]=0;
    }
    free_ind(inode->i_dev, inode->i_zone[7]); // 释放一次间接块
    free_dind(inode->i_dev, inode->i_zone[8]);  // 释放二次间接块
    inode->i_zone[7]=inode->i_zone[8]=0;  // 逻辑块顶 7、8 置零
    inode->i_size=0;  // 文件大小置零
    inode->i_dirt=1;  // 置节点已修改标志
    inode->i_mtime=inode->i_ctime=CURRENT_TIME; // 重置文件和节点修改时间为当前时间
}
