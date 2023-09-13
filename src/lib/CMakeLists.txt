
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# CMakeLists.txt for MRNIU/linux-0.11.

# 设置项目名与版本
project(
        lib
)

enable_language(C)

add_library(lib OBJECT
        ctype.c
        _exit.c
        open.c
        close.c
        errno.c
        write.c
        dup.c
        setsid.c
        execve.c
        wait.c
        string.c
        malloc.c
)

include_directories(../include)

target_compile_options(lib PRIVATE 
        ${COMMON_COMPILE_OPTIONS}
)

target_link_libraries(lib PRIVATE
        ${COMMON_LINK_OPTIONS}
)
