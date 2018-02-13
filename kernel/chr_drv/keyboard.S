/*
 * Thanks to Alfred Leung for US keyboard patches
 *  Wolf Thiel for German keyboard patches
 *  Marc Corsini for the French keyboard
 */
/* 感谢 Alfred Leung 添加了美国英语键盘补丁程序
 * Wolf Thiel 添加了德语键盘补丁程序
 * Marc Corsini 添加了法文键盘补丁程序
 */
#include <linux/config.h> # 内核配置头文件。定义键盘语言和硬盘类型(HD_TYPE)可选项

.text
.globl _keyboard_interrupt

/*
 * these are for the keyboard read functions
 */
/* 以下这些用于读键盘操作
 */
# size 是键盘缓冲区的长度(字节数)
size=1024 // must be a power of two! And MUST be the same as in tty_io.c!!!
          // 数值必须是 2 的幂！并且与 tty_io.c 中的值匹配！！！
# 以下这些是缓冲队列结构中的偏移量
head=4  # 缓冲区中头指针字段偏移
tail=8  # 缓冲区中为指针字段偏移
proc_list=12  # 等待该缓冲队列的进程字段偏移
buf=16  # 缓冲区字段偏移
# mode 是键盘特殊键的按下状态标志
# 表示大小写转换(caps)、交换键(alt)、控制键()ctrl和换挡键(shift)的状态
# 位 7 caps 键按下；位 6 caps 键的状态(英语 leds 中的对应标志位一样)；
# 位 5  右 alt 键按下；位 4 左 alt 键按下；位 3 右 ctrl 键按下；
# 位 2 左 ctrl 键按下；位 1 右 shift 键按下；位 0 左 shift 键按下
mode: .byte 0 # caps,alt,ctrl and shift mode
  # 数字锁键(num-lock)、大小写转换键(caps-lock)和滚动锁定键(scroll-lock)的 LED 发光管状态。
  # 位 7-3 全 0 不用；位 2 caps-lock;
  # 位 1 num-lock(初始置 1，也即设置数字锁定键(caps-lock)发光管为亮；位 0 scroll-lock
leds: byte 2  # num-lock,caps,scroll-lock mode(nom-lock on)
# 当扫描码是 0xe0 或 0xe1 时，置该标志。表示气候还跟着 1 个或 2 个字符扫描码，见列表后说明。
# 位 1=1 收到 0xe1 标志；位 0=1 收到 0xe0 标志
e0: .byte 0

/*
 * con_int is the real interrupt  rountine that reads the kkeyboard scan-code and
 * converts it into the appropriate ascii character(s).
 */ # con_int 是实际的中断处理子程序，用于读键盘扫描码并将其转换成相应的 ascii 字符
_keyboard_interrupt:  # 键盘中断处理程序入口点
  pushl %eax
  pushl %ebx
  pushl %ecx
  pushl %edx
  push %ds
  push %es
  movl $0x10,%eax # 将 ds、es 段寄存器置为内核数据段
  mov %ax,%ds
  mov %ax,es
  xorl %al,%al  # %eax is scan code  eax 中是扫描码
  inb $0x60,%al # 读取扫描码->al
  cmpb $0xe0,%al  # 该扫描码是 0xe0 吗？ 如果是则跳转到设置 e0 标志代码处
  je set_e0
  cmpb $0xe1,%al  # 扫描码是 0xe1 吗？ 如果是则跳转到设置 e1 标志代码处
  je set_e1
  call key_table(,%eax,4) # 调用键处理程序 ker_table+eax*4(参见下面 523 行)
  movb $0.e0  # 复位 e0 标志
# 下面这段代码(67~77 行)是针对使用 8255A 的 PC 键盘电路进行复位处理。端口 0x61 是 8255A
# 输出口 B 的地址，该输出端口的第 7 位(PB7)用于禁止和允许对键盘数据的处理。这段程序用于对收到的
# 扫描码做出应答。方法是首先禁止键盘，然后立刻重新允许键盘工作
e0_e1:  inb $0x61,%al # 取 PPI 端口 B 状态，其位 7 用于允许/禁止(0/1)键盘
  jmp 1f  # 延迟一会
1:  jmp 1f
1:  orb $0x80,%al # al 位 7 置位(禁止键盘工作)
  jmp 1f  # 再延迟一会
