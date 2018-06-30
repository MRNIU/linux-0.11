/*
 *  serial.c
 *
 * This moudle implements the rs232 io functions
 *  void rs_write(struct tty_struct * queue(;))
 *  void rs_init(void);
 * and all interrupts pertaining to serial IO.
 */
/* 该程序用于实现 rs232 的输入输出功能
 *  void rs_write(struct tty_struct * queue(;))
 *  void rs_init(void);
 * 以及与传出 IO 有关系的所有中断处理程序
 */

 #include <linux/tty.h> // tty 头文件，定义了有关 tty_io，串行通信方面的参数、常数
 #include <linux/sched.h> // 调度程序头文件，定义了任务结构 task_struct、初始任务 0 的数据
 #include <asm/system.h>  // 段操作头文件。定义哦了有关段寄存器操作的嵌入式汇编函数
 #include <asm/io.h>  // io 头文件。定义硬件端口输入/输出宏汇编语句

 #define WAKEUP_CHARS (TTY_BUF_SIZE/4)  // 当队列中含有 WAKEUP_CHARS 个字符时，就开始发送

 extern void rs1_interrupt(void);
 extern void rs2_interrupt(void);
 // 初始化串行端口。port：串口 1-0x3F8,串口 2-0x2F8
static void init(int port){
  outb_p(0x80,port+3);  // set DLAB of line control reg 设置线路控制器的 DLAB 位 (位 7)
  outb_p(0x30,port);  // LS of divisor(48->2400 bps) 发送波特率因子低字节，0x30->2400 bps
  outb_p(0x00,port+1);  // MS of divisor  发送波特率因子高字节 0x00
  outb_p(0x03,port+3);  // rest DLAB 复位 DLAB 位，数据位为 8 位
  outb_p(0x0b,port+4);  // set DTR,RTS,OUT_2  设置 DTR，RTS，辅助用户输出 2
  outb_p(0x0d,port+1);  // enable all intrs but writes  除了写(写保持空)外，允许所有中断源中断
  (void)inb(port);  // read data port to reset things(?)  读数据口，已进行复位操作(?)
}
// 初始化串行中断程序和串行接口
void rs_init(void){
  set_intr_gate(0x24,rs1_interrupt);  // 设置串行口 1 的中断门向量(硬件 IRQ4 信号)
  set_intr_gate(0x23,rs2_interrupt);  // 设置串口 2 的中断门向量(硬件 IRQ3 信号)
  init(tty_table[1].read_q.data); // 初始化串行口 1(.data 是端口号)
  init(tty_table[2].read_q.data); // 初始化串行口 2
  outb(inb_p(0x21)&0xE7,0x21); // 允许主 8259A 芯片的 IRQ3，IRQ4 中断信号请求
}

/*
 * This routine gets called when tty_write has put something into the write_queue.
 * It must check wheter the queue is empty,and set the interrupt register accordingly.
 *
 *  void _rs_write(struct tty_struct * tty);
 */
/* 在 tty_write() 已将数据放入输出(写)队列时会调用下面的子程序。必须首先检查写队列是否为空，
 * 并相应设置中断寄存器
 */
// 串行数据发送输出
// 实际上只是开启川汉发送保持寄存器已空中断标志，在 UART 将数据发送出去后允许发中断信号。
void rs_write(struct tty_struct * tty){
  cli();  // 关中断
// 如果写队列不空，则从 0x3f9(或 0x2f9)首先读取中断允许寄存器内容，天上发送保持寄存器中断标志
// 允许位(位 1)后，再写回该寄存器。
  if(!EMPTY(tty->write_q))
    outb(inb_p(tty->write_q.data+1)|0x02,tty->write_q.data+1);
  sti();  // 开中断
}
