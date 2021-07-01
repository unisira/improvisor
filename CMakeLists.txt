cmake_minimum_required(VERSION 3.0)

project(improvisor)

enable_language(C ASM_MASM)

# Find the WDK package
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
find_package(WDK REQUIRED)

wdk_add_driver(improvisor
    src/util/spinlock.c
    src/improvisor.c
    src/vcpu.c
    src/vmm.c
    src/vmx.c
    src/vmexit.asm
    src/main.c
    )

target_include_directories(improvisor INTERFACE .)
