//
// Some correcting by tytso.
// tytso 做了一些纠正

#include <linux/sched.h>  // 调度程序头文件,定义任务结构 task_struct、初始任务 0 的数据
#include <linux/kernel.h> // 内核头文件.含有一些内核常用函数的原形定义
#include <linux/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。
#include <linux/mm.h> // 内存管理头文件.含有一些内核常用函数的原形定义
#include <asm/system.h> // 系统头文件.定义了设置或修改描述符/中断门等的嵌入式汇编宏

#include <string.h> // 字符串头文件.主要定义了一些有关字符串操作的嵌入函数
#include <fcntl.h> // 文件控制头文件。用于文件及其描述符的操作控制常数符号的定义。
#include <error.h> // 错误号头文件。包含系统中各种出错号(Linus 从 MINIX 中引进的)。
#include <const.h> // 常数符号头文件。目前仅定义了 i 节点中 i_mode 字段的各标志位。
#include <sys/stat.h> // 文件状态头文件.含有文件或文件系统状态结构 stat{} 和常量.

// 访问模式宏。x 是 include/fcntl.h 第 7 行开始定义的文件访问标志。
// 根据 x 值索引对应数值(数值表示 rwx 权限：r，w，rw，wxrwxrwx)(数值是 8 进制)。
#define ACC_MODE(x) ("\004\002\006\377"[(x)&O+ACCMODE])

// comment out this line if you want names > NAME_LEN chars to be truncated.
// Else they will be disallowed.
// 如果想让文件名长度 > NAME_LEN 的字符被截掉，就将下面定义注释掉
//#define NO_TRUNCATE

#define MAY_EXEC 1  // 可执行(可进入)
#define MAY_WRITE 2 // 可写
#define MAY_READ 4 // 可读


// permission()
// is ueed to check for read/write/execute permissions on a file.
// I don't know if we should look at just the euid or both euid and uid,
// but that should be easily changed.
// permission()
// 该函数用于检测一个文件的读/写/执行权限。我不知道是否只需检查 euid，还是需要检查 euid 和
// uid 两者，不过这很容易修改。

// 检测文件访问许可权限
// 参数：inode-文件对应的 i 节点；mask-访问属性屏蔽码。返回：访问许可返回 1，否则返回 0.
static int permission(struct m_inode * inode, int mask){
  int mode=inode->i_mode; // 文件访问属性
// 特殊情况：即使是超级用户(root)也不能读/写一个已被删除的文件
// special case: not even root can read/write a deleted file.
  // 如果 i 节点有对应的设备，但该 i 节点的连接数等于 0，则返回。
  if(inode->i_dev && ! inode->i_nlinks)
    return 0;
  // 否则，如果进程的有效用户 id(euid) 与 i 节点的用户 id 相同，则取文件宿主的用户访问权限
  else if(current->euid==inode->i_uid)
    mode>>=6;
  // 否则，如果进程的有效组 id(egid) 与 i 节点的组 id 相同，则取组用户的访问权限
  else if(current->egid==inode->i_gid)
    mode>>=3;
  // 如果上面所取得的访问权限与屏蔽码相同，或者是超级用户，则返回 1，否则返回 0
  if(((mode & masl & 0007)==mask)||suser())
    return 1;
  return 0;
}

// ok, we cannot use strncmp, as the name is not in our data space.
// Thus we'll have to use match. No big problem. Match also makes some sanity tests.
// NOTE! unlike srtncmp, match returns 1 for success, 0 for failure.
// 我们不能使用 srtncmp 字符串比较函数，因为名称不再我们的数据空间(不在内核空间)。
// 因为我们只能使用 match()。 问题不大。match() 同样也处理一些完整的测试。
// 注意！与 strncmp 不同的是 match() 成功时返回 1，失败时返回 0

// 指定长度字符串比较函数。参数：len-比较字符串的长度；name-文件名指针；de-目录项结构
// 返回：相同返回 1，不同返回 0
static int match(int len, const char * name, struct dir_entry * de){
  register int same __asm__("ax");
  // 如果目录项指针空，过着目录项 i 节点等于 0，或者要比较的字符串长度超过文件名长度，则返回 0
  if(!de||!de->inode||len>NAME_LEN)
    return 0;
  // 如果要比较的长度 len 小于 NAME_LEN, 但是目录项中文件名长度超过文件名长度，则返回 0
  if(len<NAME_LEN&&de->name[len])
    return 0;
  // 下面嵌入汇编语句，在用户数据空间(fs)执行字符串的比较操作。
  // %0-eax(比较结果 same)；
  // %1-eax(eax 初值 0)；
  // %2-esi(名字指针)；
  // %3-edi(目录项名指针)；
  // %4-ecx(比较的字节长度值 len)
  __asm__("cld\n\t" // 清方向位
          "fs;repe;cmpsb\n\t" // 用户空间执行循环比较 [esi++] 和 [edi++] 操作
        "setz %%al" // 若比较结果一样(z=0)则设置 al=1(same=eax)
          :"=a"(same)
          :""(0),"S"((long)name),"D"((long)de->name),"c"(len)
          :"cx","di","si");
  return same;  // 返回比较结果
}

