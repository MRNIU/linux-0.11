/*
 *    console.c
 *
 * This moudle implements the console io functions
 *    'void con_init(void)'
 *    'void con_write(struct tty_queue * queue)'
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl
 */
/* 该模块实现控制台输入输出功能
 *   'void con_init(void)'
 *   'void con_write(struct tty_queue * queue)'
 * 希望这是一个非常完整的 VT102 实现。感谢 John T Kohl 实现了蜂鸣指示
 */
/*
 * NOTE!!!We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO),but this will work even for keyboard interrupts.
 * We know interrupts aren't enabled when getting a keyboard interrupt, as we use
 * trap-gates. Hopefully all is well.
 */
/* 注意！！！我们有时短暂地禁止和允许中断(在将一个字(word)放到视频 IO)，但即使对于键盘中断这也是
 * 可以工作的。因为我们使用陷阱门，所以我们知道在获得一个键盘中断时中断是不允许的。希望一切均正常。
 */
/*
 * Code to check for different video-cards mostly by Galen Hunt.
 * <g-hunt@ee.utah.edu>
 */
/* 检测不同显示卡的代码大多数是 Galen Hunt 编写的，<g-hunt@ee.utah.edu>
 */
#include <linux/sched.h>  // 调度程序头文件，定义任务结构 task_struct、初始任务 0 的数据
#include <linux/tty.h>  // tty 头文件，定义了有关 tty_io,串行通信方面的参数、常数
#include <asm/io.h> // io 头文件。定义硬件端口输入/输出宏汇编语句
#include <asm/system.h> // 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏

// These are set up by the sepup-routine at boot-time
// 这些是设置子程序 setup 在引导启动系统时设置的参数
// 参见对 boot/setup.s 的注释，和 setup 程序读取并保留的参数表
#define ORIG_X  // (*(unsigned char *)0x90000) 光标列号
#define ORIG_Y  // (*(unsigned char *)0x90001) 光标行号
#define ORIG_VIDEO_PAGE // (*(unsigned short *)0x90004) 显示页面
#define ORIG_VIDEO_MODE // ((*(unsigned short *)0x90006)&0xff) 显示模式
#define ORIG_VIDEO_COLS // (((*(unsigned short *)0x90006)&0xff00)>>8) 字符列数
#define ORIG_VIDEO_LINES (25) // 显示行数
#define ORIG_VIDEO_EGA_AX // (*(unsigned short *)0x90008) [??]
#define ORIG_VIDEO_EGA_BX // (*(unsigned short *)0x9000a) 显示内存大小和色彩模式
#define ORIG_VIDEO_EGA_CD // (*(unsigned short *)0x9000c) 显示卡特性参数
// 定义显示器单色/彩色显示模式类型符号常数
#define VIDEO_TYPE_MDA 0x10 // Monochrome Text Display 单色文本
#define VIDEO_TYPE_CGA  0x11  // CGA Display  CGA 显示器
#define VIDEO_TYPE_EGAM 0x20  // EGA/VGA Display  EGA/VGA 单色
#define VIDEO_TYPE_EGAC 0x21  // EGA/VGA in Color Mode EGA/VGA 彩色

#define NPAR 16

extern void keyboard_interrupt(void); // 键盘中断处理程序

static unsigned char video_type;  // Type of display being used 使用的显示类型
static unsigned long video_num_columns; // Number of text columns 屏幕文本列数
static unsigned long video_size_row;  // Bytes per row 每行使用的字节数
static unsigned long video_num_lines; // Number of test lines 屏幕文本行数
static unsigned char video_page;  // Initial cideo page 初始显示页面
static unsigned long video_mem_start; // Start of video RAM 显示内存起始地址
static unsigned long video_mem_end; // End of video RAM(sort of) 显示内存结束(末端)地址
static unsigned char video_port_reg;  // Video register select port 显示控制索引寄存器端口
static unsigned char video_port_val;  // Video register value port 显示控制器寄存器端口
static unsigned char video_erase_char;  // Char+Attrib to erase with 擦除字符属性与字符(0x0720)

// 以下这些变量用于屏幕卷屏操作
static unsigned long origin;
// Used for EGA/VGA fast scroll 用于 EGA/VGA 快速滚屏  scr_start 滚屏起始地址
static unsigned long scr_end;
// Used for EGA/VGA fast scroll 用于 EGA/VGA 快速滚屏 滚屏末端内存地址
static unsigned long pos; // 当前光标对应的显示内存地址
static unsigned long x,y; // 当前光标位置
static unsigned long top,bottom;  // 滚定时顶行行号；底行行号
// state 用于标明处理 ESC 转义序列时的当前步骤。npar,par[] 用于存放 ESC 序列的中间处理参数
static unsigned long state=0; // ANSI 转义字符序列处理状态
static unsigned long npar,par[NPAR];  // ANSI 转义字符序列参数个数和参数数组
static unsigned long ques=0;
static unsigned char attr=0x07; // 字符属性(黑底白字)

