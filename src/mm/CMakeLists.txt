
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# CMakeLists.txt for MRNIU/linux-0.11.

# 设置项目名与版本
project(
        mm
)

enable_language(C ASM)

add_library(mm OBJECT
        memory.c
        page.s
)

include_directories(../include)

target_compile_options(mm PRIVATE 
        ${COMMON_COMPILE_OPTIONS}
)

target_link_libraries(mm PRIVATE
        ${COMMON_LINK_OPTIONS}
)