1:  jmp 1f
1:  outb %al,$0x61  # 使 PPI PB7 位置位
  jmp 1f  # 延迟一会
1:  jmp 1f
1:  andb $0x7f,%al  # al 位 7 复位
  outb %al,$0x61  # 使 PPI PB7 位复位(允许键盘工作)
  movb $0x20,%al # 向 8259 中断芯片发送 EOI(中断结束)信号
  outb %al,$0x20
  pushl $0  # 控制台 tty 号=0，作为参数入栈
  call _do_tty_interrupt  # 将收到的数据复制成规范数据并存放在规范字符缓冲队列中
  addl $4,%esp  # 丢弃入栈的参数，弹出保留的寄存器，并中断返回
  pop %es
  pop %ds
  popl %edx
  popl %ecx
  popl %ebx
  popl %eax
  iret
set_e0: movb popl $1,e0 # 收到扫描前导码 0xe0 时，设置 e0 标志位(为 0)
  jmp e0_e1
set_e1: movb $2,e0  # 收到扫描前导码 0xe1 时，设置 e1 标志位(位 1)
  jmp e0_e1

/*
 * This routine fills the buffer with max 8 bytes, taken from
 * %ebx:%eax. (%edx is high). The bytes are written in the
 * order %al,%ah,%eal,%eah,%bl,%bh ... until %eax is zero.
 */ # 下面该子程序把 ebx:eax 中的最多 8 个字符添入缓冲队列中。(edx 是所写入字符的顺序是 al,ah,eah,
    # bl,bh... 直到 eax 等于 0。
put_queue:
	pushl %ecx # 保存 ecx，edx 内容
	pushl %edx # 取控制台 tty 结构中读缓冲队列指针
	movl _table_list,%edx  # read-queue for console
	movl head(%edx),%ecx # 取缓冲队列中头指针->ecx
1:	movb %al,buf(%edx,%ecx)  # 将 al 中的字符放入缓冲队列头指针位指处
	incl %ecx  # 头指针前移 1 字节
	andl $size-1,%ecx  # 以缓冲区大小调整头指针(若超出则返回缓冲区开始指针)
	cmpl tail(%edx),%ecx		# buffer full - discard everything 头指针==尾指针吗(缓冲队列满)？
	je 3f  # 如果已满，则后面未放入的字符全抛弃
	shrdl $8,%ebx,%eax # 将 ebx 中 8 位比特位右移 8 位到 eax 中，但 ebx 不变
	je 2f  # 还有字符吗？若没有(等于 0)则跳转
	shrl $8,%ebx # 将 ebx 中比特位右移 8 位，并跳转到标号 1 继续操作
	jmp 1b
2:	movl %ecx,head(%edx) # 若已将所有字符都放入了队列，则保存头指针
	movl proc_list(%edx),%ecx  # 该队列的等待进程指针？
	testl %ecx,%ecx  # 检测任务结构指针是否为空(有等待该队列的进程吗?)
	je 3f  # 无，则跳转
	movl $0,(%ecx) # 有，则置该进程为可运行就绪状态(唤醒该进程)
3:	popl %edx  # 弹出保留的寄存器并返回
	popl %ecx
	ret
# 下面这段代码根据 ctrl 或 alt 的扫描码，分别设置模式标志中的相应位。如果该扫描码之前收到过
# 0xe0 扫描码(e0 标志置位)，则说明按下的是键盘右边的 ctrl 或 alt 键，则对应设置 ctrl 或 alt
# 在模式标志 mode 中的比特位
ctrl:	movb $0x04,%al  # 0x4 是模式标志 mode 中左 ctrl 键对应的比特位(位 2)
	jmp 1f
alt:	movb $0x10,%al # 0x10 是模式标志 mode 中左 alt 键对应的比特位(位 4)
1:	cmpb $0,e0 # e0 标志置位了吗(按下的是右边的 ctrl 或 alt 键吗？)
	je 2f  # 不是则跳转
	addb %al,%al # 是，则改成置相应邮件的标志位(位 3 或位 5)
2:	orb %al,mode # 设置模式标志 mode 中对应的比特位
	ret
unctrl:	movb $0x04,%al  # 模式标志 mode 中左 ctrl 键对应的比特位(位 2)
	jmp 1f
