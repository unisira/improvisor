cmake_minimum_required(VERSION 3.0)

project(improvisor)

enable_language(C ASM_MASM)

# Find the WDK package
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
find_package(WDK REQUIRED)

wdk_add_driver(improvisor
    src/arch/cpuid.c
    src/arch/segment.c
    src/util/hash.c
    src/util/fmt.c
    src/util/spinlock.c
    src/dbg.c
    src/ldasm.c
    src/improvisor.c
    src/ldr.c
    src/pdb.c
    src/pe.c
    src/mm.c
    src/ept.c
    src/detour.c
    src/vmcall.c
    src/vcpu.c
    src/vtsc.c
    src/mtrr.c
    src/vmm.c
    src/vmx.c
    src/vtsc.asm
    src/vmcall.asm
    src/vmexit.asm
    src/cpu_state.asm
    src/intrin.asm
    src/vmm_intr.asm
    src/main.c
    )

target_include_directories(improvisor INTERFACE src/)
