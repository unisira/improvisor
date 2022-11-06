#include <improvisor.h>
#include <arch/memory.h>
#include <arch/mtrr.h>
#include <arch/cr.h>
#include <spinlock.h>
#include <macro.h>
#include <mm/mm.h>
#include <mm/vpte.h>
#include <os/pe.h>

// TODO: For host page tables, include MM_RESERVED_PT header in the raw PT list allocation
VMM_DATA PMM_RESERVED_PT gHostPageTablesHead = NULL;
VMM_DATA PMM_RESERVED_PT gHostPageTablesTail = NULL;

// The raw array of page tables, each comprising of 512 possible entries
VMM_DATA static PVOID sPageTableListRaw = NULL;
// The raw list of page table entries for the linked list
VMM_DATA static PMM_RESERVED_PT sPageTableListEntries = NULL;

// TODO: Move away from use of NTSTATUS for non-setup / windows related functions

VSC_API
PVOID
MmWinPhysicalToVirtual(
	_In_ UINT64 PhysAddr
)
/*++
Routine Description:
	Maps a physical address into memory using Windows' memory manager.
--*/
{
	PHYSICAL_ADDRESS Addr = {
		.QuadPart = PhysAddr
	};

	return MmGetVirtualForPhysical(Addr);
}

VSC_API
PMM_PTE 
MmWinReadPageTableEntry(
	_In_ UINT64 TablePfn,
	_In_ SIZE_T Index
)
/*++
Routine Description:
	Maps a page table into memory using a page frame number `TablePfn` and reads the entry selected by 
	`Index` into `Entry`
--*/
{
	PMM_PTE Table = MmWinPhysicalToVirtual(PAGE_ADDRESS(TablePfn));

	return &Table[Index];
}

VSC_API
PMM_PTE
MmGetHostPageTableVirtAddr(
	_In_ UINT64 PhysAddr
)
{
	PMM_RESERVED_PT CurrTable = gHostPageTablesHead;
	while (CurrTable != NULL)
	{
		if (CurrTable->TablePhysAddr == PhysAddr)
			return CurrTable->TableAddr;

		CurrTable = (PMM_RESERVED_PT)CurrTable->Links.Blink;
	}

	return NULL;
}

VSC_API
PMM_PTE
MmReadHostPageTableEntry(
	_In_ UINT64 TablePfn,
	_In_ SIZE_T Index
)
/*++
Routine Description:
	Maps a page table into memory using a page frame number `TablePfn` and reads the entry selected by
	`Index` into `Entry`
--*/
{
	PMM_PTE Table = MmGetHostPageTableVirtAddr(PAGE_ADDRESS(TablePfn));

	return &Table[Index];
}