unalt:	movb $0x10,%al # 0x10 是模式标志 mode 中左 alt 键对应的比特位(位 4)
1:	cmpb $0,e0 # e0 标志置位了吗(释放的是右边的 ctrl 或 alt 键吗？)
	je 2f  # 不是，则跳转
	addb %al,%al # 是，则改成复位相应右键的标志位(位 3 或位 5)
2:	notb %al # 复位模式标志 mode 中对应的比特位。
	andb %al,mode
	ret

lshift:
	orb $0x01,mode # 是左 shift 键按下，设置 mode 中对应的标志位(位 0)
	ret
unlshift:
	andb $0xfe,mode   # 是左 shift 键松开，复位 mode 中对应的标志位(位 0)
	ret
rshift:
	orb $0x02,mode  # 是右 shift 键按下，设置 mode 中对应的标志位(位 1)
	ret
unrshift:
	andb $0xfd,mode  # 是右 shift 键松开，复位 mode 中对应的标志位(位 1)
	ret

caps:	testb $0x80,mode  # 测试模式标志 mode 中位 7 是否已经置位(按下状态)
	jne 1f # 如果已处于按下状态，则返回(ret)
	xorb $4,leds # 翻转 leds 标志中 caps-lock 比特位(位 2)
	xorb $0x40,mode  # 翻转 mode 标志中 caps 键按下的比特位(位 6)
	orb $0x80,mode # 设置 mode 标志中 caps 键已按下标志位(位 7)
set_leds: # 这段代码根据 leds 标志，开启或关闭 LED 指示器。
	call kb_wait # 等待键盘控制器输入缓冲空
	movb $0xed,%al # set leds command  设置 LED 的命令。
	outb %al,$0x60 # 发送键盘命令 0xed 到 0x60 端口
	call kb_wait # 等待键盘控制器缓冲输入空
	movb leds,%al  # 取 leds 标志，作为参数
	outb %al,$0x60 # 发送该参数
	ret
uncaps:	andb $0x7f,mode # caps 键松开，则复位模式标志 mode 中的对应位(位 7)
	ret
scroll:
	xorb $1,leds # scroll 键按下，则翻转 leds 标志中的对应位(位 0)
	jmp set_leds # 根据 leds 标志重新开启或关闭 LED 指示器
num:	xorb $2,leds # num 键按下，则翻转 leds 标志中的对应位(位 1)
	jmp set_leds # 根据 leds 标志重新开启或关闭 LED 指示器


# curosr-key/numeric keypad cursor keys are handled here.
# checking for numeric keypad etc.
# 这里处理方向键/数字小键盘方向键，检测数字小键盘等
cursor:
	subb $0x47,%al # 扫描码是小数字键盘上的键(其扫描码 >= 0x47)发出的？
	jb 1f  # 如果小于则不处理，返回
	cmpb $12,%al # 如果扫描码 > 0x53(0x53-0x47=12)，则
	ja 1f  # 扫描码值超过 83(0x53)，不处理，返回
	jne cur2 # check for ctrl-alt-del  检查是否 ctrl-alt-del
      # 如果等于 12 ，则说明 del 键已被按下，则继续判断 ctrl 和 alt 是否也同时按下
	testb $0x0c,mode # 有 ctrl 键按下吗？
	je cur2  # 无，则跳转
	testb $0x30,mode # 有 alt 键按下吗？
	jne reboot # 有，则跳转到重启处理
cur2:	cmpb $0x01,e0 # e0 forces cursor movement e0 置位表示光标移动  e0 标志置位了吗？
	je cur # 置位了，则跳转光标移动处理处 cur
	testb $0x02,leds # not num-lock forces cursor num-lock 键则不许
      # 测试 leds 中标志 num-lock 键标志是否置位
	je cur # 如果没有置位(num 的 LED 不亮)，则也进行光标移动处理
	testb $0x03,mode # shift forces cursor shift 键也使光标移动
      # 测试模式标志 mode 中 shift 按下标志
	jne cur  # 如果有 shift 键按下，则也进行光标移动处理
	xorl %ebx,%ebx # 否则查询数字表(220 行)，取对应键的数字 ASCII 码
	movb num_table(%eax),%al # 以 eax 作为索引值，取对应数字字符->al
	jmp put_queue  # 将该字符放入缓冲队列中
