
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# CMakeLists.txt for MRNIU/linux-0.11.

# 设置项目名与版本
project(
        boot
)

enable_language(C ASM)

add_library(head OBJECT
        head.s
)

add_library(setup.bin OBJECT
        setup.s
)

add_library(bootsect.bin OBJECT
        bootsect.s
)

include_directories(../include)

# target_compile_options(system PRIVATE 
#         ${COMMON_COMPILE_OPTIONS}
# )

# target_link_libraries(system PRIVATE
#         ${COMMON_LINK_OPTIONS}
# )
