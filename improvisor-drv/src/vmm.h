#ifndef IMP_VM_H
#define IMP_VM_H

#include <wdm.h>

#include <arch/interrupt.h>
#include <arch/segment.h>
#include <vcpu/vcpu.h>
#include <vcpu/tsc.h>
#include <mm/mm.h>
#include <ept.h>

extern const X86_SEGMENT_SELECTOR gVmmCsSelector;
extern const X86_SEGMENT_SELECTOR gVmmTssSelector;

typedef struct _VMM_CONTEXT
{
	UINT8 CpuCount;
	PVCPU VcpuTable;
	BOOLEAN UseUnrestrictedGuests;
	BOOLEAN UseTscSpoofing;
	MM_INFORMATION Mm;
	EPT_INFORMATION Ept;
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