1:	ret
# 这段代码处理光标的移动
cur:	movb cur_table(%eax),%al # 取光标字符标中相应键的代表字符->al
	cmpb $'9,%al # 若该字符 <= ‘9’，说明是上一页、下一页、插入或删除键
	ja ok_cur  # 则功能字符序列中要添入字符 '~'
	movb $'~,%ah
ok_cur:	shll $16,%eax # 将 ax 中内容移动到 eax 高字中
	movw $0x5b1b,%ax # 在 ax 中放入 'esc['字符，与 eax 高字中字符组成移动序列
	xorl %ebx,%ebx
	jmp put_queue  # 将该字符放入缓冲队列中

#if defined(KBD_FR)
num_table:
	.ascii "789 456 1230." # 数字小键盘上键对应的数字 ASCII 码表
#else
num_table:
	.ascii "789 456 1230,"
#endif
cur_table:
	.ascii "HA5 DGC YB623" # 数字小键盘上方向键或插入删除键对应的移动表示字符表

# this routine handles function keys
func: # 下面子程序处理功能键
	pushl %eax
	pushl %ecx
	pushl %edx
  call _show_stat # 调用显示各任务状态函数(kernel/sched.c 33)
  popl %edx
  popl %ecx
  popl %eax
  subb $0x3B,%al  # 功能键 'F1' 的扫描码是 0x3B，因此此时 al 中是功能键索引号。
  jb end_func # 如果扫描码小于 0x3b，则不处理，返回
  cmpb $9,%al # 功能键是 F1-F10？
  jbe ok_func # 是，则跳转
  subb $18,%al  # 是功能键 F11、F12 吗？
  cmpb $10,%al  # 是功能键 F11？
  jb end_func # 不是，则不处理，返回
  cmpb $11,%al  # 是功能键 F12？
  ja end_func # 不是，则不处理，返回
ok_func:
	cmpl $4,%ecx # check that there is enough room  检查是否有足够空间
	jl end_func  # 需要放入 4 各字符序列，如果放不下，则返回
	movl func_table(,%eax,4),%eax  # 取功能键对应字符序列
	xorl %ebx,%ebx
	jmp put_queue  # 放入缓冲队列中
end_func:
	ret

# function keys send F1:'esc [ [ A' F2:'esc [ [ B' etc.
# 功能键发送的扫描码，F1 键为：'esc [ [ A' F2 键为：'esc [ [ B' 等
func_table:
	.long 0x415b5b1b,0x425b5b1b,0x435b5b1b,0x445b5b1b
	.long 0x455b5b1b,0x465b5b1b,0x475b5b1b,0x485b5b1b
	.long 0x495b5b1b,0x4a5b5b1b,0x4b5b5b1b,0x4c5b5b1b
# 扫描码-ASCII 字符映射表。根据在 config.h 中定义的键盘类型(FINISH,US,GERMEN,FRANCH),
# 将相应键的扫描码映射到 ASCII 字符。
#if	defined(KBD_FINNISH)
key_map:  # 以下是芬兰语键盘的扫描码映射表
	.byte 0,27 # 扫描码 0x00,0x01 对应的 ASCII 码
	.ascii "1234567890+'"  # 扫描码 0x02,......0x0c,0x0d 对应的 ASCII 码，以下类似
	.byte 127,9
	.ascii "qwertyuiop}"
	.byte 0,13,0
	.ascii "asdfghjkl|{"
	.byte 0,0
	.ascii "'zxcvbnm,.-"
	.byte 0,'*,0,32  # 36-39 扫描码 0x36-0x39 对应的 ASCII 码
	.fill 16,1,0 # 3A-49  扫描码 0x3A-0x49 对应的 ASCII 码
	.byte '-,0,0,0,'+  # 4A-4E 扫描码 0x4A-0x4E 对应的 ASCII 码
	.byte 0,0,0,0,0,0,0  # 4F-55 扫描码 0x4F-0x55 对应的 ASCII 码
	.byte '<
	.fill 10,1,0

shift_map:  # shift 键同时按下时的映射表
	.byte 0,27
	.ascii "!\"#$%&/()=?`"
	.byte 127,9
	.ascii "QWERTYUIOP]^"
	.byte 13,0
	.ascii "ASDFGHJKL\\["
	.byte 0,0
	.ascii "*ZXCVBNM;:_"
	.byte 0,'*,0,32  # 36-39
	.fill 16,1,0 # 3A-49
	.byte '-,0,0,0,'+  # 4A-4E
	.byte 0,0,0,0,0,0,0  # 4F-55
	.byte '>
	.fill 10,1,0

