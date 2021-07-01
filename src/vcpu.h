#ifndef IMP_VCPU_H
#define IMP_VCPU_H

#include "improvisor.h"
#include "vmx.h"
#include "cpu.h"

typedef struct _GUEST_STATE
{
    UINT64 Rax;
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

