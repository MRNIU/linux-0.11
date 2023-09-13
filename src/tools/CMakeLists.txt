
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# CMakeLists.txt for MRNIU/linux-0.11.

# 设置项目名与版本
project(
        tools
)

enable_language(C)

add_executable(build
        build.c
)

include_directories(../include)
