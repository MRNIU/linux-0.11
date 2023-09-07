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
#include <stdarg.h> // 标准参数头文件。以宏的形式定义变量参数列表

#include <linux/config.h> // 内核配置头文件。定义键盘语言和硬盘类型(HD_TYPE)可选项
#include <linux/sched.h>  // 调度程序头文件，定义任务结构 task_struct、初始任务 0 的数据
#include <linux/kernel.h> // 内核头文件。含有一些内核常用函数的原形定义
#include <asm/system.h> // 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏
#include <asm/io.h> // io 头文件。定义硬件端口输入/输出宏汇编语句

extern int end; // 又链接程序 ls 生成的表明程序末端的变量
struct buffer_head * start_buffer=(struct buffer_head*)&end;
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
int sync_dev(int dev){
  int i;
  struct buffer_head * bh;

  bh=start_buffer;
  for(i=0;i<NR_BUFFERS;i++,bh++){
    if(bh->b_dev!=dev)  continue;
    wait_on_buffer(bh);
    if(bh->b_dev==dev&&bh->b_dirt)  ll_rw_block(WRITE,bh);
  }
  sync_inodes();
  bh=start_buffer;
  for(i=0;i<NR_BUFFERS;i++,bh++){
    if(bh->b_dev!=dev)  continue;
    wait_on_buffer(bh);
    if(bh->b_dev==dev&&bh->b_dirt)  ll_rw_block(WRITE,bh);
  }
  return 0;
}

// 使指定设备在高速缓冲区中的数据无效
// 扫描高速缓冲区中的所有缓冲块,对于指定设备的缓冲区,复位其有效(更新)标志和已修改标志.
void inline invalidate_buffers(int dev){
  int i;
  struct buffer_head * bh;
  bh=start_buffer;
  for(i=0;i<NR_BUFFERS;i++,bh++){
    if(bh->b_dev!=dev)  continue; // 如果不是指定设备的缓冲区块,则继续扫描下一块
    wait_on_buffer(bh); // 等待该缓冲区解锁(如果已被上锁)
// 由于进程执行过睡眠等待,所以需要再判断一下缓冲区是不是指定设备的
    if(bh->b_dev==dev)  bh->b_uptodate=bh->b_dirt=0;
  }
}

/*
 * This routine checks whether a floppy has been changed ,and invalidates all
 * buffer-cache-entries in that case.This is a relatively slow routine, so we
 * have to try to minimize using it.Thus it is called only upon a 'mount' or
 * 'open'.This is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle os an operation deserve to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is that any additional
 * removable block-device will use this routine,and that mount/open needn't know
 * that floppies/whatever are special.
 */
/* 该子程序检查一个软盘是否已经被更换,如果已经更换就使高速缓冲区中与该软驱对应的所有缓冲区无效.
 * 该子程序相对来说比较慢,我们要尽量少使用它.所以尽在执行 'mount' 或 'open' 时才调用它.
 * 我想这是将速度和实用性相结合的最好方法.若在操作过程当中更换软盘,会导致数据的丢失,这是咎由自取.
 * 注意!尽管目前该子程序仅用于软盘,以后任何可移动介质的块设备都将使用该程序, mount/open 操作
 * 不需要知道是否为软盘或其他什么特殊介质
 */
void check_disk_change(int dev){  // 检查磁盘是否更换,若已经更换就让对应高速缓冲区无效.
  int i;
  if(MAJOR(dev)!=2) return; // 是软盘设备吗?如果不是则退出
  if(!floppy_change(dev&0x03))  return; // 测试对应软盘是否已更换,如果没有则退出
// 软盘已经更换,所以释放对应设备的 i 节点位图和逻辑块位图所占的高速缓冲区;并使该设备的 i 节点
// 和数据信息所占的高速缓冲区无效.
  for(i=0;i<NR_SUPER;i++)
    if(super_block[i].s_dev==dev)
      put_super(super_block[i].s_dev);
  invalidate_inodes(dev);
  invalidate_buffers(dev);
}

// hash 函数和 hash 表项的计算宏定义
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
#define hash(dev,block) hash_table[_hashfn(dev,block)]
// 从 hash 队列和空闲缓冲队列中移走指定的缓冲块
static inline void remove_from_queues(struct buffer_head * bh){
// remove from hash-queue  从 hash 队列中移除缓冲块
  if(bh->b_next)  bh->b_next->b_prev=bh->b_prev;
  if(bh->b_prev)  bh->b_prev->b_next=bh->b_next;
// 如果该缓冲区是该队列的头一个块,则让 hash 表的对应项指向本队列中的下一个缓冲区.
  if(hash(bh->b_dev,bh->b_blocknr)==bh)
    hash(bh->b_dev,bh->b_blocknr)=bh->b_next;
// remove from free list 从空闲缓冲区列表中移除缓冲块
  if(!(bh->b_prev_free)||!(bh->b_next_free))
    panic("Free block list corrupted");
  bh->b_prev_free->b_next_free=bh->b_next_free;
  bh->b_next_free->b_prev_free=bh->b_prev_free;
  if(free_list==bh) free_list=bh->b_next_free;
}