// find_entry()
// finds an entry in the specified directory with the wanted name. It returns
// the cache buffer in which the entry was found, and the entry itself
// (as a parameter-res_dir). It does NOT read the inode of the entry-you'll
// have to do that yourself if you want to.
// This also takes care of the few special cases due to '..'-traversal over
// a pseudo-root and a mount point.
//
// find_entry()
// 在指定的目录中寻找一个与名字匹配的目录项。返回一个含有找到目录项的高速缓冲区以及目录项本身
// (作为一个参数 -res_dir)。并不读目录项的 i 节点，如果需要的话需自己操作。
// ".." 目录项，操作期间也会对几种特殊情况分别处理，例如横越一个伪根目录以及安装点
//
// 查找指定目录和文件名的目录项。
// 参数：dir-指定目录 i 节点的指针；name-文件名；namelen-文件名长度；
// 返回：高速缓冲区指针；res_dir-返回的目录项结构指针
static struct buffer_head * find_entry(struct m_inode ** dir, const char * name,
              int namelen, struct dir_entry ** res_dir){
  int entries;
  int bolock,i;
  struct buffer_head * bh;
  struct dir_entry * de;
  struct super_block * sb;
// 如果定义了 NO_TRUNCATE，则若头文件名长度超过最大长度 NAME_LEN，则返回
#ifdef NO_TRUNCATE
  if(namelen>NAME_LEN)
    return NULL;
#else // 如果没有定义 NO_TRUNCATE，则若文件名长度超过最大长度 NAME_LEN，则截短之
  if(namelen>NAME_LEN)
    namelen=NAME_LEN;
#endif
// 计算本目录中目录项项数 entries。置空返回目录项结构指针。
  entries=(*dir)->i_size/(sizeof(struct dir_entry));
  *res_dir=NULL;
  if(!namelen)  // 如果文件名长度等于 0，则退出
    return NULL;
// check for '..', as we might have to do some "magic" for it
// 检查目录项 '..'，因为可能需要对其特别处理
  if(namelen==2&&get_fs_byte(name)=='.'&&get_fs_byte(name+1)=='.'){
  // '..' in a pseudo-root results in a faked '.' (just change namelen)
  // 伪根中的 '..' 如同一个假 '.' (只需改变名字长度)
  if((*dir)==current->root)
    namelen=1;
    // 否则若该目录 i 节点号等于 ROOT_INO(1) 的话，说明是文件系统根节点。则取文件系统的
    // 超级块.
    else if((*dir)->i_num==ROOT_INO){
      // '..' over a mount-point results in 'dir' being exchanged for the mounted
      // directory-inode. NOTE! We set mounted, so that we can iput the new dir.
      // 在一个安装点上的 '..' 将导致目录交换到安装到文件系统的目录 i 节点。
      // 注意！由于设置了 mounted 标志，因而我们能够取出该新目录。
      sb=get_super((*dir)->i_dev);
      // 如果被安装到的 i 节点存在，则先释放原 i 节点，然后对被安装到的 i 节点进行处理。
      // 让 *dir 指向该被安装到的 i 节点；该 i 节点的引用数加 1
      if(sb->s_imount){
        iput(*dir);
        (*dir)=sb->s_imount;
        (*dir)->i_count++;
      }
    }
  }
  // 如果该 i 节点指向的第一个直接磁盘块号为 0，则返回 NULL，退出
  if(!(block=(*dir)->i_zone[0]))
    return NULL;
  // 从节点所在设备读取指定的目录项数据块，如果不成功，则返回 NULL，退出
  if(!(bh=bread((*dir)->i_dev,block)))
    return NULL;
  // 在目录项数据块中搜索匹配指定文件名的目录项，首先让 de 指向数据块，并在不超过目录中目录项
  // 数的条件下，循环执行搜索。
  i=0;
  de=(struct dir_entry *) bh->b_data;
  while(i<entries){
    // 如果当前目录项数据块已经搜索完，还没有找到匹配的目录项，则释放当前目录项数据块。
    if((char*)de>=BLOCK_SIZE+bh->b_data){
      brelse(bh);
      bh=NULL;
      // 再读入下一目录项数据块。若这块为空，则只要还没有搜索完目录中的所有目录项，就跳过该块，
      // 继续读入下一目录项数据块。若该块不为空，就让 de 指向该目录项数据块，继续搜索
      if(!(block=bmap(*dir,i/DIR_ENTRIES_PER_BLOCK))||!(bh=bread((*dir)->i_dev,block))){
        i+=DIR_ENTRIES_PER_BLOCK;
        continue;
      }
      de=(struct dir_entry *)bh->b_data;
    }
    // 如果找到匹配的目录项的话，则返回该目录项结构指针和该目录项数据块指针，退出。
    if(match(namelen, name, de)){
      *res_dir=de;
      return bh;
    }
    de++; // 否则继续在目录项数据块中比较下一个目录项
    i++;
  }
  // 若指定目录中的所有目录项都搜索完还没有找到相应的目录项，则是放目录项数据块，返回 NULL。
  brelse(bh);
  return NULL;
}