static void sysbeep(void);  // 系统蜂鸣参数

// this is what the terminal answers to a ESC-Z or csi0c query (= vt100 response)
// 下面是中断回应 ESC-Z 或 csi0c 请求的应答(= vt100 响应)
// csi - 控制序列引导码(Control Sequence Introducer)
#define RESPONSE "\033[?1;2c"

// NOTE!gotoxy thinks x==video_num_columns is ok
// 注意！gotoxy 函数认为 x==video_num_columns，这是正确的
// 跟踪光标当前位置。参数 new_x - 光标所在列号；new_y - 光标所在行号
// 更新当前光标位置变量 x，y，并修正 pos 指向光标在显示内存中的对应位置
static inline void gotoxy(unsigned int new_x,unsigned int new_y){
// 如果输入的光标行号超出显示器列数，或者光标行号超出显示的最大行数，则退出
  if(new_x>video_num_columns||new_y>=video_num_lines) return;
  x=new_x;
  y=new_y;
  pos=origin+y*video_size_row+(x<<1);
}
// 设置滚屏起始显示内存地址
static inline void set_origin(void){
  cli();
// 首先选择显示控制数据寄存器 r12，然后写入卷屏起始地址高字节。向右移动 9 位，表示向右移动 8 位，
// 在除以 2(2 字节代表屏幕上的 1 字符)。是相对与默认显示内存操作的。
  outb_p(12,video_port_reg);
  outb_p(0xff&((origin-video_mem_start)>>9),video_port_val);
// 再选择显示控制数据寄存器 r13，然后写入卷屏起始地址底字节。向右移动 1 位 表示除以 2
  outb_p(13,video_port_reg);
  outb_p(0xff&((origin-video_mem_start)>>1),video_port_val);
  sti();
}
// 向上卷动一行(屏幕窗口向下移动)
// 将屏幕窗口向下移动一行。参见程序后列表说明。
static coid scrup(void){
// 如果显示类型是 EGA，则执行以下操作
  if(video_type==VIDEO_TYPE_EGAC||video_type==VIDEO_TYPE_EGAM){
// 如果移动起始行 top=0,移动最底行 bottom=video_num_lines=25,则表示整屏窗口向下移动
    if(!top&&bottom==video_num_lines){
// 调整屏幕显示对应内存的起始位置指针 origin 为乡下移一行屏幕字符对应的内存位置，同时也调整当前
// 光标对应的内存位置以及屏幕末行末端字符指针 src_end 的位置。
      origin+=video_size_row;
      pos+=video_size_row;
      scr_end+=video_size_row;
// 若屏幕末端最后一个显示字符所对应的显示内存指针 scr_end 超出了实际显示内存的末端，则将屏幕内容
// 数据移动到显示内存的起始位置 video_mem_start 处，并在出现的新行上填入空格字符
      if(scr_end>video_size_row){
// %0 - eax(擦除字符+属性)；%1 - ecx((显示器字符行数-1)所对应的字符数/2，是以长字移动)；
// %2 - edi(显示内存起始位置 video_mem_start);%3 - esi(屏幕内容所对应的内存起始位置 origin)
// 移动方向：[edi]->[esi],移动 ecx 个长字
        __asm__("cld\n\t" // 清方向位
                "rep\n\t" // 重复操作，将当前屏幕内存数据
                "movsl\n\t" // 移动到显示内存起始处
                "movl _video_num_columns,%1\n\t"  // ecx=1 行字符数
                "rep\n\t" // 在新行上填入空格字符
                "stosw"
                ::"a"(video_erase_char),
                "c"((video_num_lines-1)*video_num_columns>>1),
                "D"(video_mem_start),
                "S"(origin)
                :"cx","di","si");
// 根据屏幕内存数据移动后的情况，重新调整当前屏幕对应内存的起始指针、光标位置指针和屏幕末端对应
// 内存指针 scr_end
        scr_end-=origin-video_mem_start;
        pos-=origin-video_mem_start;
        origin=video_mem_start;
      }
      else{
// 如果调整后的屏幕末端对应的内存指针 scr_end 没有超出显示内存的末端 video_mem_end，则只需在
// 新行上填入擦除字符(空格字符)
// %0 - eax(擦除字符+属性)；%1 - ecx(显示器字符行数)；%2 - edi(屏幕对应内存最后一行开始处)
        __asm__("cld\n\t"
                "rep\n\t"
                "stosw"
                ::"a"(video_erase_char),
                "c"(video_num_columns),
                "D"(scr_end-video_size_row)
                :"cx","di");
      }
        set_origin(); // 向控制器写入新屏幕内容对应的内存起始位置值。
// 否则表示不是整屏移动。也即表示从指定行 top 开始的所有行向上移动 1 行(删除 1 行)。此时直接
// 将屏幕从指定行 top 到屏幕末端所有行对应的显示内存数据向上移动 1 行，并在新出现的行上填入擦除字符
// %0-eax(擦除字符+属性)；%1-ecx(top 行下 1 行开始到屏幕末行的函数多对应的内存长字数)；
// %2=edi(top 行所处的内存位置)；%3-esi(top+1 行所处的内存位置)
      }
      else{
        __asm__("cld\n\t" // 清方向位
                "rep\n\t" // 循环操作，将 top+1 到 bottom 行
                "movsl\n\t" // 所对应的内存块移到 top 行开始处
                "movl _video_num_columns,%%ecx\n\t"  // ecx=1 行字符数
                "rep\n\t" // 在新行上填入擦除字符
                "stosw"
                ::"a"(video_erase_char),
                "c"((bottom-top-1)*video_num_columns>>1),
                "D"(origin+video_size_row*top),
                "S"(origin+video_size_row*(top+1))
                :"cx","di","si");
      }
    }
// 如果显示类型不是 EGA(是 MDA)，则执行下面移动操作。因为 MDA 显示控制卡会自动调整超出显示范围
// 的情况，也即会自动翻卷指针，所以这里不对屏幕内容对应内存超出显示内存的情况单独处理。
// 处理方法与 EGA 非整屏移动情况完全一样。
    else{  // Not EGA/VGA
      __asm__("cld\n\t"
              "rep\n\t"
              "movsl\n\t"
              "movl _video_num_columns,%%ecx\n\t"
              "rep\n\t"
              "stosw"
              ::"a"(video_erase_char),
              "c"(bottom-top-1)*video_num_columns>>1),
              "D"(origin+video_size_row*top),
              "S"(origin+video_size_row*(top+1))
              :"cx","di","si");
    }
}
// 向下卷动一行(屏幕窗口向上移动)
// 将屏幕窗口向上移动一行，屏幕显示的内容向下移动 1 行，在被移动开始行的上方出现一新行。
// 参见程序列表后说明。处理方法与 scrup() 相似，只是为了在移动显示内存数据时不出现数据覆盖错误
// 情况，复制是以反方向进行的，即从屏幕倒数第 2 行的最后一个字符开始复制。
static void scrdown(void){
// 如果显示类型是 EGA，则执行下列操作
  if(video_type==VIDEO_TYPE_EGAC||video_type==VIDEO_TYPE_EGAM){
// %0-eax(擦除字符+属性);%1=ecx(top 行开始到屏幕末行 -1 行的行数所对用的内存长字数)；
// %2-edi(屏幕右下角最后一个长字位置)；%3-esi(屏幕倒数第 2 行最后一个长字位置)
// 移动方向：[esi]->[edi],移动 ecx 个长字
    __asm__("std\n\t" // 置方向位
            "rep\n\t" // 重复操作
            "movsl\n\t" // 对应的内存数据
            "addl $2,%%edi\n\t"
            // %edi has been decremented by 4 edi 已经减 4 ，因为也是反向填擦除字符。
            "movl _video_num_columns,%%ecx\n\t" // 置 ecx=1 行字符数
            "rep\n\t" // 将擦除字符填入上方新行中
            "stosw"
            ::"a"(video_erase_char),
            "c"((bottom-top-1)*video_num_columns>>1),
            "D"(origin+video_size_row*bottom-4),
            "S"(origin+video_size_row*(bottom-1)-4)
            :"ax","cx","di","si");
  }       // 如果不是 EGA 类型显示器，则执行以下操作(目前与上面完全一样)
  else{ // Not EGA/VGA
    __asm__("std\n\t"
            "rep\n\t"
            "movsl\n\t"
            "addl $2,%%edi\n\t" // %edi has been decremented by 4
            "movl _video_num_columns,%%ecx\n\t"
            "rep\n\t"
            "stosw"
            ::"a"(video_erase_char),
            "c"((bottom-top-1)*video_num_columns>>1),
            "D"(origin+video_size_row*bottom-4),
            "S"(origin+video_size_row*(bottom-1)-4)
            :"ax","cx","di","si");
  }
}
// 光标位置下移一行(1f--line feed 换行)
static void 1f(void){
// 如果光标没有在倒数第 2 行之后，则直接修改光标当前行变量 y++,并调整光标对应显示内存位置 pos(
// 加上屏幕一行字符所对应的内容长度)
  if(y+1<bottom){
    y++;
    pos+=video_size_row;
    return;
  }
  scrup();  // 否则需要将屏幕内容上移一行
}
// 光标上移一行(ri-reverse line feed 反向换行)
static void ri(void){
// 如果光标不在第 1 行上，则直接修改光标当前行标量 y--，并调整光标对应显示内存位置 pos,减去屏幕
// 上一行字符所对应的内存长度字节数
  if(y>top){
    y--;
    pos-=video_size_row;
    return;
  }
  scrdown();  // 否则需要将屏幕内容下移一行
}
// 光标回到第 1 列(0 列)左端(cr-carriage return 回车)
static void cr(void){
  poe-=x<<1;  // 光标所在的列号*2 即 0 列到光标所在列对应的内存字节长度
  x=0;
}
// 擦除光标前一字符(用空格替代)(del-delete 删除)
static void del(void){
// 如果光标没有处在 0 列，则将光标对应内存位置指针 pos 后退 2 字节(对应屏幕上一个字符)，然后
// 将当前光标变量列值减 1，并将光标所在位置字符擦除
  if(x){
    pos-=2;
    x--;
    *(unsigned short *)pos=video_erase_char;
  }
}
// 删除屏幕上与光标位置相关的部分，以屏幕为单位。csi-控制序列引导码(Control Sequence Introducer).
// ANSI 转义序列：'ESC[sJ]'(s=0 删除光标到屏幕底端；1 删除屏幕开始到光标处；2 整屏删除)。参数：
// par-对应上面 s
static void csi_J(int par){
  long count __asm__("cx"); // 设为寄存器变量
  long start __asm__("di");
// 首先根据三种情况分别设置需要删除的字符数和删除开始的显示内存位置。
  switch(par){
    case 0: // erase from cusor to end of display  擦除光标到屏幕底端
      count=(scr_end-pos)>>1;
      start=pos;
      break;
    case 1: // 擦除从屏幕开始到光标处的字符
      count=(pos-origin)>>1;
      start=origin;
      break;
    case 2: // 删除整个屏幕上的内容
      count=video_num_columns*video_num_lines;
      start=origin;
      break;
    default:
      return;
  }
// 然后使用擦数字符填写删除字符的地方
// %0 - ecx(要删除的字符数 count);%1-edi(删除操作开始地址)；%2-eax(填入的擦除字符)
  __asm__("cld\n\t"
          "rep\n\t"
          "stosw\n\t"
          ::"c"(count).
          "D"(start),"a"(video_erase_char)
          :"cx","di");
}
// 删除行内与光标位置相关的部分，以一行为单位
// ANSI 转移字符序列：'ESC[sK'(s=0 删除到行尾；1 从开始删除；2 整行都删除)
static void csi_K(int par){
  long count __asm__("cx"); // 设置寄存器变量
  long start __asm__("di");
// 首先根据三种情况分别设置需要删除的字符数和删除开始的显示内存位置
  switch(par){
    case 0: // erase from cussor to end of line  删除光标到行尾字符
      if(x>=video_num_columns)  return;
      count=video_num_columns-x;
      start=pos;
      break;
    case 1: // erase from start of line to cussor 删除从行开始到光标处
      start=pos-1(x<<1);
      count=(x<video_num_columns)?x:video_num_columns;
      break;
    case 2: // erase whole line  将整行字符全删除
      start=pos-(x<<1);
      count=video_num_columns;
      break;
    default:
      sreturn;
  }
// 然后使用擦除字符填写删除字符的地方
// %0-ecx (要删除的字符数 count)；%1-edi(删除操作开始地址)；%2-eax(填入的擦除字符)
  __asm__("cld\n\t"
          "rep\n\t"
          "stosw\n\t"
          ::"c"(count).
          "D"(start),"a"(video_erase_char)
          :"cx","di");
}
// 允许翻译(重显)(允许重新设置字符显示方式，比如加粗、加下划线、闪烁、反显等)
// ANSI 转移字符序列：'ESC[nm'.n=0 正常显示；1 加粗；4 加下划线；7 反显；27 正常显示
void csi_m(void){
  int i;

  for(i=0;i<=npar;i++)
}
