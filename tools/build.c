/*
 * This file builds a disk-image from three different files:
 *
 * - bootsect: max 510 bytes of 8086 machine code, loads the rest
 * - setup: max 4 sectors of 8086 machine code, sets up system parm
 * - system: 80386 code for actual system
 *
 * It does some checking that all files are of the correct type, and
 * just writes the result to stdout, removing headers and padding to
 * the right amount. It also writes some system data to stderr.
 */
/* 该程序从三个不同的程序中创建磁盘映像文件：
 * -bootsect: 该文件的 8086 机器码最长为 510 字节，用于加载其它程序。
 * -setup: 该文件的 8086 机器码最长为 4 个磁盘扇区，用于设置系统参数。
 * -system: 实际系统的 80386 代码。
 * 该程序首先检查所有程序模块的类型是否正确，并将检查结果在终端上显示出来，
 * 然后删除模块头部并扩充大正确的长度。该程序也会将一些系统数据写到 stderr.
 */


// Changes by tytso to allow root device specification
// Tytso 对程序做了修改，以允许指定根文件设备。

#include <stdio.h>	//fprintf 使用其中的 fprintf()
#include <string.h>	// 字符串操作
#include <stdlib.h>	// contains exit 含有 exit()
#include <sys/types.h>	// unistd.h needs this	供 unistd.h 使用
#include <sys/stat.h>	// 文件状态信息结构
#include <linux/fs.h>	// 文件系统
#include <unistd.h>	// contains read/write 含有 read()/write()
#include <fcntl.h>	// 文件操作模式符号常数

#define MINIX_HEADER 32	// MINIX 二进制模块头部长度为 32B。
#define GCC_HEADER 1024	// GCC 头部信息长度为 1024N

#define SYS_SIZE 0x2000	// ststem 文件最长字节数(字节数为 SYS_SIZE*16 = 128KB)

#define DEFAULT_MAJOR_ROOT 3	// 默认根设备主设备号 -3(硬盘)
#define DEFAULT_MINOR_ROOT 6	// 默认根设备次设备号 -6(第 2 个硬盘的第 1 分区)

// 下面指定 setup 模块占的最大扇区数：不要改变该值，除非也改变 bootsect 等相应文件
/* max nr of sectors of setup: don't change unless you also change
 * bootsect etc */
#define SETUP_SECTS 4	// setup 最大长度为 4 个扇区(4*512B)

#define STRINGIFY(x) #x	//用于出错时显示语句中表示扇区数

void die(char * str){		// 显示出错信息，并终止程序
	fprintf(stderr,"%s\n",str);
	exit(1);
}

void usage(void){	// 显示程序使用方法，并退出
	die("Usage: build bootsect setup system [rootdev] [> image]");
}