// 将制定缓冲区插入空闲链表尾并放入 hash 队列中.
static inline void insert_into_queues(struct buffer_head * bh){
// put at end of free list  放在空闲链表末尾处
  bh->b_next_free=free_list;
  bh->b_prev_free=free_list->b_prev_free;
  free_list->b_prev_free->b_next_free=bh;
  free_list->b_prev_free=bh;
// put the buffer in new hash-queue if it has a device
// 如果该缓冲块对应一个设备,则将其插入新 hash 队列中
  bh->b_prev=NULL;
  bh->b_next=NULL;
  if(!bh->b_dev)  return;
  bh->b_next=hash(bh->b_dev,bh->b_blocknr);
  hash(bh->b_dev,bh->b_blocknr)=bh;
  bh->b_next->b_prev=bh;
}

// 在高速缓冲中寻找给定设备和指定块的缓冲区块.若找到则返回h缓冲块指针,否则返回 NULL.
static struct buffer_head * find_buffer(int dev,int block){
  struct buffer_head * tmp;
  for(tmp=hash(dev,block);tmp!=NULL;tmp=tmp->b_next)
    if(tmp->b_dev==dev&&tmp->b_blocknr==block)
      return tmp;
  return NULL;
}

/*
 * Why like this,I hear you say...The reason is race-conditions.As we don't lock
 * buffers(unless we are readint them,that is),something might happen to it while
 * we sleep(ie a read-error will force it bad).This shouldn't really happen currently,
 * but the code is ready.
 */
/* 代码为什么会是这样子的?我听见你问...原因是竞争条件.哟葡语我们没有对缓冲区上锁(除非我们正在读取
 * 它们中的数据),那么当我们(进程)睡眠时缓冲区可能会发生一些问题(例如一个读错误将导致该缓冲区出错).
 * 目前这种情况实际上是不会发生的,但处理的代码已经准备好了.
 */
struct buffer_head * get_hash_table(int dev,int block){
  struct buffer_head * bh;
  for(;;){
// 在高速缓冲区中寻找给定设备和指定块的缓冲区,如果没有找到则返回 NULL,退出
    if(!(bh=find_buffer(dev,block)))  return NULL;
// 对该缓冲区增加引用计数,并等待高缓冲区解锁(如果已被上锁)
    bh->b_count++;
    wait_on_buffer(bh);
// 由于经过了睡眠状态,因此有必要早验证该缓冲区块的正确性,并返回缓冲区头指针
    if(bh->b_dev==dev&&bh->b_blocknr==block)  return bh;
    bh->b_count--;
  }
}

// Ok,this is getblk, and isn't very clear,again to hinder race-conditions.
// Most os the code is seldom used,(ie repeating),so it should be much more efficient
// than it looks.
// The algoritm is changed:hopefully better,and an elusive bug removed.
// 下面是 getblk 函数,该函数的逻辑并不是很清晰,同样也是因为要考虑竞争条件问题.其中大部分
// 代码很少用到(例如重复操作语句),因此它应该比看上去的样子有效得多.
// 算法已经做出了改变:希望能更好.而且一个嫩难以捉摸的错误已经去除.

