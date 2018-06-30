!
! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes=196KB,more than enough for current
! versions of linux
! SYS_SIZE 是要加载的节数(16 字节为 1 节)。0x3000 共为 0x30000 字节=196 KB
！(若以 1024 字节为 1 KB计，则应该是 192 KB)，对于当前的版本空间已经足够了
SYS_SIZE=0x3000
! 指编译连接后的 system 模块大小，参见文件 linux/Makefile 中 124 行的说明。这里给出了一个
！最大默认值。
!     bootsect.s      (C)1991 Linus Torvalds
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines,and moves itself out
! the way to address 0x90000,and jumps there.
!
! It then loads 'setup' directly after itself (0x90200),and the system at 0x10000,
! using BIOS interrupts.
!
! NOTE! currently system is at most 8*65536 bytes long.This should be no peoblem,
! even in the future.I want to keep it simple.This 512 KB kernel size should be enough,
! especially as this doesn't contain the buffer cache as in minix
!
! The loaded has been made as simple as possible,and continuos read errors will
! result in a unbreakable loop.Reboot by hand.It loads pretty fast by getting whole
! sectors at a time wheneber possible.
! bootsect.s 被 bios- 启动自程序加载至 0x7c00 (31 KB) 处，并将自己移到了地址 0x90000 (576 KB)
! 处，并跳转至那里。它然后使用 BIOS 中断将 'setup' 直接加载到自己的后面 （0x90200) (576.5 KB),
！并将 system 加载到地址 0x10000 处。
！注意！目前的内核系统最大长度限制为 (8*65536)(512 KB) 字节，即使在将来这也应该没有问题的。我想让它
！保持简单明了。这样 512 KB 最大内核长度应该足够了，尤其是这里没有像 MINIX 中一样包含缓冲区高速缓冲。
！加载程序已经做得够简单了，所以持续的读出错将导致死循环。只能手工重启。只要可能，通过一次读取所有扇区，
！加载过程可以做的得很快。
.globl begtext,begdata,begbss,endtext,enddata,endbss  ! 定义了 6 个全局标识符;
.text ! 文本段；
begtext:
.data ! 数据段;
begdata:
.bss  ! 未初始化数据段(Block Started by Symbol)
.begbss:
.text

SETUPLEN  =4              ! nr of setup-sectors  setup 程序的扇区数值
BOOTSEG   =0x07c0         ! original address of boot-sector   bootsect 的原始段地址
INITSEG   =0x9000         ! we move boot here-out of the way    移动 bootsect 到这里
SETUPSEG  =0x9020         ! setup starts here   setup 程序从这里开始
SYSSEG    =0x1000         ! system loaded at 0x10000(65536)   system 加载到 64 KB 处
ENDSEG    =SYSSEG+SYSSIZE ! where to stop loading   停止加载的段地址

! ROOT_DEV:   0x000 - same type of floppy as boot.  根文件系统设备引导软驱设备;
!             0x301 - first partition on first drive etc. 在第 1 个硬盘的第 1 个分区上等;
ROOT_DEV  = 0x306 ! 指定根文件系统是第 2 个一硬盘的第 1 个分区。参见后面说明。

entry start    ! 告知连接程序，程序从 start 标号开始执行。
start:
! 55-64 行作用是将自身 (bootsect) 从目前段 0x07c0 (31 KB) 移动到 0x9000 (576 KB) 处，
！共 256 字(512 字节) ，然后跳转到移动后代码的 go 标号处，即本程序的下一语句处。
      mov ax,#BOOTSEG ！将 ax 段寄存器置为 0x07c0;
      mov ds,ax
      mov ax,#INITSEG ! 将 es 段寄存器置为 0x9000;
      mov es,ax
      mov cx,#256 ! 移动计数值=256 字；
      mov si,si ！源地址  ds:si=0x07c0:0x0000
      mov di,di ! 目的地址 es:di=0x9000:0x0000
      rep ! 重复执行，直到 cx =0
      movw  ！ 将DS：SI的内容送至ES：DI，note! 是复制过去，原来的代码还在。
      jmpi go,INITSEG ！段间跳转。这里 INITSEG 指出跳转到的段地址。
！从下面开始，CPU 执行已经移动到 0x9000 段处的代码。
go:
      mov ax,cs
      mov ds,ax
      mov es,ax