alt_map:  # alt 键同时按下时的映射表
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
  .byte 0,0,0,0		/* 36-39 */
  .fill 16,1,0		/* 3A-49 */
  .byte 0,0,0,0,0		/* 4A-4E */
  .byte 0,0,0,0,0,0,0	/* 4F-55 */
  .byte '|
  .fill 10,1,0

#elif defined(KBD_US)

key_map:  # 以下是美式键盘的扫描码映射表.
	.byte 0,27
	.ascii "1234567890-="
	.byte 127,9
	.ascii "qwertyuiop[]"
	.byte 13,0
	.ascii "asdfghjkl;'"
	.byte '`,0
	.ascii "\\zxcvbnm,./"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0


shift_map:
	.byte 0,27
	.ascii "!@#$%^&*()_+"
	.byte 127,9
	.ascii "QWERTYUIOP{}"
	.byte 13,0
	.ascii "ASDFGHJKL:\""
	.byte '~,0
	.ascii "|ZXCVBNM<>?"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

#elif defined(KBD_GR)

key_map:  # 以下是德语键盘的扫描码映射表.
	.byte 0,27
	.ascii "1234567890\\'"
	.byte 127,9
	.ascii "qwertzuiop@+"
	.byte 13,0
	.ascii "asdfghjkl[]^"
	.byte 0,'#
	.ascii "yxcvbnm,.-"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0


shift_map:
	.byte 0,27
	.ascii "!\"#$%&/()=?`"
	.byte 127,9
	.ascii "QWERTZUIOP\\*"
	.byte 13,0
	.ascii "ASDFGHJKL{}~"
	.byte 0,''
	.ascii "YXCVBNM;:_"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0@\0$\0\0{[]}\\\0"
	.byte 0,0
	.byte '@,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0


#elif defined(KBD_FR)

key_map:  # 以下是法语键盘的扫描码映射表.
	.byte 0,27
	.ascii "&{\"'(-}_/@)="
	.byte 127,9
	.ascii "azertyuiop^$"
	.byte 13,0
	.ascii "qsdfghjklm|"
	.byte '`,0,42		/* coin sup gauche, don't know, [*|mu] */
	.ascii "wxcvbn,;:!"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '<
	.fill 10,1,0

shift_map:
	.byte 0,27
	.ascii "1234567890]+"
	.byte 127,9
	.ascii "AZERTYUIOP<>"
	.byte 13,0
	.ascii "QSDFGHJKLM%"
	.byte '~,0,'#
	.ascii "WXCVBN?./\\"
	.byte 0,'*,0,32		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte '-,0,0,0,'+	/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '>
	.fill 10,1,0

alt_map:
	.byte 0,0
	.ascii "\0~#{[|`\\^@]}"
	.byte 0,0
	.byte '@,0,0,0,0,0,0,0,0,0,0
	.byte '~,13,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0
	.byte 0,0,0,0,0,0,0,0,0,0,0
	.byte 0,0,0,0		/* 36-39 */
	.fill 16,1,0		/* 3A-49 */
	.byte 0,0,0,0,0		/* 4A-4E */
	.byte 0,0,0,0,0,0,0	/* 4F-55 */
	.byte '|
	.fill 10,1,0

#else
#error "KBD-type not defined"
#endif
# do_self handles "normal" keys, ie keys that don't change meaning
# and which have just one character returns.
# do_self 用于处理‘普通’键，也即含义没有变化并且只有一个字符返回的键
do_self:  # 469-475 行用于根据模式标志 mode 选择 alt_map、shift_map 或 key_map 映射表之一
	lea alt_map,%ebx # alt 键同时按下是的映射表基址 alt_map->ebx
	testb $0x20,mode # alt-gr  右 alt 键同时按下了？
	jne 1f # 是，则向前跳转到标号 1 处
	lea shift_map,%ebx # shift 键同时按下时的映射表基址 shift_map->ebx
	testb $0x03,mode # 有 shift 键同时按下了吗？
	jne 1f # 有，则向前跳转到标号 1 处
	lea key_map,%ebx # 否则使用普通映射表 key_map