// 下面宏定义用于同时判断缓冲区的修改标志和锁定标志,并且定义修改标志的权重要比锁定标志大.
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)
// 取高速缓冲中指定的缓冲区.检查所指定的缓冲区是否已经在高速缓冲区中,如果不在,就需要在高速缓冲
// 中建立一个对应的新项.返回相应缓冲区头指针.
struct buffer_head * getblk(int dev,int block){
  struct buffer_head * tmp,*bh;
repeat:
// 搜索 hash 表,如果指定块已经在高速缓冲中,则返回对应缓冲区头指针,退出.
  if(bh=get_hash_table(dev,block))  return bh;
// 扫描空闲数据块链表,寻找空闲缓冲区.首先让 tmp 窒息那个空闲链表的第一个空闲缓冲区头.
  tmp=free_list;
  do{
    if(tmp->b_count)  continue;
    if(!bh||BADNESS(tmp)<BADNESS(bh)){
      bh=tmp;
      if(!BADNESS(tmp))
        break;
    }
  }while((tmp=tmp->b_next_free)!=free_list);
  if(!bh){
    sleep_on(&buffer_wait);
    goto repeat;
  }
  wait_on_buffer(bh); // 等待该缓冲区解锁(如果已被上锁的话)
  if(bh->b_count) goto repeat;  // 如果该缓冲区又被其它任务使用的话,只好重复上述过程.
// 如果该缓冲区已被修改,则将数据写盘,并再次等待缓冲区解锁.如果该缓冲区又被其它任务使用的话,
// 只好重复上述过程.
  while(bh->b_dirt){
    sync_dev(bh->b_dev);
    wait_on_buffer(bh);
    if(bh->b_count) goto repeat;
  }

// NOTE!! While we slept waiting for this block,somebody else might already have
// added "this" block to the cache. check it
// 注意!!当进程为了等待该缓冲块而睡眠时,其它进程可能已经将该缓冲块加入高速缓冲中,所以要对此进行检查.

// 在高速缓冲 hash 表中检查指定缓冲区是否已经加入.如果是的话,就重复上述过程.
    if(find_buffer(dev,block))  goto repeat;
// OK,FINALLY we know that this buffer is the only one of it's kind,and that it's
// unused (b_count=0),unlocked (b_lock=0),and clean
// 最终我们知道该缓冲区使指定参数的唯一一块
// 而且还没有被使用(b_count=0),未被上锁(b_lock=0),并且是干净的(未被修改的)
// 于是让我们占用此缓冲区.置引用计数为 1,复位修改标志和有效(更新)标志.
  bh->b_count=1;
  bh->b_dev=0;
  bh->b_uptodate=0;
// 从 hash 队列和空闲块链表中移除该缓冲区头,让该缓冲区用于指定设备和其上的指定块.
  remove_from_queues(bh);
  bh->b_dev=dev;
  bh->b_blocknr=block;
// 然后根据此新的设备号和块号重新插入空闲链表和 hash 队列新位置处.并最终返回缓冲头指针.
  insert_into_queues(bh);
  return bh;
}

// 释放指定的缓冲区.等待该缓冲区解锁.引用计数递减 1.唤醒等待空闲缓冲区的进程.
void brelse(struct buffer_head * buf){
  if(!buf)  return;
  wait_on_buffer(buf);
  if(!(buf->b_count--)) panic("Trying to free buffer");
  wake_up(&buffer_wait);
}

// bread() reads a specified block and returns the buffer that contains it. It
// returns NULL block was unreadable.
// 从设备上读取指定的数据块并返回含有数据的缓冲区.如果指定的块不存在则返回 NULL.
struct buffer_head * bread(int dev,int block){
  struct buffer_head *bh;
// 在高速缓冲区中申请一块缓冲区,如果返回值指针式 NULL 指针,表示内核出错,死机.
  if(!(bh=getblk(dev,block))) panic("bread:getblk returned NULL\n");
  if(bh->b_uptodate)  return bh;
  ll_rw_block(READ,bh); // 否则调用 ll_rw_block(),产生读设备块请求.并等待缓冲区解锁.、
  wait_on_buffer(bh);
  if(bh->b_uptodate)  return bh;  // 如果该缓冲区已更新,则返回缓冲区头指针,退出
  brelse(bh); // 否则表明读设备操作失败,释放该缓冲区,返回 NULL 指针,退出
  return NULL;
}

// 复制内存块.从 from 地址复制一块数据到 to 位置.
#define COPYBLK(from,to)\
__asm__("cld\n\t"\
        "rep\n\t"\
        "movsl\n\t"\
        ::"c"(BLOCK_SIZE/4),"S"(from),"D"(to)\
        )
// bread_page reads four buffers into memory at the desired address.It's a function
// of its own,as there is some speed to be got by reading them all at the same
// time,not waiting for one to be read,and then another etc.
// bread_page 一次读取四个缓冲块内容读到指定的地址.他是一个完整的函数,因为同时读取四块可以获得
// 速度上的好处,不用等着读一块,再读一块了
// 读设备上一个页面(4 个缓冲块)的内容到内存指定的地址
void bread_page(unsigned long address,int dev,int b[4]){
  struct buffer_head * bh[4];
  int i;
  for(i=0;i<4;i++)
    if(b[i]){
// 取高速缓冲中指定设备和块号的缓冲区,如果该缓冲区数据无效则产生读设备请求.
      if(bh[i]=getblk(dev,b[i]))
        if(!bh[i]->b_uptodate)
          ll_rw_block(READ,bh[i]);
    }else bh[i]=NULL;
// 将 4 块缓冲区伤的内容顺序复制到指定地址处
  for(i=0;i<4;i++,address+=BLOCK_SIZE)
    if(bh[i]){
      wait_on_buffer(bh[i]);  // 等待缓冲区解锁(如果已上锁的话)
      if(bh[i]->b_uptodate) // 如果该缓冲区中有数据的话,则复制
        COPYBLK((unsigned long)bh[i]->b_data,address);
      brelse(bh[i]);  // 释放该缓冲区
    }
}