VSC_API
NTSTATUS
MmWinTranslateAddrVerbose(
	_In_ PVOID Address,
	_Out_ PMM_PTE pPml4e,
	_Out_ PMM_PTE pPdpte,
	_Out_ PMM_PTE pPde,
	_Out_ PMM_PTE pPte
)
/*++
Routine Description:
	Translates a linear address using the active page tables and returns a copy of each of the page table 
	entries
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	X86_CR3 Cr3 = {
		.Value = __readcr3()
	};

	MM_PTE Pte = {0};

	X86_LA48 LinearAddr = {
		.Value = (UINT64)Address
	};
	
	Pte = *MmWinReadPageTableEntry(Cr3.PageDirectoryBase, LinearAddr.Pml4Index);
	if (!Pte.Present)
	{
		ImpDebugPrint("PML4E for '%llX' is invalid...\n", Address);
		return STATUS_INVALID_PARAMETER;
	}

	*pPml4e = Pte;
	
	Pte = *MmWinReadPageTableEntry(Pte.PageFrameNumber, LinearAddr.PdptIndex);
	if (!Pte.Present)
	{
		ImpDebugPrint("PDPTE for '%llX' is invalid...\n", Address);
		return STATUS_INVALID_PARAMETER;
	}

	*pPdpte = Pte;

	if (Pte.LargePage)
		return STATUS_SUCCESS;

	Pte = *MmWinReadPageTableEntry(Pte.PageFrameNumber, LinearAddr.PdIndex);
	if (!Pte.Present)
	{
		ImpDebugPrint("PDE for '%llX' is invalid...\n", Address);
		return STATUS_INVALID_PARAMETER;
	}

	*pPde = Pte;

	if (Pte.LargePage)
		return STATUS_SUCCESS;

	Pte = *MmWinReadPageTableEntry(Pte.PageFrameNumber, LinearAddr.PtIndex);
	if (!Pte.Present)
	{
		ImpDebugPrint("PTE for '%llX' is invalid...\n", Address);
		return STATUS_INVALID_PARAMETER;
	}

	*pPte = Pte;

	return Status;
}

VSC_API
NTSTATUS
MmHostReservePageTables(
	_In_ SIZE_T Count
)
/*++
Routine Description:
	Reserves entire page tables by allocating them and creating a linked list which can be taken from and
	used in the host page tables. This is done so that they can also be mapped into the page tables as they
	will show up inside of the host allocations linked list.
--*/
{
	NTSTATUS Status = STATUS_SUCCESS; 

	sPageTableListRaw = ImpAllocateHostNpPool(PAGE_SIZE * Count);
	if (sPageTableListRaw == NULL)
	{
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto panic;
	}

	sPageTableListEntries = ImpAllocateHostNpPool(sizeof(MM_RESERVED_PT) * Count);
	if (sPageTableListEntries == NULL)
	{
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto panic;
	}

	// Setup the head as the first entry in this array
	gHostPageTablesTail = gHostPageTablesHead = sPageTableListEntries;

	for (SIZE_T i = 0; i < Count; i++)
	{
		PMM_RESERVED_PT CurrTable = sPageTableListEntries + i;

		CurrTable->TableAddr = (PCHAR)sPageTableListRaw + (i * PAGE_SIZE);
		CurrTable->TablePhysAddr = ImpGetPhysicalAddress(CurrTable->TableAddr);

		CurrTable->Links.Flink = i < Count - 1	? &(CurrTable + 1)->Links : NULL;
		CurrTable->Links.Blink = i > 0			? &(CurrTable - 1)->Links : NULL;
	}

panic:
	if (!NT_SUCCESS(Status))
	{
		if (sPageTableListRaw)
			ExFreePoolWithTag(sPageTableListRaw, POOL_TAG);
		if (sPageTableListEntries)
			ExFreePoolWithTag(sPageTableListEntries, POOL_TAG);
	}
		
	return Status;
}