// add_entrty()
// adds a file entry to the specified directory, using the same semantics as
// find_entry(). It returns NULL if it failed.
//
// NOTE!! The inode part of 'de' is left at 0-which means you may not sleep
// between calling this and putting something into the entry, as someone else
// might have used it while you slept.
//
// add_entrty()
// 使用与 find_entry() 同样的方法，往指定目录中添加一文件目录项，如果失败则返回 NULL。
// 注意！！ 'de' (指定目录项结构指针)的 i 节点部分被设置为 0-这表示在调用该函数和往目录项中
// 添加信息之间不能睡眠，因为若睡眠那么其它人(进程)可能会已经使用了该目录项。
//
// 根据指定的目录和文件名添加目录项。
// 参数：dir-指定目录的 i 节点；name-文件名；namelen-文件名长度；
// 返回：高速缓冲区指针；res_dir-返回的目录项结构指针；
static struct buffer_head * add_entrty(struct m_inode * dir, const char * name,
                int namelen, struct dir_entry ** res_d){
  int block,i;
  struct buffer_head * bh;
  struct dir_entry * de;

  *res_dir=NULL;
  // 如果定义了 NO_TRUNCATE，则若文件名长度超过最大长度 NAME_LEN，则返回
#ifdef NO_TRUNCATE
  if(namelen>NAME_LEN)
    return NULL;
#else
  if(namelen>NAME_LEN)
    return NAME_LEN;
#endif
  if(!namelen)
    return NULL;
  // 如果该目录 i 节点所指向的第一个直接磁盘块号为 0，则返回 NULL 退出
  if(!(block=dir->i_zone[0]))
    return NULL;
  if(!(bh=bread(dir->i_dev, block)))  // 如果读取该磁盘块失败，则返回 NULL 并退出
    return NULL;
  // 在目录项数据块中循环查找最后未使用的目录项。首先让目录结构指针 de 指向高速缓冲的数据块开始处，
  // 即第一个目录项。
  i=0;
  de=(struct dir_entry *)bh->b_data;
  while(1){
    // 如果当前判别的目录项已经超出当前数据块，则释放该数据块，重新申请一块磁盘块 block。如果
    // 申请失败，则返回 NULL 退出。
    if((char *)de>=BLOCK_SIZE+bh-b_data){
      brelse(bh);
      bh=NULL;
      block=create_block(dir,i/DIR_ENTRIES_PER_BLOCK);
      if(!block)
        return NULL;
      if(!(bh=bread(dir->i_dev, block))){
        i+=DIR_ENTRIES_PER_BLOCK;
        continue;
      }
      // 否则，让目录项结构指针 de 指向该块的高速缓冲数据块开始处。
      de=(struct dir_entry *) bh->b_data;
    }
    // 如果当前所操作的目录项序号 i* 目录结构大小已经超过了该目录所指出的大小 i_size，则说明
    // 该第 i 个目录项还未使用，我们可以使用它。于是对该目录项进行设置(置该目录项的 i 节点指针为空)。
    // 并更新该目录的长度值(加上一个目录项的长度，设置目录的 i 节点已修改标志，再更新该目录的
    // 改变时间为当前时间)。
    if(i*sizeof(struct dir_entry)>=dir->i_size){
      de->inode=0;
      dir->i_size=(i+1)*sizeof(struct dir_entry);
      dir->i_dirt=1;
      dir->i_ctime=CURRENT_TIME;
    }
    // 若该目录项的 i 节点为空，则表示找到一个还未使用的目录项。于是更新目录的修改时间为当前时间。
    // 并从用户数据区复制文件名到该目录项的文件名字段，置相应的高速缓冲块为已修改标志。返回该目录
    // 项的指针以及该高速缓冲区的指针，退出。
    if(!de->inode){
      dir->i_mtime=CURRENT_TIME;
      for(i=0;i<NAME_LEN;i++)
        de->name[i]=(i<namelen)?get_fs_byte(name+i):0;
      bh->b_dirt=1;
      *res_dir=de;
      return bh;
    }
    de++; // 如果该目录项已被使用，则继续检测下一个目录项
    i++;
  }
  // 执行不到这里，也许 Linus 在写这段代码时是先复制了上面 find_entry() 的代码，而后修改的
  brelse(bh);
  return NULL;
}

// get_dir()
//
// Getdir traverses the pathname util it hits the  topmost directory.
// It returns NULL on failure.
//
// get_dir()
// 该函数根据给出的路径名进行搜索，直到达到最顶端的目录。如果失败则返沪 NULL

// 搜寻指定路径名的目录。参数：pathname-路径名
// 返回：目录的 i 节点指针。失败时返回 NULL.
static struct m_inode *get_dir(const char * pathname){
  char c;
  const char * thisname;
  struct m_inode * inode;
  struct buffer_head * bh;
  int namelen, inr, idev;
  struct dir_entry * de;
  // 如果进程没有设定根 i 节点，或者该进程根 i 节点的引用为 0，则系统出错，死机。
  if(!current->root||!current->root->i_count)
    panic("No cwd inode");
  // 如果用户指定的路径名的第一个字符是 '/'，则说明路径名是绝对路径名。则从根 i 节点开始操作。
  if((c=get_fs_byte(pathname))=='/'){
    inode=current->root;
    pathname++;
  }
  // 否则若第一个字符是其他字符，则表示给定的是相对路径名。应从进程的当前工作目录开始操作。
  // 则取进程当前工作目录的 i 节点
  else if(c)
    inode=current->pwd;
  else  // 否则表示路径名为空，出错。返回 NULL，退出
    return NULL;  // * empty name is bad 空的路径名是错误的
  inode->i_count++; // 将取得的 i 节点引用计数增 1
  while(1){
    // 若该 i 节点不是目录节点，或者没有可进入的访问许可，则释放该 i 节点，返回 NULL，退出
    thisname=pathname;
    if(!S_ISDIR(inode->i_mode)||!permission(inode, MAY_EXEC)){
      iput(inode);
      return NULL;
    }
    // 从路径名开始起搜索检测字符，直到字符已是结尾符(NULL)或者是 '/'，此时 namelen 正好是
    // 当前处理目录名的长度。如果最后也是一个目录名，但其后没有加 '/'，则不会返回该最后目录的
    // i 节点！
    // 比如： /var/log/httpd，将只返回 log/ 目录的 i 节点
    for(namelen=0;(c=get_fs_byte(pathname++))&&(c!='/');namelen++)
      // nothing
      ;
    // 若字符是结尾符 NULL，则表明已经到达指定目录，则返回该 i 节点指针，退出
    if(!c)
      return inode;
    // 调用查找指定目录和文件名的目录项函数，在当前处理目录中寻找子目录项。如果没有找到，则释放
    // 该 i 节点，并返回 NULL，退出
    if(!(bh=find_entry(&inode, thisname, namelen, &de))){
      iput(inode);
      return NULL;
    }
    // 取该子目录项的 i 节点号 inr 和设备号 idev，释放包含该目录项的钙素缓冲块和该 i 节点
    inr=de->inode;
    idev=inode->i_dev;
    brelse(bh);
    iput(inode);
    // 取节点号 inr 的 i 节点信息，若失败，则返回 NULL，退出。否则继续以该子目录的 i 节点进行操作。
    if(!(inode=iget(idev, inr)))
      return NULL;
  }
}