! put stack at 0x9ff00  ! 将堆栈指针 sp 指向 0x9ff00 (即 0x9000:0xff00) 处
      mov ss,ax
      mov sp,#0xFF00  ! arbitary value >>512
! 由于代码段移动过，所以要重新设置堆栈段的位置。 sp 只要指向远大于 512 偏移 (即地址 0x90200)
! 处都可以。因为从 0x90200 地址处开始还要放置 setup 程序，而此时 setup 程序大约为 4 个扇区，
！因此 sp 要指向大于 0x2000+0x200*4+堆栈大小 处。
！load the setup-sectors directly after the bootblock.
! Note the 'es' is already set up.
! 在 bootsect 程序块后紧跟着加载 setup 模块的代码数据
！注意 es 已经设置好了。(在移动代码时 es 已经指向目的段地址处 0x9000)。
load_setup:
! 86-95 行的用途是利用 BIOS 中断 INT 0x13 将 setup 模块从磁盘第 2 个扇区开始读到 0x90200 开始
！处，共读 4 个扇区。如果读出错则复位驱动器，并重试，没有退路。该中断用法为： ah=0x02-读磁盘扇区到内存；
！al=需要读出的扇区数量；ch=磁道(柱面)号的低 8 位；cl=开始扇区(位 0～5)，磁道号高 2 位(位 6～7)；
！dh=磁头号；dl=驱动器号(若是硬盘则需要置位 7)；es:bs-> 指向数据缓冲区。若出错则 CF 标志置位。
      mov dx,#0x0000  ! drive 0,head 0  驱动器 0 ，磁头 0;
      mov cx,#0x0002  ! sevtor 2,track 0  扇区 2 ，磁道 0;
      mov bx,#0x0200   ! address =512,in INITSEG  INITSEG 段 512 偏移处;
      mov ax,#0x0200+SETUPSEG ! service 2,nr of sectors 服务号 2 ，后面是扇区数;
      int 0x13        ! read it
      jnc ok_load_setup ! of - continue   若正常，则继续;
      mov dx,#0x0000
      mov ax,#0x0000    ! rest the diskette   复位磁盘
      int 0x13
      j load_setup

ok_load_setup:

! Get disk drive parameters,specifically nr of sectors/tarck
! 取磁盘驱动器参数，特别是每道扇区数量。区磁盘驱动器参数 INT 0x13 调用格式和返回信息如下：
！ah=0x80；dl=驱动器号(若是硬盘则要置位 7 为 1)。返回：ah=0，al=0；bl=驱动器类型(AT/PS2);
! ch=最大磁道好的低 8 位，dl=每磁道最大扇区数(位 0～ 5)，最大磁道号高 2 位(位 6～7)；dh=最大磁头数
！dl=驱动器数量；es:di 为软驱磁盘参数表。若出错则 CF 置位，且 ah=状态码。
      mov dl,#0x00
      mov ax,#0x0800  ! AH=8 if get drive parameters
      int 0x13
      mov ch,#0x00
      seg cs ! 表示下一条语句的操作数在 cs 段寄存器所指的段中。
      mov sectors,cx  ! 保存每磁道扇区数
      mov ax,#INITSEG
      mov es,ax ! 因为上面取磁盘参数中断修改了 es ，这里重新改回。

!Print some inane message 显示信息('Loading system ...'回车换行，共 24 个字符)
      mov ah,#0x30  ! read cursor pos
      xor bh,bh     ! 读光标位置
      int 0x10

      mov cx,#24    ！共 24 个字符
      mov bx,#0x0007  ！page 0，attribute 7(normal)
      mov bp,#msg1   !指向要显示的字符串
      mov ax,#0x1301  ！writting string,move cursor
      int 0x10    ! 写字符串并移动光标

! ok,we've written the message,now we want to load the system(at 0x10000)
! 现在开始将 system 模块加载到 64 KB 处

      mov ax,#SYSSEG
      mov es,ax   ! segment of 0x10000   es=system 段地址
      call read_it ！读磁盘上 system 模块，es 为输入参数
      call kill_motor ！关闭电动机，这样就可以知道驱动器的状态了

