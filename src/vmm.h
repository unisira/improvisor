#ifndef IMP_VM_H
#define IMP_VM_H

#include <wdm.h>
#include <intrin.h>

#include "arch/segment.h"
#include "section.h"
#include "vcpu.h"
#include "vtsc.h"
#include "ept.h"
#include "mm.h"

typedef struct _VMM_CONTEXT
{
    UINT8 CpuCount;
    PVCPU VcpuTable;
    BOOLEAN UseUnrestrictedGuests;
    BOOLEAN UseTscSpoofing;
    MM_INFORMATION MmInformation;
    EPT_INFORMATION EptInformation;
    X86_PSEUDO_DESCRIPTOR HostInterruptDescriptor;
} VMM_CONTEXT, *PVMM_CONTEXT;

NTSTATUS 
VmmStartHypervisor(
    VOID
);

VOID 
VmmShutdownHypervisor(
   VOID 
);

#endif