// dir_namei()
//
// dir_namei() returns the inode of the directory of the specified name, and the
// name within that directory.
//
// dir_namei()
// dir_namei() 函数返回指定目录名的 i 节点指针，以及在最顶层目录的名称。
// 参数：pathname-目录路径名；namelen-路径名长度
// 返回：指定目录名最顶层目录的 i 节点指针和最顶层目录名及其长度
static struct m_inode * dir_namei(const char * pathname, int * namelen, const char ** name){
  char c;
  const char * basename;
  struct m_inode *dir;
  // 取指定路径名最顶层目录的 i 节点，若出错则返回 NULL，退出
  if(!(dir=get_dir(pathname)))
    return NULL;
  // 对路径名 pathname 进行搜索检测，查找最后一个 '/' 后面的名字字符串，计算其长度，
  // 并返回最顶层目录的 i 节点指针。
  basename=pathname;
  while(c=get_fs_byte(pathname++))
    if(c=='/')
      basename=pathname;
  *namelen=pathname-basename-1;
  *namelen=basename;
  return dir;
}

// namei()
//
// is used by simple commamds to get the inode of a specified name.
// Open, link etc use their own routines, but this is enough for things like 'chmod'
// etc.
//
// namei()
// 该函数被许多简单的命令用于取得指定路径名称的 i 节点。open、link 等则使用它们自己的相应函数，
// 但对于像修改模式 'chmod' 等这样的命令，该函数已足够用了。
//
// 取指定路径名的 i 节点。参互：pathname-路径名。返回：对应的 i 节点
struct m_inode * namei(const char * pathname){
  const char * basename;
  int inr, dev,namelen;
  struct m_inode * dir;
  struct buffer_head * bh;
  struct dir_entry * de;
  // 首先查找指定路径的最顶层目录的目录名及其 i 节点，若不存在，则返回 NULL，退出。
  if(!(dir=dir_namei(pathname, &namelen, &basename)))
    return NULL;
  // 如果返回的最顶层名字的长度是 0，则表示该路径名以一个目录名为最后一项
  if(!namelen)  // special case: '/usr/' etc.
    return dir; // 对应于 '/usr/' 等情况
  // 在返回的顶层目录中寻找指定文件名的目录项的 i 节点。因为如果最后也是一个目录名，但其后
  // 没有加 '/'，则不会返回该最后目录的 i 节点！比如：/var/log/httpd, 将只返回 log/ 目录的
  // i 节点。因为 die_namei() 将不以 '/' 结束的最后一个名字当作一个文件名来看待。因此这里需要
  // 单独对这种情况使用寻找目录项 i 节点函数 find_entry() 进行处理.
  bh=find_entry(&dir, basename, namelen, &de);
  if(!bh){
    iput(dir);
    return NULL;
  }
  // 取该目录项的 i 节点号和目录的设备号，并释放包含该目录项的高速缓冲区以及目录 i 节点。
  inr=de->inode;
  dev=dir->i_dev;
  brelse(bh);
  iput(dir);
  // 取对应节号的 i 节点，修改其访问时间为当前时间，并置已修改标志。最后返回该 i 节点指针。
  dir=iget(dev, inr);
  if(dir){
    dir->i_atime=CURRENT_TIME;
    dir->i_dirt=1;
  }
  return dir;
}

// open_namei()
//
// namei for open-this is in fact almost the whole open-routine.
//
// open_namei()
// open() 所使用的 namei 函数-这其实是几乎完整的打开文件程序。
//
// 文件打开 namei 函数
// 参数：pathname-文件路径名；flag-文件打开标志；mode-文件访问属性；
// 返回：成功返回 0，否则返回出错码；res_inode-返回的对应文件路径名的 i 节点指针。
int open_namei(const char * pathname, int flag, int mode, struct m_inode ** res_inode){
  const char * basename;
  int inr, dev, namelen;
  struct m_inode * dir, * inode;
  struct buffer_head * bh;
  struct dir_entry * de;
  // 如果文件访问许可模式是只读(0)，但文件截 0 标志 O_TRUNC 却置位了，则改为只写标志
  if((flag&O_TRUNC)&&!(flag&O_ACCMODE))
    flag|=O_WRONLY;
  // 使用进程的文件访问许可屏蔽码，屏蔽掉给定模式中的相应位，并天上普通文件标志。
  mode&=0777 &~ current->umask;
  mode|=I_REGULAR;
  // 根据路径名寻找到对应的 i 节点，以及最顶端文件名及其长度
  if(!(dir=dir_namei(pathname, &namelen, &basename)))
    return -ENOENT;
  // 如果最顶端文件名长度为 0(例如：'/usr' 这种路径名的情况)，那么若打开操作不是创建、截 0，
  // 则表示打开一个目录名，直接返回该目录的 i 节点，并退出。
  if(!namelen){
    if(!(flag&(O_ACCMODE|O_CREAT|O_TRUNC))){
      *res_inode=dir;
      return 0;
    }
    iput(dir);
    return -EISDIR;
  }
  // 在 dir 节点对应的目录中取文件名对应的目录项结构 de 和该目录项所在的高速缓冲区。
  bh=find_entry(&dir, basename, namelen, &de);
  // 如果该高速缓冲指针为 NULL，则表示没有找到对应文件名的目录项，因此只可能是创建文件操作。
  if(!bh){
    // 如果不是创建文件，则释放该目录的 i 节点，返回出错号退出
    if(!(flag&O_CREAT)){
      iput(dir);
      return -ENOENT;
    }
    // 如果用户在该目录没有写的权利，则释放该目录的 i 节点，返回出错号退出
    if(!permission(dir, MAY_WRITE)){
      iput(dir);
      return -EACCES;
    }
    // 在目录节点对应设备上申请一个新 i 节点，若失败，则释放目录的 i 节点，并返回没有空间出错码。
    inode=new_inode(dir->i_dev);
    if(!inode){
      iput(dir);
      return -ENOSPC;
    }
    // 否则使用该新 i 节点，对其进行初始设置：置节点的用户 id；对应节点访问模式；置已修改标志
    inode->i_uid=current->euid;
    inode->i_mode=mode;
    inode->i_dirt=1;
    // 然后在指定目录 dir 中添加一新目录项
    bh=add_entrty(dir, basename, namelen, &de);
    // 如果返回的应该含有新目录项的高速缓冲区指针为 NULL，则表示添加目录项操作失败。于是将该新
    // i 节点的引用连接计数减 1；并释放该 i 节点与目录的 i 节点，返回出错码，退出
    if(!bh){
      inode->i_nlinks--;
      iput(inode);
      iput(dir);
      return -ENOSPC;
    }
    // 初始设置该新目录项：置 i 节点号为新申请到的 i 节点的号码；并置高速缓冲区已修改标志。然后释放
    // 该高速缓冲区，释放目录的 i 节点。返回新目录项的 i 节点指针，退出
    de->inode=inode->i_num;
    bh->b_dirt=1;
    brelse(bh);
    iput(dir);
    *res_inode=inode;
    return 0;
  }
  // 若上面在目录中取文件名对应的目录项结构操作成功(即 bh 不为 NULL)，取出该目录项的 i 节点
  // 号和其所在的设备号，并释放该高速缓冲区以及目录的 i 节点
  inr=de->inode;
  dev=dir->i_dev;
  brelse(bh);
  iput(dir);
  // 如果独占使用标志 O_EXCL 置位，则返回文件已存在出错码，退出
  if(flag&O_EXCL)
    return -EEXIST;
  // 如果取该目录项对应 i 节点的操作失败，则返回访问出错码，退出
  if(!(inode=iget(dev, inr)))
    return -EACCES;
  // 若该 i 节点是一个目录的节点并且访问模式是只读或读写，或者没有访问的许可权限，则释放该 i 节点，
  // 返回访问权限出错码，退出
  if((S_ISDIR(inode->i_mode)&&(flag&O_ACCMODE))||!permission(inode, ACC_MODE(flag))){
    iput(inode);
    return -EPERM;
  }
  inode->i_atime=CURRENT_TIME;  // 更新该 i 节点的访问时间自断为当前时间
  if(flag&O_TRUNC)  // 如果设立了截 0 标志，则将该 i 节点的文件长度截为 0
    truncate(inode);
  *res_inode=inode; // 最后返回该目录项 i 节点的指针，并返回 0(成功)
  return 0;
}