! After that we check which root_device to use. If the device is defined(!=0),nothing
! is done and the given device is used. Otherwise,either /dev/PS0 (2.28) or /dev/at0 (2,8),
! depending on the number of sectors that the BIOS reports currently.
! 此后，检查要使用哪个根文件系统设备(简称根设备)。如果已经指定了设备(!=0)，就直接使用给定的设备。
！否则就需要根据 BIOS 报告的每磁道扇区数来确定到底要使用 /dev/PS0 (2,28) 还是 /dev/at0 (2,8)。
！上面行中两个设备文件的含义： 在 Linux 中软驱的主设备号是 2 (参见第 50 行的注释)，次设备号=
！type*4+nr ,其中 nr 为 0-3 分别对应软驱 A、B、C 或 D ；type 是软驱的类型，(2->1.2 MB 或 7->1.44 MB 等)
! 因为 7*4+0=28 ，所以 /dev/PS0 (2,28) 指的是 1.44 MB A 驱动器，其设备号是 0x021c; /dev/at0 (2,8)
！是 1.2 MB A 驱动器，其设备号是 0x0208
      seg cs
      mov ax,root_dev   ! 取 508,509 字节出的根设备号并判断是否已被定义
      cmp ax,#0
      jne root_defined
      seg cs
      mov bx,sectors
! 取第 109 行保存的每磁道扇区数。若 sectors=15 ，则说明是 1.2 MB 的驱动器；如果 sectors=18 ，
！则说明是 1.44 MB 软驱。因为是可引导的驱动器，所以肯定是 A 驱。
      mov ax,#0x0208  ! /dev/ps0 -1.2 MB
      cmp bx,#15      ！判断每磁道扇区数是否 =15
      je root_defined ！如果等于，则 ax 中就是引导驱动器的设备号
      mov ax,#0x021c  ! /dev/PS0 - 1.44 MB
      cmp bx,#18
      je root_defined
undef_root:   ！如果都不一样，则死循环(死机)
      jmp undef_root
root_defined:
      seg cs
      mov root_dev,ax   ！将检查过的设备号保存起来

! after that (evering loaded), we jump to the setup-toutine loaded dircetly after
! the bootsect:
! 到此，所有程序都加载完毕，我们就跳转到被加载在 bootsect 后面的 setup 程序去。
      jmpi 0,SETUPSEG   ! 跳转到 0x9020:0000 (setup.s 程序的开始处)。程序到此结束。
！下面是两个子程序
! This routine loads the system at address 0x10000, makeing sure no 64 KB boundaries
! are crossed. We try to load it as fast as possible, loading whole tarcks whenever we can.
!
! in: es - starting address segment (normally 0x1000)
! 该子程序将系统模块加载到内存地址 0x10000 处，并确定没有跨越 64 KB 的内存边界。我们试图尽快地
！进行加载，只要可能，就每次加载整条磁道的数据。输入 es- 开始内存地址段值(通常是 0x1000)。
sread:  .word 1 + SETUPLEN
！sectors read of current track  磁道中已读扇区数。
！开始时已读入 1 扇区的引导扇区 bootsect 和 setup 程序所占的扇区 SETUPLEN
head:   .word 0   ! current head     当前磁头号
track:  .word 0   ! current tarck    当前磁道号

read_it:
！测试输入的测试。从盘上读如的数据必须存放在位于内存地址 64 KB 的边界开始处，否则进入死循环。
！清 bx 寄存器，用于便是当前段内存放数据的开始位置。
      mov ax,es
      test ax,#0x0fff
      jne die   ! es must be at 64 KB boundary  es 值必须位于 64 KB 地址边界。
      xor bx,bx ! bx is starting addresswithin segment  bx 为段内偏移位置。
