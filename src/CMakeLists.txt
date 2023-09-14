
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# CMakeLists.txt for MRNIU/linux-0.11.

# 设置项目名与版本
project(
        src
)

add_subdirectory(kernel)
add_subdirectory(mm)
add_subdirectory(fs)
add_subdirectory(lib)
add_subdirectory(boot)
add_subdirectory(init)
add_subdirectory(tools)

add_executable(system
        kernel
        mm
        fs
        lib
)

target_compile_options(system PRIVATE 
        # ${COMMON_COMPILE_OPTIONS}
        # -freestanding
)

target_link_libraries(system PRIVATE
        # ${COMMON_LINK_OPTIONS}
        -ffreestanding
)

set_target_properties(system PROPERTIES LINKER_LANGUAGE C)

# add_executable(build
#         tools/build.c
# )