// 系统调用函数-创建一个特殊文件或普通文件节点(node)
// 创建名称为 filename，由 mode 和 dev 指定的文件系统节点(普通文件、设备特殊文件或命名管道)
// 参数：filename-路径名；mode-指定使用许可以及所创建节点的类型；dev-设备号
// 返回：成功则返回 0，否则返回出错码
int sys_mknod(const char * filename, int mode, int dev){
  const char * basename;
  int namelen;
  struct m_inode * dir, * inode;
  struct buffer_head * bh;
  struct dir_entry * de;

  if(!suser())  // 如果不是超级用户，则返回访问许可出错码
    return -EPERM;
  // 如果找不到对应路径名目录的 i 节点，则返回出错码
  if(!(dir=dir_namei(filename, &namlen, &basename)))
    return -ENOENT;
  // 如果最顶端的文件名长度为 0，则说明给出的路径名最后没有指定文件名，释放该目录 i 节点，返回
  // 出错码，退出
  if(!namelen){
    iput(dir);
    return -ENOENT;
  }
  // 如果在该目录中没有写的权限，则释放该目录的 i 节点，返回访问许可出错码，退出
  if(!permission(dir, MAY_WRITE)){
    iput(dir);
    return -EPERM;
  }
  // 如果对应路径名上最后的文件名的目录项已经存在，则释放包含该目录项的高速缓冲区，释放目录的
  // i 节点，返回文件已经存在出错码，退出
  bh=find_entry(&dir, basename, namelen, &de);
  if(bh){
    brelse(bh);
    iput(dir);
    return -EEXIST;
  }
  // 申请一个新的 i 节点，如果不成功，则释放目录的 i 节点，返回无空间出错码，退出
  inode->new_inode(dir->i_dev);
  if(!inode){
    iput(dir);
    return -ENOSPC;
  }
  // 设置该 i 节点的属性模式。如果要创建的是块设备文件或者是字符设备文件，则令 i 节点的直接块
  // 指针 0 等于设备号
  inode->i_mode=mode;
  if(S_ISBLK(mode)||S_ISCHR(mode))
    inode->i_zone[0]=dev;
  // 设置该 i 节点的修改时间、访问时间为当前时间
  inode->i_mtime=inode->i_atime=CURRENT_TIME;
  inode->i_dirt=1;
  // 在目录中新添加一个目录项，如果失败(包含该目录项的高速缓冲区指针为 NULL)，则释放目录的 i
  // 节点；所申请的 i 节点引用计数复位，并释放该 i 节点。返回出错码，退出。
  bh=add_entrty(dir, basename, namelen, &de);
  if(!bh){
    iput(dir);
    inode->i_nlinks=0;
    iput(inode);
    return -ENOSPC;
  }
  // 令该目录项的 i 节点字读啊等于新 i 节点号，置高速缓冲区已修改标志，释放目录和新的 i 节点，
  // 释放高速缓冲区，最后返回 0(成功)
  de->inode=inode->i_num;
  bh->b_dirt=1;
  iput(dir);
  ipit(inode);
  brelse(bh);
  return 0;
}