rp_read:
! 判断是否已读入全部数据。比较当前所读段是否就是系统数据末端所处的段 (#ENDSEG) ，如果不是就跳转
！至下面 ok1_read 标号处继续读数据。否则推出子程序返回。
      mov ax,es
      cmp ax,#ENDSEG
      jb ok1_read
      ret
ok1_read:
! 计算和验证当前磁道需要读取的扇区数，放在 ax 寄存器中。根据当前磁道还未读取的扇区数以及段内数据字节
！开始偏移位置，计算如果全部读取这些未读扇区，所读总字节数是否会超过 64 KB 段长限制。若超过，则根据此次
！最多能读入的字节数 (64 KB - 段内偏移位置)，反算出此次需要读的扇区数。
      seg cs
      mov ax,sectors  ! 取每磁道扇区数
      sub ax,sread    ！减去当前磁道已读扇区数
      mov cx,ax       ！cx=ax=当前磁道未读扇区数
      shl cx,#9       ！cx=cx*512 字节
      add cx,bx      ！cx=cx+段内偏移值(bx),即此次操作后段内读入字节数。
      jnc ok2_read    ！若没有超过 64 KB 字节，则跳转至 ok2_read 处执行
      je ok2_read
      xor ax,ax       ！若加上此次将读磁道上所有未读扇区时会超过 64 KB，
      sub ax,bx       ！则计算此时最多能读入的字节数(64 KB - 段内读偏移位置)，
      shr ax,#9       ! 再转换成需要读的扇区数。
ok2_read:
      call read_track
      mov cx,ax       ! cx=该次操作已读取的扇区数
      add ax,sread    ！当前磁道上已读取的扇区数
      seg cs
      cmp ax,sectors  ！如果当前磁道上还有扇区未读，则跳转到 ok3_read 处。
      jne ok3_read
      mov ax,#1       ！下面读磁道另一磁头 (1 号磁头) 上的数据。如果已经完成，则去读下一磁道。
      sub ax,head     ！判断当前磁头号
      jne ok4_read    ！如果是 0 磁头，则再去读 1 磁头面上的扇区数据。
      inc track       ！否则去读下一磁道
ok4_read:
      mov head,ax       ！保存当前磁道号
      xor ax,ax       ！清当前磁道已读扇区数
ok3_read:
      mov sread,ax
      shl cs,#9
      add bx,cx
      jnc rp_read
      mov ax,es
      add ax,#0x1000
      mov es,ax
      xor bx,bx
      jmp rp_read
! 读当前磁道上指定开始扇区和需读扇区的数据到 es:bx 开始处，参见第 81 行下对 BIOS 磁盘读中断 int 0x13，
！ah=2 的说明。(al - 需读扇区数;es:bx - 缓冲区开始位置)
read_track:
      push ax
      push bx
      push cx
      push dx
      mov dx,track    ! 取当前磁道号
      mov cx,sread    ！取当前磁道上已读扇区数
      inc cx          ！cl=开始读扇区
      mov ch,dl       ！ch=当前磁道号
      mov dx,head     ！取当前磁道号
      mov dh,dl       ！dh=磁头号
      mov dl,#0       ！dl=驱动器号(为 0 表示当前驱动器)
      and dx,#0x0100  ！磁头号不大于 1
      mov ah,#2       ！ah=2，读磁盘扇区功能号
      int 0x13
      jc bad_rt       ！若出错，则跳转至 bad_rt
      pop dx
      pop cx
      pop bx
      pop ax
      ret
bad_rt:
      mov ax,#0   ! 执行驱动器复位操作(磁盘中断号 0)，再跳到 read_track 处重试
      mov dx,#0
      int 0x13
      pop dx
      pop cx
      pop bx
      pop ax
      jmp read_track

/*
  * This procedure turns off the floppy drive motor,so that we enter the kernel
  * in a known state,and don't have to worry about later
  */
/* 该子程序用于关闭软驱电动机，这样在进入内核后它处于已知状态，以后也就无需担心它了。 */
kill_motor:
      push dx
      mov dx,#0x3f2   ！软驱控制卡的驱动端口，只写
      mov al,#0       ！A 驱动器，关闭 FDC ，禁止 DMA 和中断请求，关闭电动机
      outb            ！将 al 中的内容输出到 dx 指定的端口去
      pop dx
      ret

sectors:
      .word 0         ! 存放当前启动软盘每磁道的扇区数

msg1:
      .byte 13,10     ！回车、换行的 ASCII 码
      .ascii "Loading system..."
      .byte 13,10,13,10   ！共 24 个 ASCII 码字符

.org 508    !表示下语句从地址 508(0x1fc) 开始，所以 root_dev 在启动扇区第 508 开始的 2B 中
root_dev:
      .word ROOT_DEV    ！这里存放根文件系统所在的设备号(init/main.c 中会用)
boot_flag:
      .word 0xAA55      ！硬盘有效标识

.text
endtext:
.data
enddata:
.bss
endbss:
