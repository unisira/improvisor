#ifndef IMP_VCPU_H
#define IMP_VCPU_H

#include "improvisor.h"
#include "vmx.h"
#include "cpu.h"
#include "mm.h"
#include "tsc.h"

// Emulation was successful, continue execution
#define VMM_EVENT_CONTINUE (0x00000000)
// An interrupt/exception was injected into the guest
#define VMM_EVENT_INTERRUPT (0x00000001)
// The hypervisor encountered an error and should shut down immediately
#define VMM_EVENT_ABORT (0x00000002)

#pragma pack(push, 1)
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
    M128 Xmm0;
    M128 Xmm1;
    M128 Xmm2;
    M128 Xmm3;
    M128 Xmm4;
    M128 Xmm5;
    M128 Xmm6;
    M128 Xmm7;
    M128 Xmm8;
    M128 Xmm9;
    M128 Xmm10;
    M128 Xmm11;
    M128 Xmm12;
    M128 Xmm13;
    M128 Xmm14;
    M128 Xmm15;
    UINT64 RFlags;
} GUEST_STATE, *PGUEST_STATE;
#pragma pack(pop)

typedef struct _VCPU_DELEGATE_PARAMS
{
    struct _VMM_CONTEXT* VmmContext;
    ULONG ActiveVcpuCount;
    ULONG FailedCoreMask;
    NTSTATUS Status;
} VCPU_DELEGATE_PARAMS, *PVCPU_DELEGATE_PARAMS;

typedef union _VCPU_STACK
{
    UINT8 Limit[0x6000];

    struct 
    {
        struct _VCPU* Vcpu;
    } Cache;
} VCPU_STACK, *PVCPU_STACK;

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
    UINT64 MsrBitmapPhysical;
    BOOLEAN IsLaunched;
    BOOLEAN IsShuttingDown;
    CPU_STATE LaunchState;
    VMX_STATE Vmx;
    TSC_STATUS TscInfo;
    UINT64 Cr0ShadowableBits;
    UINT64 Cr4ShadowableBits;
    BOOLEAN UnrestrictedGuest;
    struct _VMM_CONTEXT* Vmm; 
} VCPU, *PVCPU;

typedef enum _MSR_ACCESS
{
	MSR_READ,
	MSR_WRITE
} MSR_ACCESS, *PMSR_ACCESS;

typedef ULONG VMM_EVENT_STATUS;

typedef VMM_EVENT_STATUS VMEXIT_HANDLER(_Inout_ PVCPU, _Inout_ PGUEST_STATE);

NTSTATUS 
VcpuSetup(
    _Inout_ PVCPU Vcpu,
    _In_ UINT8 Id
);

NTSTATUS
VcpuDestroy(
    _Inout_ PVCPU Vcpu
);

VOID
VcpuSpawnPerCpu(
    _Inout_ PVCPU_DELEGATE_PARAMS Params
);

VOID
VcpuShutdownPerCpu(
    _Inout_ PVCPU Vcpu 
);

#endif

