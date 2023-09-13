
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# CMakeLists.txt for MRNIU/linux-0.11.

# 设置项目名与版本
project(
        init
)

enable_language(C)

add_library(main OBJECT
        main.c
)

include_directories(../include)

target_compile_options(main PRIVATE 
        ${COMMON_COMPILE_OPTIONS}
)

target_link_libraries(main PRIVATE
        ${COMMON_LINK_OPTIONS}
)