# 取映射表中对应扫描码的 ASCII 字符，若没有对应字符，则返回(转 none)
1:	movb (%ebx,%eax),%al # 将扫描码作为索引值，取对应的 ASCII 码->al
	orb %al,%al  # 检测看是否有对应的 ASCII 码
	je none  # 若没有(对应的 ASCII 码=0)，则返回
# 若 ctrl 键已按下或 caps 键锁定，并且字符在 'a'-'}'(0x61-0x7D)范围内，则将其转换成大写字符
# (0x41-0x5D)
	testb $0x4c,mode # ctrl or caps  控制键已按下或 caps 灯亮？
	je 2f  # 没有，则向前跳转标号 2 处
	cmpb $'a,%al # 将 al 中的字符与 'a' 比较
	jb 2f  # 若 al 值 <'a',则转标号 2 处
	cmpb $'},%al # 将 al 中的字符与 '}' 比较
	ja 2f  # 若 al 值 >'}',则转标号 2 处
	subb $32,%al # 将 al 转换为大写字符(减 0x20)
# 若 ctrl 键已按下，并且字符在 '`'--'_'(0x40-0x5F)之间(是大写字符)，则将其装换为控制字符
# (0x00-0x1F)
2:	testb $0x0c,mode # ctrl  ctrl 键同时按下了吗？
	je 3f  # 若没有则转标号 3
	cmpb $64,%al # 将 al 与 ‘@’(64)字符比较(即判断字符所属范围)
	jb 3f  # 若值 <'@',则转标号 3
	cmpb $64+32,%al  # 将 al 与 ‘``’(96)字符比较(即判断字符所属范围)
	jae 3f # 若值 <'``',则转标号 3
	subb $64,%al # 否则 al 值减 0x40，即将字符转换为 0x00-0x1f 之间的控制字符
3:	testb $0x10,mode # left alt  左 alt 键同时按下？
	je 4f  # 没有，则转标号 4
	orb $0x80,%al  # 字符的位 7 置位
# 将 al 中的字符放入读缓冲队列中
4:	andl $0xff,%eax  # 清 eax 的高字和 ah
	xorl %ebx,%ebx # 清 ebx
	call put_queue # 将字符放入缓冲队列中
none:	ret

/*
 * minus has a routine of it's own, as a 'E0h' before
 * the scan code for minus means that the numeric keypad
 * slash was pushed.
 */ # 减号有它自己的处理程序，因为减号扫描码前的 0xe0 意味着按下了小数字键盘上的斜杠键
minus:	cmpb $1,e0 # e0 标志置位了吗？
	jne do_self  # 没有，则调用 do_self 对减号符进行普通处理
	movl $'/,%eax  # 否则用 '/' 替换减号 '-'->al
	xorl %ebx,%ebx
	jmp put_queue  # 并将字符放入缓冲队列中

/*
 * This table decides which routine to call when a scan-code has been
 * gotten. Most routines just call do_self, or none, depending if
 * they are make or break.
 */ # 下面是一张子程序是 do_self，或者是 none,这取决于是按键(make)还是释放键(break)