// 系统调用函数-创建目录。参数：pathname-路径名；mode-牡蛎使用的权限属性
// 返回：成功则返回 0，否则返回 出错码
int sys_mkdir(const char * pathname, int mode){
  const char * basename;
  int namelen;
  struct m_inode * dir, * inode;
  struct buffer_head * bh, * dir_block;
  struct dir_entry * de;

  if(!suser())  // 如果不是超级用户，则返回访问许可出错码
    return -EPERM;
  // 如果找不到对应路径名目录的 i 节点，则返回出错码
  if(!(dir=dir_namei(pathname, &namlen, &basename)))
    return -ENOENT;
  // 如果最顶端的文件名长度为 0，则说明给出的路径名最后没有指定文件名，释放该目录 i 节点，返回出错码，
  // 退出
  if(!namelen){
    iput(dir);
    return -ENOENT;
  }
  // 如果在该目录中没有写的权限，则释放该目录的 i 节点，返回访问许可出错码，退出
  if(!permission(dir, MAY_WRITE)){
    iput(dir);
    return -EPERM;
  }
  // 如果对应历经名上最后的文件名的目录项已经存在，则释放包含该目录项的高速缓冲区，释放目录的 i
  // 节点，返回文件已经存在出错码，退出
  bh=find_entry(&dir, basename, namelen, &de);
  if(bh){
    brelse(bh);
    iput(dir);
    return -EEXIST;
  }
  // 申请一个新的 i 节点，如果不成功，则释放目录的 i 节点，返回我空间出错码，退出
  inode=new_inode(dir->i_dev);
  if(!inode){
    iput(dir);
    return -ENOSPC;
  }
  // 置该新 i 节点对应的文件长度为 32(一个目录项的大小)，直接点已修改标志，以及节点的修改时间
  // 和访问时间。
  inode->i_size=32;
  inode->i_dirt=1;
  inode->i_mtime=inode->i_atime=CURRENT_TIME;
  // 为该 i 节点申请一块磁盘，并令节点第一个直接块指针等于该块号。如果申请是失败，则释放对应
  // 目录的 i 节点；复位新申请的 i 节点连接计数；释放该新的 i 节点，返回没有空间出错码，退出
  if(!(inode->i_zone[0]=new_block(inode->i_dev))){
    iput(dir);
    inode->i_nlinks--;
    iput(inode);
    return -ENOSPC;
  }
  // 置该新的 i 节点已修改标志。读新申请的磁盘块。若出错，则释放对应目录的 i 节点；释放申请的磁盘块；
  // 复位新申请的 i 节点连接技术；释放该新申请的 i 节点，返回没有空间出错码，退出
  inode->i_dirt=1;
  if(!(dir_block=bread(inode->i_dev, inode->i_zone[0]))){
    iput(dir);
    free_block(inode->i_dev, inode->i_zone[0]);
    inode->i_nlinks--;
    iput(inode);
    return -ERROR;
  }
  // 令 de 指向目录项数据块，置该目录项的 i 节点号字段等于新申请的 i 节点号，名字字段等于 "."
  de=(struct dir_entry *)dir_block->b_data;
  de->inode=inode->i_num;
  srtcpy(de->name, ".");
  inode->i_nlinks=2;
  // 然后设置该高速缓冲区已修改标志，并释放该缓冲区
  dir_block->b_dirt=1;
  brelse(dir_block);
  // 初始化设置新 i 节点的模式字段，并置该 i 节点已修改标志
  inode->i_mode=I_DIRECTORY|(mode&0777&~current->umask);
  inode->i_dirt=1;
  // 在目录中新添加一个目录项，如果失败(包含该目录项的高速缓冲区指针为 NULL)，则释放该目录的 i
  //节点；所申请的 i 节点引用连接计数复位，并释放该 i 节点。返回出错码，退出。
  bh=add_entrty(dir, basename, namelen, &de);
  if(!bh){
    iput(dir);
    free_block(inode->i_dev, inode->i_zone[0]);
    inode->i_nlinks=0;
    iput(inode);
    return -ENOSPC;
  }
  // 令该目录项的 i 节点字段等于新 i 节点号，置高速缓冲区已修改标志，释放目录和新的 i 节点，
  // 释放高速缓冲区，最后返回 0(成功)
  de->inode=inode->i_num;
  bh->b_dirt=1;
  dir->i_nlinks++;
  dir->i_dirt=1;
  iput(dir);
  iput(inode);
  brelse(bh);
  return 0;
}

// routine to check that the specified directory is empty (for mkdir)
// 用于检查指定的目录是否为空的子程序(用于 rmdir 系统调用函数)
// 检查指定目录是不是空的。
// 参数：inode-指定目录的 i 节点指针。
// 返回：1-空的；0-不空
static int empty_dir(struct m_inode * inode){
  int nr,block;
  int len;
  struct buffer_head * bh;
  struct dir_entry * de;
  // 计算指定目录中现有目录项的个数(应该起码由两个，即 '.' 和 '..' 连个鬼文件目录项)
  len=inode->i_size/sizeof(struct dir_entry);
  // 如果目录项个数少于 2 个或者该目录 i 节点的第 1 个直接块没有指向任何磁盘块号，或者相应
  // 磁盘块渡部叔，则显示警告信息 "设备 dev 上目录错"，返回 0(失败)。
  if(len<2||!inode->i_zone[0]||!(bh=bread(inode->i_dev, inode->i_zone[0]))){
    printk("warning-bad directory on dev %04x\n", inode->i_dev);
    return 0;
  }
  // 让 de 指向含有读出磁盘块数据的高速缓冲区第 1 项目录项
  de=(struct dir_entry *)bh->b_data;
  // 如果第 1 个目录项的 i 节点号字段值不等于该目录的 i 节点号，或者第 2 个目录项的 i 节点号
  // 字段为零，或者两个目录项的名字字段不分别等于 "." 和 ".."，则显示警告信息 "设备 dev 上目录错"
  // 并返回 0
  if(de[0].inode!=inode->i_num||!de[1].inode||strcmp(".", de[0].name)||strcmp("..", de[1].name)){
    printk("warning-bad directory on dev %04\n", inode->i_dev);
    return 0;
  }
  nr=2; // 令 nr 等于目录项序号；de指向第三个目录项
  de+=2;
  // 循环检测该目录中所有的目录项(len-2 个)，看有没有目录项的 i 节点号字段不为 0(被使用)
  while(ne<len){
    // 如果该快磁盘块中的目录项已经检测完，则释放该磁盘块的高速缓冲区，读取下一块含有目录项的
    // 磁盘块。若相应块没有使用(货已经不用，如文件已经删除等)，则继续读下一块，若读不出，则出错，
    // 返回 0. 否则让 de 指向读出块的第一个目录项。
    if((void *) de>=(void *)(bh->b_data+BLOCK_SIZE)){
      brelse(bh);
      block=bmap(inode, nr/DIR_ENTRIES_PER_BLOCK);
      if(!block){
        nr+=DIR_ENTRIES_PER_BLOCK;
        continue;
      }
      if(!(bh=bread(inode->i_dev, block)))
        return 0;
      de=(struct dir_entry *) bh->b_data;
    }
    // 如果该目录项的 i 节点号字段不等于 0，则表示该目录项目前正被使用，则释放该高速缓冲区，
    // 返回 0，退出
    if(de->inode){
      brelse(bh);
      return 0;
    }
    de++; // 否则，若还没有查询完该目录中的所有目录项，则继续检测
    nr++;
  }
  // 到这里说明该目录中没有找到已用的目录项(当然除了头两个以外)，则释放缓冲区，返回 1
  brelse(bh);
  return 1;
}