VMM_API
NTSTATUS
MmAllocateHostPageTable(
	_Out_ PVOID* pTable
)
/*++
Routine Description:
	Allocates a host page table from the linked list and returns it
--*/
{
	if (gHostPageTablesHead->Links.Flink == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	PMM_RESERVED_PT ReservedPt = gHostPageTablesHead;

	*pTable = ReservedPt->TableAddr;

	gHostPageTablesHead = (PMM_RESERVED_PT)ReservedPt->Links.Flink;

	return STATUS_SUCCESS;
}

VMM_API
PMM_RESERVED_PT
MmGetLastAllocatedPageTable(VOID)
/*++
Routine Description:
	This function returns the last allocated host page table entry
--*/
{
	return (PMM_RESERVED_PT)gHostPageTablesHead->Links.Blink;
}

VSC_API
NTSTATUS
MmPrepareVmmImageData(
	PVOID ImageBase
)
/*++
Routine Description:
	Maps all VMM-owned sections for the PE file found at `ImageBase` into the host page tables
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	SIZE_T SectionCount;
	PIMAGE_SECTION_HEADER Sections = PeGetSectionHeaders(ImageBase, &SectionCount);

	if (SectionCount == -1)
	{
		ImpDebugPrint("Invalid PE image from ImageBase...\n");
		return STATUS_INVALID_ADDRESS;
	}
	
	for (SIZE_T i = 0; i < SectionCount; i++)
	{
		PIMAGE_SECTION_HEADER Section = &Sections[i];

		// TODO: Refactor to switch statement once conversion to C++ is done

		// Match any .VMM* section name
		if (memcmp(Section->Name, ".VMM", 4) == 0)
		{
			// Map all .VMM* sections as host allocations.
			if (!NT_SUCCESS(ImpInsertAllocRecord(RVA_PTR(ImageBase, Section->VirtualAddress), Section->SizeOfRawData, IMP_HOST_ALLOCATION)))
			{
				ImpDebugPrint("Failed to create IMPV section allocation record for '%s' (%llx)...\n", Section->Name, RVA_PTR(ImageBase, Section->VirtualAddress));
				return STATUS_INSUFFICIENT_RESOURCES;
			}
		}

		// Map the .text section in, this is generally considered shared code - TODO: Some functions in .text could be host-only
		if (memcmp(Section->Name, ".text", 5) == 0)
		{
			// Map all .VMM* sections as host allocations.
			if (!NT_SUCCESS(ImpInsertAllocRecord(RVA_PTR(ImageBase, Section->VirtualAddress), Section->SizeOfRawData, IMP_SHARED_ALLOCATION)))
			{
				ImpDebugPrint("Failed to create IMPV section allocation record for '%s' (%llx)...\n", Section->Name, RVA_PTR(ImageBase, Section->VirtualAddress));
				return STATUS_INSUFFICIENT_RESOURCES;
			}
		}

		// Map the .rdata section for strings
		if (memcmp(Section->Name, ".rdata", 6) == 0)
		{
			// Map all .VMM* sections as host allocations.
			if (!NT_SUCCESS(ImpInsertAllocRecord(RVA_PTR(ImageBase, Section->VirtualAddress), Section->SizeOfRawData, IMP_SHARED_ALLOCATION)))
			{
				ImpDebugPrint("Failed to create IMPV section allocation record for '%s' (%llx)...\n", Section->Name, RVA_PTR(ImageBase, Section->VirtualAddress));
				return STATUS_INSUFFICIENT_RESOURCES;
			}
		}

		// .VSC section contains VMM startup code. This shouldn't be mapped anywhere once the VMM starts
		if (memcmp(Section->Name, ".VSC", 4) == 0)
		{
			if (!NT_SUCCESS(ImpInsertAllocRecord(RVA_PTR(ImageBase, Section->VirtualAddress), Section->SizeOfRawData, IMP_SHADOW_ALLOCATION)))
			{
				ImpDebugPrint("Failed to create IMPV section allocation record for '%s' (%llx)...\n", Section->Name, RVA_PTR(ImageBase, Section->VirtualAddress));
				return STATUS_INSUFFICIENT_RESOURCES;
			}
		}
	}

	return Status;
}

VSC_API
NTSTATUS
MmInitialise(
	_Inout_ PMM_INFORMATION MmSupport
)
{
	NTSTATUS Status = STATUS_SUCCESS;

	// Reserve 1000 page tables for the host to create its own page tables and still
	// have access to them post-VMLAUNCH
	Status = MmHostReservePageTables(500);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to reserve page tables for the host...\n");
		return Status;
	}

	// Cache MTRR regions for EPT 
	if (!NT_SUCCESS(MtrrInitialise()))
		return STATUS_INSUFFICIENT_RESOURCES;

	Status = MmSetupHostPageDirectory(MmSupport);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to setup the host page directory... (%X)\n", Status);
		return Status;
	}

	// TODO: Pool allocator

	return Status;
}

VMM_API
NTSTATUS
MmReadGuestPhys(
	_In_ UINT64 PhysAddr,
	_In_ SIZE_T Size,
	_In_ PVOID Buffer
)
/*++
Routine Description:
	This function reads physical memory by using one of the VPTEs from the guest mapping range. Buffer doesn't need to be
	page boundary checked as it is only used in host mode
--*/
{
	// TODO: Cache VPTE translations (VTLB) (Fire Performance) (Real)
	PMM_VPTE Vpte = NULL;
	if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
		return STATUS_INSUFFICIENT_RESOURCES;

	SIZE_T SizeRead = 0;
	while (Size > SizeRead)
	{
		const SIZE_T MaxReadable = PAGE_SIZE - PAGE_OFFSET(PhysAddr + SizeRead);

		MmMapGuestPhys(Vpte, PhysAddr + SizeRead);

		SIZE_T SizeToRead = Size - SizeRead > MaxReadable ? MaxReadable : Size - SizeRead;		
		RtlCopyMemory(RVA_PTR(Buffer, SizeRead), Vpte->MappedVirtAddr, SizeToRead);

		SizeRead += SizeToRead;
	}

	MmFreeVpte(Vpte);

	return STATUS_SUCCESS;
}

VMM_API
NTSTATUS
MmWriteGuestPhys(
	_In_ UINT64 PhysAddr,
	_In_ SIZE_T Size,
	_In_ PVOID Buffer
)
/*++
Routine Description:
	Writes the contents of `Buffer` to a physical address
--*/
{
	PMM_VPTE Vpte = NULL;
	if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
		return STATUS_INSUFFICIENT_RESOURCES;
	
	SIZE_T SizeWritten = 0;
	while (Size > SizeWritten)
	{
		MmMapGuestPhys(Vpte, PhysAddr + SizeWritten);

		SIZE_T SizeToWrite = Size - SizeWritten > PAGE_SIZE - (PhysAddr & 0xFFF) ? PAGE_SIZE - (PhysAddr & 0xFFF) : Size - SizeWritten;
		RtlCopyMemory(Vpte->MappedVirtAddr, (PCHAR)Buffer + SizeWritten, SizeToWrite);

		SizeWritten += SizeToWrite;
	}

	MmFreeVpte(Vpte);

	return STATUS_SUCCESS;
}

VMM_API
NTSTATUS
MmReadGuestVirt(
	_In_ UINT64 TargetCr3,
	_In_ UINT64 VirtAddr,
	_In_ SIZE_T Size,
	_Inout_ PVOID Buffer
)
/*++
Routine Description:
	Maps a virtual address using the translation in `GuestCr3` and reads `Size` bytes into `Buffer`. Size must not cross any page boundaries
	for `VirtAddr` or `Buffer`
--*/
{
	if (Size > PAGE_SIZE - max(PAGE_OFFSET(VirtAddr), PAGE_OFFSET(Buffer)))
		return STATUS_INVALID_BUFFER_SIZE;

	PMM_VPTE Vpte = NULL;
	if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
		return STATUS_INSUFFICIENT_RESOURCES;

	if (!NT_SUCCESS(MmMapGuestVirt(Vpte, TargetCr3, VirtAddr)))
		return STATUS_INVALID_PARAMETER;

	RtlCopyMemory(Buffer, Vpte->MappedVirtAddr, Size);

	MmFreeVpte(Vpte);

	return STATUS_SUCCESS;
}

VMM_API
NTSTATUS
MmWriteGuestVirt(
	_In_ UINT64 TargetCr3,
	_In_ UINT64 VirtAddr,
	_In_ SIZE_T Size,
	_In_ PVOID Buffer
)
/*++
Routine Description:
	Maps a virtual address using the translation in `GuestCr3` and writes `Buffer` to it. Size must not cross any page boundaries
	for `VirtAddr` or `Buffer`
--*/
{
	if (Size > PAGE_SIZE - max(PAGE_OFFSET(VirtAddr), PAGE_OFFSET(Buffer)))
		return STATUS_INVALID_BUFFER_SIZE;

	PMM_VPTE Vpte = NULL;
	if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
		return STATUS_INSUFFICIENT_RESOURCES;
	
	if (!NT_SUCCESS(MmMapGuestVirt(Vpte, TargetCr3, VirtAddr)))
		return STATUS_INVALID_PARAMETER;

	RtlCopyMemory(Vpte->MappedVirtAddr, Buffer, Size);

	MmFreeVpte(Vpte);

	return STATUS_SUCCESS;
}

UINT64
MmResolveGuestVirtAddr(
	_In_ UINT64 TargetCr3,
	_In_ UINT64 PhysAddr
)
/*++
Routine Description:
	Resolves virtual addresses by using Window's self referencing PTE's. Essentially reimplements MmGetVirtualForPhysical
--*/
{
	// TODO: Understand why this number changes.
	UINT64 TranslateAddr = (48 * (PhysAddr >> 12)) + 0xFFFFF30000000008;
	
	UINT64 TranslateData = 0;
	if (!NT_SUCCESS(MmReadGuestVirt(TargetCr3, TranslateAddr, sizeof(UINT64), &TranslateData)))
		return -1;

	if (TranslateData == 0)
		return -1;

	return (PhysAddr & 0xFFF) + ((INT64)(TranslateData << 25) >> 16);
}