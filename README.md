# Linux-Kernel
来自 `Linux 内核完全注释` 的内容,从 0.11 内核版本开始,完成后参考 0.12 版本进行修改.旨在加深对操作系统的理解.

# 进度

## 内核引导启动程序
- [x] bootsect.s
- [x] setup.s
- [x] head.s



## 内核初始化
- [ ] main.c

##进程调度与系统调用
- [ ] asm.s
- [ ] traps.c
- [ ] system_call.s
- [x] mktime.c
- [ ] sched.c
- [x] signal.c
- [ ] exit.c
- [ ] fork.c
- [x] sys.c
- [x] vsprintf.c
- [x] printk.c
- [x] panic.c


## 输入输出系统--块设备驱动程序
- [ ] blk.h
- [ ] hd.c
- [x] ll_rw_blk.c
- [ ] ramdisk.c
- [x] floppy.c


## 输入输出系统--字符设备驱动程序
- [x] keyboard.S
- [x] console.c
- [x] serial.c
- [x] rs_io.s
- [x] tty_io.c
- [ ] tty_ioctl.c


## 数学协处理器
- [x] math-emulation.c


## 文件系统
- [ ] buffer.c
- [ ] bitmap.c
- [ ] inode.c
- [ ] namei.c
- [ ] file_table.c
- [ ] block_dev.c
- [ ] file_dev.c
- [ ] pipe.c
- [ ] char_dev.c
- [ ] read_write.c
- [ ] truncate.c
- [ ] open.c
- [ ] exec.c
- [ ] stat.c
- [ ] fcntl.c
- [ ] ioctl.c



## 内存管理
- [ ] memory.c
- [ ] page.c


## 包含文件



## 内核库文件



## 内核组建工具


# 参考资料
- Linux 内核完全注释--赵炯 机械工业出版社,ISBN:978-7-111-14968-2