// Ok,breada can be used as bread,but additionally to mrk other blocks for reading
// as well.End the argument list with a negative number.
// breada 可以像 bread 一样使用,但会另外预读一些模块.该函数参数列表需要使用一个负数来表明
// 参数列表的结束
// 从指定设备读取指定的一些块.成功时返回第 1 块的缓冲去头指针,否则返回 NULL
struct buffer_head * breada(int dev,int first,...){
  va_list args;
  struct buffer_head *bh,*tmp;
  va_start(args,first); // 取可变参数表中第 1 个参数(块号)
// 取高速缓冲中指定设备和块号的缓冲区.如果缓冲区数据无效,则发出读设备数据块请求
  if(!(bh=getblk(dev,first))) panic("bread:getblk returned NULL\n");
  if(!bh->b_uptodate) ll_rw_block(READ,bh);
// 然后顺序取可变参数表中其他预读块号,并作与上面同样处理,但不引用,注意, 330 行上又一个 bug.
// 其中的 bh 应该是 tmp.这个 bug 直到 0.96 版的内核代码中才被纠正过来.这里已修改
  while((first=va_arg(args,int))>=0){
    tmp=getblk(dev,first);
    if(tmp){
      if(!tmp->b_uptodate)
        ll_rw_block(READ,tmp);
      tmp->b_count--;
    }
  }
// 可变参数表中所有参数处理完毕.等待第 1 个缓冲区解锁(如果已被上锁)
  va_end(args);
  wait_on_buffer(bh);
// 如果缓冲区中数据有效,则返回缓冲区头指针,退出.否则释放该缓冲区,返回 NULL,退出.
  if(bh->b_uptodate)  return bh;
  brelse(bh);
  return NULL;
}

// 缓冲区初始化函数.参数 buffer_end 是指定的缓冲区内存的末端.若系统有 16 MB 内存,则缓冲区末端
// 设置为 4 MB.若系统有 8 MB 内存,缓冲区末端设置为 2 MB
void buffer_init(long buffer_end){
  struct buffer_head * h=start_buffer;
  void * b;
  int i;
// 如果缓冲区高端等于 1 MB,则由于从 640 KB~1 MB 被显示内存和 BIOS 占用,因此实际可用缓冲区内存
// 高端应该是 640 KB.否则内存高端一定大于 1 MB.
  if(buffer_end==1<<20) b=(void *)(640*1024);
  else b=(void*)buffer_end;
// 这段代码用于初始化缓冲区,简历空闲缓冲区环链表,并获取系统中缓冲块的数目.操作的过程是从缓冲区
// 高端开始划分 1 KB 大小的缓冲块,与此同时在缓冲取低端简历描述该缓冲块的结构 buffer_head,并将这些
// buffer_head 组成双向链表.h 是指向缓冲头结构的指针,而 h+1 是指向内存地址连续的下一个缓冲头地址,
// 也可以说是指向 h 缓冲头的末端外.为了保证有足够长的内存来存储一个缓冲头结构,
// 需要 b 所指向的内存块地址 >=h 缓冲头的末端,即需要 >=h+1
  while((b-=BLOCK_SIZE)>=((void*)(h+1))){
    h->b_dev=0; // 使用该缓冲区的设备号
    h->b_dirt=0;  // 脏标志,也即缓冲区修改标志
    h->b_count=0; // 该缓冲区引用计数
    h->b_lock=0;  // 缓冲区锁定标志
    h->b_uptodate=0;  // 缓冲区更新标志(或称数据有效标志)
    h->b_wait=NULL; // 指向等待该缓冲区解锁的进程
    h->b_next=NULL; // 指向具有相同 hash 值的下一个缓冲头
    h->b_prev=NULL; // 指向具有相同 hash 值的前一个缓冲头
    h->b_data=(char*)b; // 指向对应缓冲区块(1024 字节)
    h->b_prev_free=h-1; // 指向链表中前一项
    h->b_next_free=h+1; // 指向链表中下一项
    h++;  // h 指向下一新缓冲头位置
    NR_BUFFERS++; // 缓冲区块数累加
    if(b==(void*)0x100000)  b=(void*)0xa0000; // 如果地址 b 递减到等于 1 MB,则跳过 384 KB,
                                              // 让 b 指向地址 0xa0000(640 KB)处
  }
  h--;  // 让 h 指向最后一个有效缓冲头
  free_list=start_buffer; // 让空闲链表头指向头一个缓冲区头
  free_list->b_prev_free=h; // 链表头的 b_prev_free 指向前一项(即最后一项)
  h->b_next_free=free_list; // h 的下一项指针指向第一项,形成一个环链
  for(i=0;i<NR_HASH;i++)  hash_table[i]=NULL; // 初始化 hash 表(散列表),置表中所有的指针为 NULL
}
