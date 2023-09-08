
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# CMakeLists.txt for MRNIU/linux-0.11.

# 设置最小 cmake 版本
cmake_minimum_required(VERSION 3.27 FATAL_ERROR)

# 设置项目名与版本
project(
        linux
        VERSION 0.11
)

# 禁止原地编译
if (${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})
    # 如果你看到这句话，cmake 此时已经在根目录下生成了一些临时文件，你需要删除它们
    # CMakeFiles, CMakeCache.txt
    message(
            FATAL_ERROR
            "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
endif ()

# 设置辅助 cmake 脚本路径
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# 导入函数
include(functions)

# 导入第三方依赖
include(3rd)

# 导入编译配置
include(compile_config)

# 添加要编译的目录
add_subdirectory(${PROJECT_SOURCE_DIR}/src)
add_subdirectory(${PROJECT_SOURCE_DIR}/doc)

# if (TARGET_ARCH STREQUAL "x86_64" OR TARGET_ARCH STREQUAL "aarch64")
#     # 创建 image 目录并将文件复制
#     make_uefi_dir(${PROJECT_BINARY_DIR}/src/boot/boot.efi $<TARGET_FILE:kernel> ${PROJECT_SOURCE_DIR}/tools/startup.nsh)
# endif ()

# qemu 参数设置
list(APPEND QEMU_FLAGS
        # 使用标准输出显示
        -serial stdio
        # 启动 telnet 服务，使用 2333 端口，不等待连接
        -monitor ${QEMU_MONITOR_ARG}
        -m 128M
        -net none
        -bios ${ovmf_BINARY_DIR}/OVMF_${TARGET_ARCH}.fd
        -hda fat:rw:${CMAKE_BINARY_DIR}/image/
)

# 运行 qemu
add_custom_target(run DEPENDS ${RUN_DEPENDS}
        COMMENT "Run Kernel in qemu ..."
        COMMAND ${CMAKE_COMMAND} -E echo ${QEMU_FLAGS}
        COMMAND qemu-system-${TARGET_ARCH} ${QEMU_FLAGS}
)

# 在 qemu 中调试
add_custom_target(debug DEPENDS ${DEBUG_DEPENDS}
        COMMENT "Debug Kernel in qemu ..."
        COMMAND ${CMAKE_COMMAND} -E echo ${QEMU_FLAGS}
        COMMAND qemu-system-${TARGET_ARCH}
        ${QEMU_FLAGS}
        # 等待 gdb 连接
        -S
        # 使用 1234 端口
        -gdb ${QEMU_GDB_PORT}
)