int main(int argc, char ** argv){
	int i,c,id;
	char buf[1024];
	char major_root, minor_root;
	struct stat sb;

	if ((argc != 4) && (argc != 5))	// 若命令行参数不是 4 个或 5 个，则显示用法并退出
		usage();
	if (argc == 5) {	// 如果参数是 5 个，则说明带有根设备名
		// 如果根设备名是软盘("FLOPPY"),则取该设备文件的状态信息，若出错则显示信息，退出
		if (strcmp(argv[4], "FLOPPY")) {
			if (stat(argv[4], &sb)) {
				perror(argv[4]);
				die("Couldn't stat root device.");
			}
			// 若成功则取该设备名状态结构中的主设备号和次设备号，否则让主设备号和次设备号取 0
			major_root = MAJOR(sb.st_rdev);
			minor_root = MINOR(sb.st_rdev);
		} else {
			major_root = 0;
			minor_root = 0;
		}
		// 若参数只有 4 个，则让主设备号和次设备号等于系统默认的根设备
	} else {
		major_root = DEFAULT_MAJOR_ROOT;
		minor_root = DEFAULT_MINOR_ROOT;
	}
	// 在标准错误终端上显示所选择的根设备主、次设备号
	fprintf(stderr, "Root device is (%d, %d)\n", major_root, minor_root);
	// 如果主设备号不等于 2(软盘)或 3(硬盘)，也不等于 0(取系统默认根设备)，则显示出错信息，退出
	if ((major_root != 2) && (major_root != 3) &&
	    (major_root != 0)) {
		fprintf(stderr, "Illegal root device (major = %d)\n",
			major_root);
		die("Bad root device --- major #");
	}
	for (i=0;i<sizeof buf; i++) buf[i]=0;	// 初始化缓冲区，全置 0.
	// 一制度方式打开参数 1 指定的文件(bootsect),若出错则显示出错信息，退出
	if ((id=open(argv[1],O_RDONLY,0))<0)
		die("Unable to open 'boot'");
	// 读取文件中的 MINIX 执行头部信息(参见列表后说明)，若出错则显示出错信息，退出
	if (read(id,buf,MINIX_HEADER) != MINIX_HEADER)
		die("Unable to read header of 'boot'");
	// 0x0301-MINIX 头部 a_magic 魔数
	if (((long *) buf)[0]!=0x04100301)
		die("Non-Minix header of 'boot'");
	if (((long *) buf)[1]!=MINIX_HEADER)
		die("Non-Minix header of 'boot'");
	if (((long *) buf)[3]!=0)
		die("Illegal data segment in 'boot'");
	if (((long *) buf)[4]!=0)
		die("Illegal bss in 'boot'");
	if (((long *) buf)[5] != 0)
		die("Non-Minix header of 'boot'");
	if (((long *) buf)[7] != 0)
		die("Illegal symbol table in 'boot'");
	i=read(id,buf,sizeof buf);
	fprintf(stderr,"Boot sector %d bytes.\n",i);
	if (i != 512)
		die("Boot block must be exactly 512 bytes");
	if ((*(unsigned short *)(buf+510)) != 0xAA55)
		die("Boot block hasn't got boot flag (0xAA55)");
	buf[508] = (char) minor_root;
	buf[509] = (char) major_root;
	i=write(1,buf,512);
	if (i!=512)
		die("Write call failed");
	close (id);

	if ((id=open(argv[2],O_RDONLY,0))<0)
		die("Unable to open 'setup'");
	if (read(id,buf,MINIX_HEADER) != MINIX_HEADER)
		die("Unable to read header of 'setup'");
	if (((long *) buf)[0]!=0x04100301)
		die("Non-Minix header of 'setup'");
	if (((long *) buf)[1]!=MINIX_HEADER)
		die("Non-Minix header of 'setup'");
	if (((long *) buf)[3]!=0)
		die("Illegal data segment in 'setup'");
	if (((long *) buf)[4]!=0)
		die("Illegal bss in 'setup'");
	if (((long *) buf)[5] != 0)
		die("Non-Minix header of 'setup'");
	if (((long *) buf)[7] != 0)
		die("Illegal symbol table in 'setup'");
	for (i=0 ; (c=read(id,buf,sizeof buf))>0 ; i+=c )
		if (write(1,buf,c)!=c)
			die("Write call failed");
	close (id);
	if (i > SETUP_SECTS*512)
		die("Setup exceeds " STRINGIFY(SETUP_SECTS)
			" sectors - rewrite build/boot/setup");
	fprintf(stderr,"Setup is %d bytes.\n",i);
	for (c=0 ; c<sizeof(buf) ; c++)
		buf[c] = '\0';
	while (i<SETUP_SECTS*512) {
		c = SETUP_SECTS*512-i;
		if (c > sizeof(buf))
			c = sizeof(buf);
		if (write(1,buf,c) != c)
			die("Write call failed");
		i += c;
	}

	if ((id=open(argv[3],O_RDONLY,0))<0)
		die("Unable to open 'system'");
//	if (read(id,buf,GCC_HEADER) != GCC_HEADER)
//		die("Unable to read header of 'system'");
//	if (((long *) buf)[5] != 0)
//		die("Non-GCC header of 'system'");
	for (i=0 ; (c=read(id,buf,sizeof buf))>0 ; i+=c )
		if (write(1,buf,c)!=c)
			die("Write call failed");
	close(id);
	fprintf(stderr,"System is %d bytes.\n",i);
	if (i > SYS_SIZE*16)
		die("System is too big");
	return(0);
}
