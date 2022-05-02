#ifndef IMP_MM_H
#define IMP_MM_H

#include <ntdef.h>

#include "arch/cr.h"

typedef struct _MM_INFORMATION
{
	X86_CR3 Cr3;
} MM_INFORMATION, *PMM_INFORMATION;

typedef union _MM_PTE
{
	UINT64 Value;

	struct
	{
		UINT64 Present : 1;
		UINT64 WriteAllowed : 1;
		UINT64 SupervisorOwned : 1;
		UINT64 WriteThrough : 1;
		UINT64 CacheDisable : 1;
		UINT64 Accessed : 1;
		UINT64 Dirty : 1; 
		UINT64 LargePage : 1; 
		UINT64 Global : 1;
		UINT64 Ignored1 : 3; // Windows has Write (1), CoW (3) here 
		UINT64 PageFrameNumber : 40;
		UINT64 Ignored3 : 11;
		UINT64 ExecuteDisable : 1;
	};
} MM_PTE, *PMM_PTE;

typedef struct _MM_VPTE
{
	LIST_ENTRY Links;
	PMM_PTE Pte;
	UINT64 PtePhysAddr;
	PVOID MappedVirtAddr;
	UINT64 MappedPhysAddr;
	PVOID MappedAddr;
} MM_VPTE, *PMM_VPTE;

typedef struct _MM_RESERVED_PT
{
	LIST_ENTRY Links;
	PVOID TableAddr;
	UINT64 TablePhysAddr;
} MM_RESERVED_PT, *PMM_RESERVED_PT;

extern PMM_RESERVED_PT gHostPageTablesHead;
extern PMM_RESERVED_PT gHostPageTablesTail;

NTSTATUS
MmInitialise(
	_Inout_ PMM_INFORMATION MmSupport
);

NTSTATUS
MmAllocateHostPageTable(
	_Out_ PVOID* Table
);

PMM_RESERVED_PT
MmGetLastAllocatedPageTableEntry(VOID);

NTSTATUS
MmAllocateVpte(
	_Out_ PMM_VPTE* pVpte
);

VOID
MmFreeVpte(
	_Inout_ PMM_VPTE Vpte
);

NTSTATUS
MmWriteGuestPhys(
	_In_ UINT64 PhysAddr,
	_In_ SIZE_T Size,
	_In_ PVOID Buffer
);

NTSTATUS
MmReadGuestPhys(
	_In_ UINT64 PhysAddr,
	_In_ SIZE_T Size,
	_In_ PVOID Buffer
);

NTSTATUS
MmMapGuestVirt(
	_Inout_ PMM_VPTE Vpte,
	_In_ UINT64 GuestCr3,
	_In_ UINT64 VirtAddr
);

NTSTATUS
MmWriteGuestVirt(
	_In_ UINT64 GuestCr3,
	_In_ UINT64 VirtAddr,
	_In_ SIZE_T Size,
	_In_ PVOID Buffer
);

NTSTATUS
MmReadGuestVirt(
	_In_ UINT64 GuestCr3,
	_In_ UINT64 VirtAddr,
	_In_ SIZE_T Size,
	_In_ PVOID Buffer
);

#endif