// 系统调用函数-删除指定名称的目录
// 参数：name-目录名(路径名)
// 返回：返回 0 表示成功，否则返回出错号
int sys_rmdir(const char * name){
  const chat * basename;
  int namelen;
  struct m_inode * dir, * inode;
  struct buffer_head * bh;
  struct dir_entry * de;
  // 如果不是超级用户，则返回访问许可出错码
  if(!suser())
    return -EPERM;
  // 如果找不到对应路径名的 i 节点，则返回出错码
  if(!(dir=dir_namei(name, &namelen, &basename)))
    return -ENOENT;
  // 如果最顶端的文件名长度为 0，则说明给出的路径名最后没有指定文件名，释放该目录 i 节点，返回
  // 出错码，退出
  if(!namelen){
    iput(dir);
    return -ENOENT;
  }
  // 如果在该目录中没有写的权限，则释放该目录的 i 节点，返回访问许可出错码，退出
  if(!permission(dir, MAY_WRITE)){
    iput(dir);
    return -EPERM;
  }
  // 如果对应路径名上最后的文件名的目录项不存在，则释放包含该目录项的高速缓冲区，释放目录的 i 节点，
  // 返回文件已经存在出错码，退出。否则 dir 是包含要被删除目录名的目录 i 节点，de 是要被删除的
  // 目录的目录项结构
  bh=find_entry(&dir, basename, namelen, &de);
  if(!bh){
    iput(dir);
    return -ENOENT;
  }
  // 取该目录项指明的 i 节点。若出错则释放目录 i 节点，并释放含有目录项的高速缓冲，返回出错号
  if(!(inode=iget(dir->i_dev, de->inode))){
    iput(dir);
    brelse(bh);
    return -EPERM;
  }
  // 若该目录设置了受限删除标志并且进程的有效用户 id 不等于该 i 节点用户 id，则表示没有权限删除
  // 该目录，则释放包含要删除目录名的目录 i 节点和该要删除目录的 i 节点，释放高速缓冲，返回出错码
  if((dir->i_mode&S_ISVTX)&&current->euid&&inode->i_uid!=current->euid){
    iput(dir);
    iput(inode);
    brelse(bh);
    return -EPERM;
  }
  // 如果要被删除的目录项的 i 节点的设备号不等于包含该目录项的目录的设备号，或者该被删除目录的
  // 引用连接计数大于 1(表示有符号连接等)，则不能删除该目录，于是释放包含要删除目录名的目录 i 节点
  // 和该要删除目录的 i 节点，释放高速缓冲区，返回出错码
  if(iode->i_dev!=dir->i_dev||inode->i_count>1){
    iput(dir);
    iput(inode);
    brelse(bh);
    return -EPERM;
  }
  // 若要被删除的目录项 i 节点号等与包含该需要删除目录的 i 节点号，则表示试图删除 "." 目录。
  // 于是释放包含要删除目录名的目录 i 节点和该要删除目录的 i 节点，释放高速缓冲区，返回出错码
  if(inode==dir){ // we may not delete ".", but "../dir" is ok
    iput(inode);  // 我们不可以删除 "."，但可以删除 "../dir"
    iput(dir);
    brelse(bh);
    return -EPERM;
  }
  // 若要被删除的目录的 i 节点的属性表明这不是一个目录，则释放包含要删除目录名的目录 i 节点和
  // 该要删除目录的 i 节点，释放高速缓冲区，返回出错码
  if(!S_ISDIR(inode->i_mode)){
    iput(inode);
    iput(dir);
    brelse(bh);
    return -ENOTDIR;
  }
  // 若该需被删除的目录不空，则释放包含要删除目录名的目录 i 节点和该要删除目录的 i 节点，释放
  // 高速缓冲区，返回出错码
  if(!empty_dir(inode)){
    iput(inode);
    iput(dir);
    brelse(bh);
    return -ENOTEMPTY;
  }
  // 若该需被删除目录的 i 节点的连接数不等于 2，则显示警告信息
  if(inode->i_nlinks!=2)
    printk("empty directory has nlink! =2(%d)",inode->i_nlinks);
  // 置该需要被删除目录的目录项的 i 节点号字读啊为 0，表示该目录项不再使用，并置含有该目录项的
  // 高速缓冲区已修改标志
  dir->i_nlinks=0;
  bh->b_dirt=1;
  brelse(bh);
  // 置被删除目录的 i 节点的连接数为 0，并置 i 节点已修改标志
  inode->i_nlinks=0;
  inode->i_dirt=1;
  // 将包含被删除目录名的目录的 i 节点引用计数减 1，修改其改变时间和修改时间为当前时间，
  // 并置该节点已修改标志
  dir->i_nlinks--;
  dir->i_ctimr=dir->i_mtimr=CURRENT_TIME;
  dir->i_dirt=1;
  // 最后释放班汉要删除目录名的目录 i 节点和要删除目录的 i 节点，返回 0(成功)
  iput(dir);
  iput(inode);
  return 0;
}

