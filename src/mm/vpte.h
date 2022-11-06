#ifndef IMP_MM_VPTE_H
#define IMP_MM_VPTE_H

#include <ntdef.h>

#include <mm/mm.h>

typedef struct _MM_VPTE
{
	LIST_ENTRY Links;
	// Pointer to the PTE being used to map `MappedAddr`
	PMM_PTE Pte;
	// The address this VPTE is being used to map
	PVOID MappedVirtAddr;
	// The physical address of the memory being mapped by `Pte`
	UINT64 MappedPhysAddr;
	// Address the VPTE is mapped to
	PVOID MappedAddr;
} MM_VPTE, * PMM_VPTE;

NTSTATUS
MmAllocateVpteList(
	_In_ SIZE_T Count
);

NTSTATUS
MmAllocateVpte(
	_Out_ PMM_VPTE* pVpte
);

VOID
MmFreeVpte(
	_Inout_ PMM_VPTE Vpte
);

VOID
MmMapGuestPhys(
	_Inout_ PMM_VPTE Vpte,
	_In_ UINT64 PhysAddr
);

NTSTATUS
MmMapGuestVirt(
	_Inout_ PMM_VPTE Vpte,
	_In_ UINT64 TargetCr3,
	_In_ UINT64 VirtAddr
);

NTSTATUS
MmSetupHostPageDirectory(
	_Inout_ PMM_INFORMATION MmSupport
);

#endif