key_table:
	.long none,do_self,do_self,do_self # 00-03 s0 esc 1 2
	.long do_self,do_self,do_self,do_self  # 04-07 3 4 5 6
	.long do_self,do_self,do_self,do_self  # 08-0B 7 8 9 0
	.long do_self,do_self,do_self,do_self  # 0C-0F + ' bs tab
	.long do_self,do_self,do_self,do_self	 # 10-13 q w e r
	.long do_self,do_self,do_self,do_self	 # 14-17 t y u i
	.long do_self,do_self,do_self,do_self  # 18-1B o p } ^
	.long do_self,ctrl,do_self,do_self  # 1C-1F enter ctrl a s
	.long do_self,do_self,do_self,do_self  # 20-23 d f g h
	.long do_self,do_self,do_self,do_self  # 24-27 j k l |
	.long do_self,do_self,lshift,do_self  # 28-2B { para lshift ,
	.long do_self,do_self,do_self,do_self  # 2C-2F z x c v
	.long do_self,do_self,do_self,do_self  # 30-33 b n m ,
	.long do_self,minus,rshift,do_self  # 34-37 . - rshift *
	.long alt,do_self,caps,func  # 38-3B alt sp caps f1
	.long func,func,func,func  # 3C-3F f2 f3 f4 f5
	.long func,func,func,func  # 40-43 f6 f7 f8 f9
	.long func,num,scroll,cursor  # 44-47 f10 num scr home
	.long cursor,cursor,do_self,cursor  # 48-4B up pgup - left
	.long cursor,cursor,do_self,cursor  # 4C-4F n5 right + end
	.long cursor,cursor,cursor,cursor  # 50-53 dn pgdn ins del
	.long none,none,do_self,func  # 54-57 sysreq ? < f11
	.long func,none,none,none  # 58-5B f12 ? ? ?
	.long none,none,none,none  # 5C-5F ? ? ? ?
	.long none,none,none,none  # 60-63 ? ? ? ?
	.long none,none,none,none  # 64-67 ? ? ? ?
	.long none,none,none,none  # 68-6B ? ? ? ?
	.long none,none,none,none  # 6C-6F ? ? ? ?
	.long none,none,none,none  # 70-73 ? ? ? ?
	.long none,none,none,none  # 74-77 ? ? ? ?
	.long none,none,none,none  # 78-7B ? ? ? ?
	.long none,none,none,none  # 7C-7F ? ? ? ?
	.long none,none,none,none  # 80-83 ? br br br
	.long none,none,none,none  # 84-87 br br br br
	.long none,none,none,none  # 88-8B br br br br
	.long none,none,none,none  # 8C-8F br br br br
	.long none,none,none,none  # 90-93 br br br br
	.long none,none,none,none  # 94-97 br br br br
	.long none,none,none,none  # 98-9B br br br br
	.long none,unctrl,none,none  # 9C-9F br unctrl br br
	.long none,none,none,none  # A0-A3 br br br br
	.long none,none,none,none  # A4-A7 br br br br
	.long none,none,unlshift,none  # A8-AB br br unlshift br
	.long none,none,none,none  # AC-AF br br br br
	.long none,none,none,none  # B0-B3 br br br br
	.long none,none,unrshift,none  # B4-B7 br br unrshift br
	.long unalt,none,uncaps,none  # B8-BB unalt br uncaps br
	.long none,none,none,none  # BC-BF br br br br
	.long none,none,none,none  # C0-C3 br br br br
	.long none,none,none,none  # C4-C7 br br br br
	.long none,none,none,none  # C8-CB br br br br
	.long none,none,none,none  # CC-CF br br br br
	.long none,none,none,none  # D0-D3 br br br br
	.long none,none,none,none  # D4-D7 br br br br
	.long none,none,none,none  # D8-DB br ? ? ?
	.long none,none,none,none  # DC-DF ? ? ? ?
	.long none,none,none,none  # E0-E3 e0 e1 ? ?
	.long none,none,none,none  # E4-E7 ? ? ? ?
	.long none,none,none,none  #  E8-EB ? ? ? ?
	.long none,none,none,none  # EC-EF ? ? ? ?
	.long none,none,none,none  # F0-F3 ? ? ? ?
	.long none,none,none,none  # F4-F7 ? ? ? ?
	.long none,none,none,none  # F8-FB ? ? ? ?
	.long none,none,none,none  # FC-FF ? ? ? ?

# kb_wait waits for the keyboard controller buffer to empty.
# there is no timeout - if the buffer doesn't empty, we hang.
# 子程序 kb_wait 用于等待键盘控制器缓冲空。不存在超时处理，如果缓冲永远不为空的话，程序就会永远
# 等待(死掉)
kb_wait:
	pushl %eax
1:	inb $0x64,%al  # 读键盘控制器状态
	testb $0x02,%al  # 测试输入缓冲期是否为空(等于 0)
	jne 1b # 若不空，则跳转循环等待
	popl %eax
	ret

# This routine reboots the machine by asking the keyboard
# controller to pulse the reset-line low.
# 该子程序通过设置键盘控制器，向复位线输出负脉冲，使系统复位重启(reboot)
reboot:
	call kb_wait # 首先等待键盘控制器输入缓冲器空
	movw $0x1234,0x472 # don't do memory check
	movb $0xfc,%al # pulse reset and A20 low
	outb %al,$0x64 # 向系统复位和 A20 线输出负脉冲
die:	jmp die  # 死机
