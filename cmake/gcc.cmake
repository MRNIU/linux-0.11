
# This file is a part of MRNIU/linux-0.11
# (https://github.com/MRNIU/linux-0.11).
# 
# gcc.cmake for MRNIU/linux-0.11.

if (APPLE)
    message(STATUS "Now is Apple systens.")
    # GCC
    find_program(Compiler_gcc x86_64-elf-g++)
    if (NOT Compiler_gcc)
        message(FATAL_ERROR "x86_64-elf-g++ not found.\n"
                "Run `brew install x86_64-elf-g++` to install.")
    else ()
        message(STATUS "Found x86_64-elf-g++ ${Compiler_gcc}")
    endif ()

    set(TOOLCHAIN_PREFIX x86_64-elf-)
    set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
    set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
    set(CMAKE_READELF ${TOOLCHAIN_PREFIX}readelf)
    set(CMAKE_AR ${TOOLCHAIN_PREFIX}ar)
    set(CMAKE_LINKER ${TOOLCHAIN_PREFIX}ld)
    set(CMAKE_NM ${TOOLCHAIN_PREFIX}nm)
    set(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump)
    set(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}ranlib)

    # 跳过编译器检查
    set(CMAKE_C_COMPILER_FORCED TRUE)
    set(CMAKE_CXX_COMPILER_FORCED TRUE)

    # qemu
    find_program(QEMU qemu-system-x86_64)
    if (NOT QEMU)
        message(FATAL_ERROR "qemu not found.\n"
                "Run `brew install qemu-system` to install.")
    else ()
        message(STATUS "Found qemu ${QEMU}")
    endif ()
elseif (UNIX)
    message(STATUS "Now is UNIX-like OS's.")
    # GCC
    find_program(Compiler_gcc g++)
    if (NOT Compiler_gcc)
        message(FATAL_ERROR "g++ not found.\n"
                "Run `sudo apt-get install -y gcc g++` to install.")
    else ()
        message(STATUS "Found g++ ${Compiler_gcc}")
    endif ()

    # qemu
    find_program(QEMU qemu-system-x86_64)
    if (NOT QEMU)
        message(FATAL_ERROR "qemu not found.\n"
                "Run `sudo apt-get install -y qemu-system` to install.")
    else ()
        message(STATUS "Found qemu ${QEMU}")
    endif ()
endif ()
