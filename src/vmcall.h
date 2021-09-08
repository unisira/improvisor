#ifndef IMP_VMCALL_H
#define IMP_VMCALL_H

#include "vcpu.h"

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

#endif
