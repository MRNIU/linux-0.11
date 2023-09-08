
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# CMakeLists.txt for MRNIU/linux-0.11.

# 设置项目名与版本
project(
        src
)

set(BLK_DRV_SRC
        kernel/blk_drv/ll_rw_blk.c
        kernel/blk_drv/floppy.c
        kernel/blk_drv/hd.c
        kernel/blk_drv/ramdisk.c
)
add_library(blk_drv STATIC
        ${BLK_DRV_SRC}
)


set(CHR_DRV_SRC  
        kernel/chr_drv/tty_io.c
        kernel/chr_drv/console.c
        kernel/chr_drv/keyboard.s 
        kernel/chr_drv/serial.c
        kernel/chr_drv/rs_io.s
        kernel/chr_drv/tty_ioctl.s
)
add_library(chr_drv STATIC
        ${CHR_DRV_SRC}
)

set(KERNEL_SRC  
        kernel/sched.c
        kernel/system_call.s
        kernel/traps.s
        kernel/asm.s
        kernel/fork.c
        kernel/panic.c
        kernel/printk.c
        kernel/vsprintf.c
        kernel/sys.c
        kernel/exit.c 
        kernel/signal.c
        kernel/mktime.c
)
add_library(kernel STATIC
        ${KERNEL_SRC}
)

set(MM_SRC	
        mm/memory.c
        mm/page.s
)
add_library(mm STATIC
        ${MM_SRC}
)

set(FS_SRC
        fs/open.c
        fs/read_write.c
        fs/inode.c
        fs/file_table.c
        fs/buffer.c
        fs/super.c
        fs/block_dev.c
        fs/char_dev.c
        fs/file_dev.c
        fs/stat.c
        fs/exec.c
        fs/pipe.c
        fs/namei.c
        fs/bitmap.c
        fs/fcntl.c
        fs/ioctl.c
        fs/truncate.c
)
add_library(fs STATIC
        ${FS_SRC}
)

set(LIB_SRC  
        lib/ctype.c
        lib/_exit.c
        lib/open.c
        lib/close.c
        lib/errno.c
        lib/write.c
        lib/dup.c
        lib/setsid.c
        lib/execve.c
        lib/wait.c
        lib/string.c
        lib/malloc.c
)
add_library(lib STATIC
        ${LIB_SRC}
)

# TODO: need to document why the size need plus 3*512
KERNEL_SIZE = $(stat -t "%s" ../kernel.bin | tr -d "\n")
   SYS_SIZE = $(echo '($(KERNEL_SIZE) + 15 + 3*512)/16' | bc)


target_compile_options(boot PRIVATE
        ${COMMON_COMPILE_OPTIONS}
        -DSYS_SIZE=$(SYS_SIZE)
)

target_link_options(boot PRIVATE
        ${COMMON_LINK_OPTIONS}
        -Ttext 0 -e _start
)
