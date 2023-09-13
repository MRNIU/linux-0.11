
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# CMakeLists.txt for MRNIU/linux-0.11.

# 设置项目名与版本
project(
        fs
)

enable_language(C)

add_library(fs OBJECT
        open.c
        read_write.c
        inode.c
        file_table.c
        buffer.c
        super.c
        block_dev.c
        char_dev.c
        file_dev.c
        stat.c
        exec.c
        pipe.c
        namei.c
        bitmap.c
        fcntl.c
        ioctl.c
        truncate.c
)

include_directories(../include)

target_compile_options(fs PRIVATE 
        ${COMMON_COMPILE_OPTIONS}
)

target_link_libraries(fs PRIVATE
        ${COMMON_LINK_OPTIONS}
)
