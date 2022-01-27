#ifndef IMP_VM_H
#define IMP_VM_H

#include <wdm.h>
#include <intrin.h>

#include "arch/segment.h"
#include "vcpu.h"
#include "vtsc.h"
#include "mm.h"

typedef struct _VMM_CONTEXT
{
    UINT8 CpuCount;
    PVCPU VcpuTable;
    BOOLEAN UseUnrestrictedGuests;
    MM_SUPPORT MmSupport;
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
