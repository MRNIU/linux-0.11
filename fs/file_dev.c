#include <errno.h> // 错误号头文件。包含系统中各种出错号(Linus 从 MINIX 中引进的)。
#include <fcntl.h> // 文件控制头文件。用于文件及其描述符的操作控制常数符号的定义

#include <linux/sched.h> // 调度程序头文件,定义任务结构 task_struct、初始任务 0 的数据
#include <linux/kernel.h> // 内核头文件.含有一些内核常用函数的原形定义
#include <asm/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数。
