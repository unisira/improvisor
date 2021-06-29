#ifndef IMP_VM_H
#define IMP_VM_H

#include <Wdm.h>

#include "vcpu.h"

#define POOL_TAG 'IMPV'

typedef struct _VMM_CONTEXT
{
    UINT8 CpuCount;
    PVCPU VcpuTable; 
} VMM_CONTEXT, *PVMM_CONTEXT

NTSTATUS 
VmmStartHypervisor(
    VOID
);

VOID 
VmmShutdownHypervisor(
    VOID
);

#endif
