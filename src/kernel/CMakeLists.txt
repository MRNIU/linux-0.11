
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# CMakeLists.txt for MRNIU/linux-0.11.

# 设置项目名与版本
project(
        kernel
)

enable_language(C ASM)

add_library(kernel OBJECT
        math/math_emulate.c
        blk_drv/ll_rw_blk.c
        blk_drv/floppy.c
        blk_drv/hd.c
        blk_drv/ramdisk.c
        chr_drv/tty_io.c
        chr_drv/console.c
        chr_drv/keyboard.S
        chr_drv/serial.c
        chr_drv/rs_io.s
        chr_drv/tty_ioctl.c
        sched.c
        system_call.s
        traps.c
        asm.s
        fork.c
        panic.c
        printk.c
        vsprintf.c
        sys.c
        exit.c 
        signal.c
        mktime.c
        chr_drv
        blk_drv
        math
)

include_directories(../include)

target_compile_options(kernel PRIVATE 
        ${COMMON_COMPILE_OPTIONS}
)

target_link_libraries(kernel PRIVATE
        ${COMMON_LINK_OPTIONS}
)
