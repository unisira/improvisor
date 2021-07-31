#ifndef IMP_VM_H
#define IMP_VM_H

#include <wdm.h>

#include "arch/segment.h"
#include "mm/page.h"
#include "vcpu.h"

typedef struct _VMM_CONTEXT
{
    UINT8 CpuCount;
    PVCPU VcpuTable;
    BOOLEAN UseUnrestrictedGuests;
    PMM_SUPPORT MmSupport;
    X86_PSEUDO_DESCRIPTOR HostInterruptDescriptor;
} VMM_CONTEXT, * PVMM_CONTEXT;

NTSTATUS 
VmmStartHypervisor(
    VOID
);

VOID 
VmmShutdownHypervisor(
    VOID
);

#endif
