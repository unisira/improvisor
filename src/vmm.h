#ifndef IMP_VM_H
#define IMP_VM_H

#include <wdm.h>
#include <intrin.h>

#include "arch/interrupt.h"
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
	PX86_INTERRUPT_TRAP_GATE Idt;
	PX86_SEGMENT_DESCRIPTOR Gdt;
	PX86_TASK_STATE_SEGMENT Tss;
	BOOLEAN UseUnrestrictedGuests;
	BOOLEAN UseTscSpoofing;
	MM_INFORMATION MmInformation;
	EPT_INFORMATION EptInformation;
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
