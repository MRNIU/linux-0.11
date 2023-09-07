
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
#
# functions.cmake for MRNIU/linux-0.11.
# 辅助函数

# 生成 target 输出文件的 readelf -a
# _target: target 名
# 在 ${${_target}_BINARY_DIR} 目录下生成 $<TARGET_FILE:${_target}>.readelf 文件
function(readelf_a _target)
    add_custom_command(TARGET ${_target}
            COMMENT "readelf -a $<TARGET_FILE:${_target}> ..."
            POST_BUILD
            DEPENDS ${_target}
            WORKING_DIRECTORY ${${_target}_BINARY_DIR}
            COMMAND ${CMAKE_READELF} -a $<TARGET_FILE:${_target}> > $<TARGET_FILE:${_target}>.readelf || (exit 0)
    )
endfunction()

# 生成 target 输出文件的 objdump -D
# _target: target 名
# 在 ${${_target}_BINARY_DIR} 目录下生成 $<TARGET_FILE:${_target}>.disassembly 文件
function(objdump_D _target)
    add_custom_command(TARGET ${_target}
            COMMENT "objdump -D $<TARGET_FILE:${_target}> ..."
            POST_BUILD
            DEPENDS ${_target}
            WORKING_DIRECTORY ${${_target}_BINARY_DIR}
            COMMAND ${CMAKE_OBJDUMP} -D $<TARGET_FILE:${_target}> > $<TARGET_FILE:${_target}>.disassembly
    )
endfunction()

# 将 elf 转换为 efi
# _elf: 要转换的 target 名
# _efi: 输出的 efi 文件名
# 在 ${${_target}_BINARY_DIR} 目录下生成 ${_efi} 文件
function(elf2efi _target _efi)
    add_custom_command(TARGET ${_target}
            COMMENT "Convert $<TARGET_FILE:${_target}> to efi ..."
            POST_BUILD
            DEPENDS ${_target}
            WORKING_DIRECTORY ${${_target}_BINARY_DIR}
            COMMAND ${CMAKE_OBJCOPY} $<TARGET_FILE:${_target}> ${_efi}
            -g -R .comment -R .gnu_debuglink -R .note.gnu.build-id
            -R .gnu.hash -R .plt -R .rela.plt -R .dynstr -R .dynsym -R .rela.dyn
            -S -R .eh_frame -R .gcc_except_table -R .hash
            --target=efi-app-${TARGET_ARCH} --subsystem=10
    )
endfunction()

# 创建 image 目录并将文件复制
# _boot: boot efi 文件
# _kernel: kernel elf 文件
# _startup: startup.nsh 文件
function(make_uefi_dir _boot _kernel _startup)
    add_custom_target(image_uefi DEPENDS boot ${_kernel}
            COMMENT "Copying bootloader and kernel"
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/image/
            COMMAND ${CMAKE_COMMAND} -E copy ${_boot} ${PROJECT_BINARY_DIR}/image/
            COMMAND ${CMAKE_COMMAND} -E copy ${_kernel} ${PROJECT_BINARY_DIR}/image/
            COMMAND ${CMAKE_COMMAND} -E copy ${_startup} ${PROJECT_BINARY_DIR}/image/
    )
endfunction()

# 添加测试覆盖率 target
# DEPENDS 要生成的 targets
# SOURCE_DIR 源码路径
# BINARY_DIR 二进制文件路径
# EXCLUDE_DIR 要排除的目录
function(add_coverage)
    # 解析参数
    set(options)
    set(one_value_keywords SOURCE_DIR BINARY_DIR)
    set(multi_value_keywords DEPENDS EXCLUDE_DIR)
    cmake_parse_arguments(
            ARG "${options}" "${one_value_keywords}" "${multi_value_keywords}" ${ARGN}
    )

    # 不检查的目录
    list(APPEND EXCLUDES --exclude)
    foreach (_item ${ARG_EXCLUDE_DIR})
        list(APPEND EXCLUDES '${_item}')
    endforeach ()

    # 添加 target
    add_custom_target(coverage DEPENDS ${ARG_DEPENDS}
            COMMAND ${CMAKE_CTEST_COMMAND}
    )
    # 在 coverage 执行完毕后生成报告
    add_custom_command(TARGET coverage
            COMMENT "Generating coverage report ..."
            POST_BUILD
            WORKING_DIRECTORY ${ARG_BINARY_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_OUTPUT_DIR}
            COMMAND ${LCOV_EXE}
            -c
            -o ${COVERAGE_OUTPUT_DIR}/coverage.info
            -d ${ARG_BINARY_DIR}
            -b ${ARG_SOURCE_DIR}
            --no-external
            ${EXCLUDES}
            --rc lcov_branch_coverage=1
            COMMAND ${GENHTML_EXE}
            ${COVERAGE_OUTPUT_DIR}/coverage.info
            -o ${COVERAGE_OUTPUT_DIR}
            --branch-coverage
    )
endfunction()
