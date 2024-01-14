cmake_minimum_required(VERSION 3.16)

project(improvisor)

# Enable C, C++ and MASM
enable_language(C CXX ASM_MASM)

add_subdirectory(improvisor-drv)
add_subdirectory(improvisor-ldr)
