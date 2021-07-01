#ifndef IMP_VM_H
#define IMP_VM_H

#include <wdm.h>

#include "vcpu.h"

typedef struct _VMM_CONTEXT
{
    UINT8 CpuCount;
    PVCPU VcpuTable;
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