// 系统调用函数-删除文件名以及可能也删除其相关的文件。
// 从文件系统删除一个名字。如果是一个文件的最后一个连接，并且没有进程正打开该文件，则该文件也将
// 被删除，并释放所占用的设备空间
// 参互：name-文件名。
// 返回：成功则返回 0，否则返回出错号
int sys_unlink(const chat * name){
  const char * basename;
  int namelen;
  struct m_inode * dir, * inode;
  struct buffer_head * bh;
  struct dir_entry * de;
  // 如果找不到对应路径名目录的 i 节点，则返回出错码
  if(!(dir=dir_namei(name, &namelen, &basename)))
    return -ENOENT;
  // 如果最顶端的文件名长度为 0，则说明给出的路径名最后没有指定文件名，释放该目录 i 节点，返回
  // 出错码，退出
  if(!namelen){
    iput(dir);
    return -ENOENT;
  }
  // 如果在该目录中没有写的权限，则释放该目录的 i 节点，返回访问许可出错码，退出
  if(!permission(dir, MAY_WRITE)){
    iput(dir);
    return -EPERM;
  }
  // 如果对应路径名上最后的文件名的目录项不存在，则释放包含该目录想的高速缓冲区，释放目录的 i 节点，
  // 返回文件已经存在出错码，退出。否则 dir 是包含要被删除目录名的目录 i 节点，de 是要被删除目录的目录项结构。
  bh=find_entry(&dir, basename, namelen, &de);
  if(!bh){
    iput(dir);
    return -ENOENT;
  }
  // 取该目录项指明的 i 节点。若出错则释放目录 i 节点，并释放含有目录项的高速缓冲，返回出错号。
  if(!(inode=iget(dir0>i_dev, de->inode))){
    iput(dir);
    brelse(bh);
    return -EPERM;
  }
  // 如果该指定文件名是一个目录，则也不能删除，释放该目录 i 节点和该文件名目录项的 i 节点，释放包含
  // 该目录项的缓冲区，返回出错号
  if(S_ISDIR(inode->i_mode)){
    iput(inode);
    iput(dir);
    brelse(bh);
    return -EPERM;
  } // 如果该 i 节点的连接数已经为 0，则显示警告信息，修正其为 1
  if(!inode->i_nlinks){
    printk("Deleting nonexistent file (%04x: %d), %d\n", inode->i_dev, inode->i_num,
            inode->i_nlinks);
    inode->i_nlinks=1;
  }
  // 将该文件名的目录项中的 i 节点号字段置为 0，表示释放该目录项，并设置包含该目录项的缓冲区
  // 已修改标志，释放该高速缓冲区
  de->inode=0;
  bh->b_dirt=1;
  brelse(bh);
  // 该 i 节点的连接数减 1.置已修改标志，更新改变时间为当前时间。最后释放该 i 节点和目录的 i 节点，
  // 返回 0(成功)
  inode->i_nlinks--;
  inode->i_dirt=1;
  inode->i_ctime=CURRENT_TIME;
  iput(inode);
  iput(dir);
  return 0;
}

// 系统调用函数-为文件建立一个文件名。唯一存在文件建一个新连接(也称硬连接-hard link)
// 参数：oldname-原路径名；newname-新的路径名。
// 返回：若成功则返回 0，否则返回出错号。
int sys_link(const chat * oldname, const chat * newname){
  struct dir_entry * de;
  struct m_inode * oldinode, * dir;
  struct buffer_head * bh;
  const char * basename;
  int namelen;
  // 取原文件路径名对应的 i 节点 oldinode。如果为 0，则表示出错，返回出错号。
  oldinode=namei(oldname);
  if(!oldinode)
    return -ENOENT;
  // 如果原路径名对应的是一个目录名，则释放该 i 节点，返回出错号
  if(S_ISDIR(oldinode->i_mode)){
    iput(dir);
    return -EPERM;
  }
  // 查找新路径名的最顶层目录的 i 节点，并返回最后的文件名长度。如果目录的 i 节点没有找到，则
  // 释放原路径名的 i 节点，返回出错号。
  dir=dir_namei(newname, &namelen, &basename);
  if(!dir){
    iput(oldname);
    return -EACCES;
  }
  // 如果新路径名中不包括文件名，则释放原路径名 i 节点和新路径名目录的 i 节点，返回出错号
  if(!namelen){
    iput(oldinode);
    iput(dir);
    return -EPERM;
  }
  // 如果新路径名目录的设备号与原路径名的设备号不一样，则也不能建立连接，于是释放新路径名目录
  // 的 i 节点和原路径名的 i 节点，返回出错号
  if(dir->i_dev!=oldinode->i_dev){
    iput(dir)
    iput(oldinode);
    return -EXDEV;
  }
  // 如果用户没有在新目录中写的权限，则也不能建立连接，于是释放新路径名目录的 i 节点和原路径
  // 名的 i 节点，返回出错号
  if(!permission(dir, MAY_WRITE)){
    iput(dir);
    iput(oldinode);
    return -EACCES;
  }
  // 查询该新路径名是否已经存在，如果存在，则也不能建立连接，于是释放包含在已存在目录项的高速缓冲区，
  // 释放新路径名目录的 i 节点，返回出错号
  bh=find_entry(&dir, basename, namelen, &de);
  if(bh){
    brelse(bh);
    iput(dir);
    iput(oldinode);
    return -EEXIST;
  }
  // 在新目录中添加一个目录项，若失败则释放该目录的 i 节点和原路径名的 i 节点，返回出错号。
  bh=add_entrty(dir, basename, namelen, &de);
  if(!bh){
    iput(dir);
    iput(oldinode);
    return -ENOSPC;
  }
  // 否则初始设置该目录项的 i 节点号等于原路径名的 i 节点号，并置包含该新添目录项的高速缓冲区
  // 已修改标志，释放该缓冲区，释放目录的 i 节点
  de->inode=oldinode->i_num;
  bh->b_dirt=1;
  brelse(bh);
  iput(dir);
  // 将原节点的应用计数加 1，修改其该表时间为当前时间，并设置 i 节点已修改标志，最后释放原路径名
  // 的 i 节点，并返回 0(成功)
  oldinode->i_nlinks++;
  oldinode->i_ctime=CURRENT_TIME;
  oldinode->i_dirt=1;
  iput(oldinode);
  return 0;
}
