
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# compile_config.cmake for MRNIU/linux-0.11.
# 配置信息

# 通用编译选项
list(APPEND COMMON_COMPILE_OPTIONS
        # 如果 CMAKE_BUILD_TYPE 为 Release 则使用 -O3 -Werror，否则使用 -O0 -g -ggdb
        $<$<CONFIG:Release>:-O3;-Werror>
        $<$<CONFIG:Debug>:-O0;-g;-ggdb>
        # 打开全部警告
        -Wall
        # 打开额外警告
        -Wextra
        -fomit-frame-pointer
        -fno-builtin
        -march=i386
)

# 通用链接选项
list(APPEND COMMON_LINK_OPTIONS
        -m i386pe
        -Ttext 0
        -e startup_32
        -s
        -M
        --image-base 0x0000
)
