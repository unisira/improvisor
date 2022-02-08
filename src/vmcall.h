#ifndef IMP_VMCALL_H
#define IMP_VMCALL_H

#include "vcpu.h"
#include "ept.h"

#define HRESULT_SUCCESS (0x8100)
// Unknown hypercall ID
#define HRESULT_UNKNOWN_HCID (0x8101) 
// Invalid target address (RDX) value
#define HRESULT_INVALID_TARGET_ADDR (0x8102)
// Invalid buffer address (RCX) value
#define HRESULT_INVALID_BUFFER_ADDR (0x8103)
// Insufficient resources to complete the requested task
#define HRESULT_INSUFFICIENT_RESOURCES (0x8104)
// The extended hypercall info (RBX) value was invalid 
#define HRESULT_INVALID_EXT_INFO (0x8105)

typedef ULONG HYPERCALL_RESULT; 

typedef union _HYPERCALL_INFO
{
    UINT64 Value;

    struct
    {
        UINT64 Id : 16;
        UINT64 Result: 16;
    };
} HYPERCALL_INFO, *PHYPERCALL_INFO; 

VMM_EVENT_STATUS
VmHandleHypercall(
    _In_ PVCPU Vcpu,
    _In_ PGUEST_STATE GuestState,
    _In_ PHYPERCALL_INFO Hypercall
);

//
// Hypercall implementations for kernel level code
//

HYPERCALL_RESULT
VmReadSystemMemory(
    _In_ PVOID Src,
    _In_ PVOID Dst,
    _In_ SIZE_T Size
);

HYPERCALL_RESULT
VmWriteSystemMemory(
    _In_ PVOID Src,
    _In_ PVOID Dst,
    _In_ SIZE_T Size
);

HYPERCALL_RESULT
VmShutdownVcpu(
    _Out_ PVCPU* pVcpu
);

HYPERCALL_RESULT
VmEptRemapPages(
    _In_ UINT64 GuestPhysAddr,
    _In_ UINT64 PhysAddr,
    _In_ SIZE_T Size,
    _In_ EPT_PAGE_PERMISSIONS Permissions
);

HYPERCALL_RESULT
VmGetLastResult(
    _Out_ HYPERCALL_RESULT* pResult
);

#endif
