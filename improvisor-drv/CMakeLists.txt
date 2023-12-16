# Find the WDK package
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
find_package(WDK REQUIRED)

wdk_add_driver(improvisor-drv
    src/arch/cpu.asm
    src/arch/cpuid.c
    src/arch/mtrr.c
    src/arch/segment.c
    src/mm/image.c
    src/mm/mm.c
    src/mm/vpte.c
    src/os/input.c
    src/os/pe.c
    src/pdb/pdb.c
    src/vcpu/interrupts.asm
    src/vcpu/interrupts.c
    src/vcpu/tsc.asm
    src/vcpu/tsc.c
    src/vcpu/vcpu.asm
    src/vcpu/vcpu.c
    src/vcpu/vdr.c
    src/vcpu/vmcall.asm
    src/vcpu/vmcall.c
    src/vcpu/vmexit.asm
    src/vcpu/vmexit.c
    src/ll.c
    src/detour.c
    src/ept.c
    src/fmt.c
    src/hash.c
    src/improvisor.c
    src/ldasm.c
    src/ldr.c
    src/spinlock.c
    src/vmm.c
    src/vmx.c
    src/win.c
    src/main.c
)

target_include_directories(improvisor-drv PUBLIC src)
