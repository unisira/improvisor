#ifndef IMP_VCPU_H
#define IMP_VCPU_H

#include "improvisor.h"
#include "vmx.h"
#include "cpu.h"

typedef struct _GUEST_STATE
{
    UINT64 Rax;
    UINT64 Rbx;
    UINT64 Rcx;
    UINT64 Rdx;
    UINT64 Rsi;
    UINT64 Rdi;
    UINT64 R8;
    UINT64 R9;
    UINT64 R10;
    UINT64 R11;
    UINT64 R12;
    UINT64 R13;
    UINT64 R14;
    UINT64 R15;
    UINT64 Rip;
    UINT32 MxCsr;
    UINT32 _Align1;
    UINT64 _Align2;
    M128A Xmm0;
    M128A Xmm1;
    M128A Xmm2;
    M128A Xmm3;
    M128A Xmm4;
    M128A Xmm5;
    M128A Xmm6;
    M128A Xmm7;
    M128A Xmm8;
    M128A Xmm9;
    M128A Xmm10;
    M128A Xmm11;
    M128A Xmm12;
    M128A Xmm13;
    M128A Xmm14;
    M128A Xmm15;
    UINT64 RFlags;
} GUEST_STATE, *PGUEST_STATE;

typedef struct _VCPU_DELEGATE_PARAMS
{
    struct _VMM_CONTEXT* VmmContext;
    ULONG ActiveVcpuCount;
    ULONG FaultyCoreId;
    NTSTATUS Status;
} VCPU_DELEGATE_PARAMS, *PVCPU_DELEGATE_PARAMS;

typedef union _VCPU_STACK
{
    UINT8 Limit[KERNEL_STACK_SIZE];

    struct 
    {
        struct _VCPU* Vcpu;
    } Cache;
} VCPU_STACK, *PVCPU_STACK;

typedef struct _TSC_INFO
{
    UINT64 VmEntryLatency;
} TSC_INFO, *PTSC_INFO;

typedef struct _VCPU
{
    UINT8 Id;
    PCHAR MsrBitmap;
    RTL_BITMAP MsrLoReadBitmap;
    RTL_BITMAP MsrHiReadBitmap;
    RTL_BITMAP MsrLoWriteBitmap;
    RTL_BITMAP MsrHiWriteBitmap;
    PVCPU_STACK Stack;
    PVMX_REGION Vmcs;
    PVMX_REGION Vmxon;
    UINT64 VmcsPhysical;
    UINT64 VmxonPhysical;
    BOOLEAN IsLaunched;
    CPU_STATE LaunchState;
    VMX_STATE Vmx;
    TSC_INFO TscInfo;
    struct _VMM_CONTEXT* Vmm; 
} VCPU, *PVCPU;

typedef enum _MSR_ACCESS
{
	MSR_READ,
	MSR_WRITE
} MSR_ACCESS, *PMSR_ACCESS;

typedef ULONG VMM_EVENT_STATUS;

// Emulation was successful, continue execution
#define VMM_EVENT_CONTINUE 0x00000000
// An interrupt/exception was injected into the guest
#define VMM_EVENT_INTERRUPT 0x00000001


typedef VMM_EVENT_STATUS VMEXIT_HANDLER(PVCPU, PGUEST_STATE);

NTSTATUS 
VcpuSetup(
    _Inout_ PVCPU Vcpu,
    _In_ UINT8 Id
);

VOID
VcpuSpawnPerCpu(
    _Inout_ PVCPU_DELEGATE_PARAMS Params
);

VOID
VcpuShutdownPerCpu(
    _Inout_ PVCPU_DELEGATE_PARAMS Params
);

#endif

