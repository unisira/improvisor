#include <improvisor.h>
#include <arch/memory.h>
#include <mm/vpte.h>
#include <mm/mm.h>
#include <os/pe.h>
#include <spinlock.h>

#define VPTE_BASE ((PVOID)0xfffffb00b5000000)

#ifdef _DEBUG
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#endif

VMM_DATA PMM_VPTE gVirtualPTEHead = NULL;

// The raw list of virtual PTE list entries
VMM_DATA static PMM_VPTE sVirtualPTEListRaw = NULL;
// Lock for accessing the virtual PTE list
VMM_DATA static SPINLOCK sVirtualPTEListLock;

NTSTATUS
MmAllocateVpteList(
	_In_ SIZE_T Count
)
/*++
Routine Description:
	This function allocates a list of entries for the virtual PTE list which is used for
	mapping guest physical memory
--*/
{
	sVirtualPTEListRaw = ImpAllocateHostNpPool(sizeof(MM_VPTE) * (Count + 1));
	if (sVirtualPTEListRaw == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	gVirtualPTEHead = sVirtualPTEListRaw;

	for (SIZE_T i = 0; i < Count + 1; i++)
	{
		PMM_VPTE CurrVpte = sVirtualPTEListRaw + i;

		CurrVpte->Links.Flink = i < Count ? &(CurrVpte + 1)->Links : NULL;
		CurrVpte->Links.Blink = i > 0 ? &(CurrVpte - 1)->Links : NULL;
	}

	return STATUS_SUCCESS;
}

VMM_API
NTSTATUS
MmAllocateVpte(
	_Out_ PMM_VPTE* pVpte
)
/*++
Routine Description:
	This function takes a VPTE entry from the head of the list.
--*/
{
	SpinLock(&sVirtualPTEListLock);

	if (gVirtualPTEHead->Links.Flink == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	*pVpte = gVirtualPTEHead;

	gVirtualPTEHead = (PMM_VPTE)gVirtualPTEHead->Links.Flink;

	SpinUnlock(&sVirtualPTEListLock);

	return STATUS_SUCCESS;
}

VMM_API
VOID
MmFreeVpte(
	_Inout_ PMM_VPTE Vpte
)
/*++
Routine Description:
	This function frees a VPTE entry by clearing the structure, adding the entry to the end of the list
	and removing it from its current position
--*/
{
	SpinLock(&sVirtualPTEListLock);

	Vpte->MappedPhysAddr = 0;
	Vpte->MappedVirtAddr = NULL;
	// Invalidate this VPTE's PTE
	Vpte->Pte->Present = FALSE;
	Vpte->Pte->PageFrameNumber = 0;

	// Remove this entry from its current position
	PMM_VPTE NextVpte = (PMM_VPTE)Vpte->Links.Flink;
	PMM_VPTE PrevVpte = (PMM_VPTE)Vpte->Links.Blink;

	if (NextVpte != NULL)
		NextVpte->Links.Blink = PrevVpte ? &PrevVpte->Links : NULL;
	if (PrevVpte != NULL)
		PrevVpte->Links.Flink = NextVpte ? &NextVpte->Links : NULL;

	PMM_VPTE HeadVpte = gVirtualPTEHead;
	PMM_VPTE HeadNext = gVirtualPTEHead->Links.Flink;

	// Update the entry after the head entry's backwards link to the newly free'd VPTE
	HeadNext->Links.Blink = &Vpte->Links;

	// Update the new head entry with the old head entry's forward link
	Vpte->Links.Flink = HeadVpte->Links.Flink;
	// Set the new head entry's backwards link to the old head entry
	Vpte->Links.Blink = &HeadVpte->Links;
	// Update the old head entry's forward link to the new head entry
	HeadVpte->Links.Flink = &Vpte->Links;

	gVirtualPTEHead = Vpte;

	SpinUnlock(&sVirtualPTEListLock);
}

VSC_API
NTSTATUS
MmCreateGuestMappingRange(
	_Inout_ PMM_PTE Pml4,
	_In_ PVOID Address,
	_In_ SIZE_T Size
)
/*++
Routine Description:
	This function allocates as many empty mappings as it takes to create enough PTEs to cover `Size`, and fills them
	into a linked list the VMM can allocate from
--*/
{
	if (!NT_SUCCESS(MmAllocateVpteList((Size + PAGE_SIZE - 1) / PAGE_SIZE)))
		return STATUS_INSUFFICIENT_RESOURCES;

	SIZE_T SizeMapped = 0;
	while (Size > SizeMapped)
	{
		X86_LA48 LinearAddr = {
			.Value = (UINT64)Address + SizeMapped
		};

		PMM_PTE Pml4e = &Pml4[LinearAddr.Pml4Index];

		PMM_PTE Pdpte = NULL;
		if (!Pml4e->Present)
		{
			Pml4e->Present = TRUE;
			Pml4e->WriteAllowed = TRUE;
			Pml4e->Accessed = TRUE;
			Pml4e->Dirty = TRUE;

			PMM_PTE Pdpt = NULL;
			if (!NT_SUCCESS(MmAllocateHostPageTable(&Pdpt)))
			{
				ImpDebugPrint("Couldn't allocate host PDPT for '%llX'...\n", Address);
				return STATUS_INSUFFICIENT_RESOURCES;
			}

			Pdpte = &Pdpt[LinearAddr.PdptIndex];

			Pml4e->PageFrameNumber = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(Pdpt));
		}
		else
			Pdpte = MmReadHostPageTableEntry(Pml4e->PageFrameNumber, LinearAddr.PdptIndex);

		PMM_PTE Pde = NULL;
		if (!Pdpte->Present)
		{
			Pdpte->Present = TRUE;
			Pdpte->WriteAllowed = TRUE;
			Pdpte->Accessed = TRUE;
			Pdpte->Dirty = TRUE;

			PMM_PTE Pd = NULL;
			if (!NT_SUCCESS(MmAllocateHostPageTable(&Pd)))
			{
				ImpDebugPrint("Couldn't allocate host PD for '%llX'...\n", Address);
				return STATUS_INSUFFICIENT_RESOURCES;
			}

			Pde = &Pd[LinearAddr.PdIndex];
			Pdpte->PageFrameNumber = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(Pd));
		}
		else
			Pde = MmReadHostPageTableEntry(Pdpte->PageFrameNumber, LinearAddr.PdIndex);

		PMM_PTE Pte = NULL;
		if (!Pde->Present)
		{
			Pde->Present = TRUE;
			Pde->WriteAllowed = TRUE;
			Pde->Accessed = TRUE;
			Pde->Dirty = TRUE;

			PMM_PTE Pt = NULL;
			if (!NT_SUCCESS(MmAllocateHostPageTable(&Pt)))
			{
				ImpDebugPrint("Couldn't allocate host PD for '%llX'...\n", Address);
				return STATUS_INSUFFICIENT_RESOURCES;
			}

			Pte = &Pt[LinearAddr.PtIndex];
			Pde->PageFrameNumber = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(Pt));
		}
		else
			Pte = MmReadHostPageTableEntry(Pde->PageFrameNumber, LinearAddr.PtIndex);

		if (!Pte->Present)
		{
			Pte->Present = TRUE;
			Pte->WriteAllowed = TRUE;
			Pte->Accessed = TRUE;
			Pte->Dirty = TRUE;
		}

		PMM_VPTE Vpte = NULL;
		if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
			return STATUS_INSUFFICIENT_RESOURCES;

		Vpte->Pte = Pte;
		Vpte->MappedAddr = (PVOID)LinearAddr.Value;

		SizeMapped += PAGE_SIZE;
	}

	// Reset VPTE head to the beginning
	gVirtualPTEHead = sVirtualPTEListRaw;

	return STATUS_SUCCESS;
}

VSC_API
NTSTATUS
MmCopyAddressTranslation(
	_Inout_ PMM_PTE Pml4,
	_In_ PVOID Address,
	_Inout_ PSIZE_T SizeMapped
)
/*++
Routine Description:
	This function copies an address translation from the current page tables to `Pml4`
--*/
{
	// TODO: Once fixed, don't blindly copy and create own translation instead

	MM_PTE Pml4e = { 0 }, Pdpte = { 0 }, Pde = { 0 }, Pte = { 0 };

	if (!NT_SUCCESS(MmWinTranslateAddrVerbose(Address, &Pml4e, &Pdpte, &Pde, &Pte)))
	{
		*SizeMapped += PAGE_SIZE;
		ImpDebugPrint("Failed to translate '%llX' in current page directory...\n", Address);
		return STATUS_INVALID_PARAMETER;
	}

	X86_LA48 LinearAddr = {
		.Value = (UINT64)Address
	};

	PMM_PTE HostPml4e = &Pml4[LinearAddr.Pml4Index];

	PMM_PTE HostPdpte = NULL;
	if (!HostPml4e->Present)
	{
		*HostPml4e = Pml4e;

		PMM_PTE HostPdpt = NULL;
		if (!NT_SUCCESS(MmAllocateHostPageTable(&HostPdpt)))
		{
			ImpDebugPrint("Couldn't allocate host PDPT for '%llX'...\n", Address);
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		HostPdpte = &HostPdpt[LinearAddr.PdptIndex];
		HostPml4e->PageFrameNumber = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(HostPdpt));
	}
	else
		HostPdpte = MmReadHostPageTableEntry(HostPml4e->PageFrameNumber, LinearAddr.PdptIndex);

	PMM_PTE HostPde = NULL;
	if (!HostPdpte->Present)
	{
		*HostPdpte = Pdpte;

		if (Pdpte.LargePage)
		{
			*SizeMapped += GB(1);

			return STATUS_SUCCESS;
		}

		PMM_PTE HostPd = NULL;
		if (!NT_SUCCESS(MmAllocateHostPageTable(&HostPd)))
		{
			ImpDebugPrint("Couldn't allocate host PD for '%llX'...\n", Address);
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		HostPde = &HostPd[LinearAddr.PdIndex];
		HostPdpte->PageFrameNumber = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(HostPd));
	}
	else
	{
		if (Pdpte.LargePage)
			return STATUS_SUCCESS;

		HostPde = MmReadHostPageTableEntry(HostPdpte->PageFrameNumber, LinearAddr.PdIndex);
	}

	PMM_PTE HostPte = NULL;
	if (!HostPde->Present)
	{
		*HostPde = Pde;

		if (Pde.LargePage)
		{
			*SizeMapped += MB(2);

			return STATUS_SUCCESS;
		}

		PMM_PTE HostPt = NULL;
		if (!NT_SUCCESS(MmAllocateHostPageTable(&HostPt)))
		{
			ImpDebugPrint("Couldn't allocate host PD for '%llX'...\n", Address);
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		HostPte = &HostPt[LinearAddr.PtIndex];
		HostPde->PageFrameNumber = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(HostPt));
	}
	else
	{
		if (Pde.LargePage)
			return STATUS_SUCCESS;

		HostPte = MmReadHostPageTableEntry(HostPde->PageFrameNumber, LinearAddr.PtIndex);
	}

	if (!HostPte->Present)
		*HostPte = Pte;

	*SizeMapped += PAGE_SIZE;

	return STATUS_SUCCESS;
}

#ifdef _DEBUG
typedef enum _MM_PT_LEVEL
{
	MM_PT_PTE = 0,
	MM_PT_PDE,
	MM_PT_PDPTE,
	MM_PT_PML4E
} MM_PT_LEVEL, * PMM_PT_LEVEL;

VSC_API
UINT64
MmInsertPtIndexToAddress(
	_In_ UINT64 Address,
	_In_ UINT64 Index,
	_In_ MM_PT_LEVEL Level
)
{
	return (Address & ~((~0ULL >> (64 - 9)) << (12 + 9 * Level))) | ((Index) << (12 + 9 * Level));
}

VSC_API
PVOID
MmMakeAddressCanonical(
	_In_ UINT64 Address
)
{
	return Address | (~0ULL << (12 + (9 * (MM_PT_PML4E + 1))));
}

VSC_API
VOID
MmIteratePageDirectory(
	_In_ PMM_PTE Table,
	_In_ MM_PT_LEVEL Level,
	_In_ UINT64 Address
)
{
	for (SIZE_T i = 0; i < 512; i++)
	{
		PMM_PTE Entry = &Table[i];

		if (!Entry->Present)
			continue;

		if (Level != MM_PT_PTE && !Entry->LargePage)
		{
			// Insert the current level index to the address
			Address = MmInsertPtIndexToAddress(Address, i, Level);

			// Move down to the next level
			MmIteratePageDirectory(MmGetHostPageTableVirtAddr(PAGE_ADDRESS(Entry->PageFrameNumber)), Level - 1, Address);
		}
		else
		{
			// Make the current PT indices into a canonical address. This is the address of a page containing an allocation
			PVOID CanonicalAddress = MmMakeAddressCanonical(MmInsertPtIndexToAddress(Address, i, Level));

			if (CanonicalAddress >= VPTE_BASE && CanonicalAddress < RVA_PTR(VPTE_BASE, PAGE_SIZE * 512))
				goto matched_vpte;

			PIMP_ALLOC_RECORD CurrRecord = gHostAllocationsHead;
			while (CurrRecord != NULL)
			{
				// Skip allocation records that are mapped into host memory
				if ((CurrRecord->Flags & (IMP_SHARED_ALLOCATION | IMP_HOST_ALLOCATION)) == 0)
					goto skip;

				if (PAGE_ALIGN(CurrRecord->Address) <= CanonicalAddress && RVA_PTR(CurrRecord->Address, CurrRecord->Size) >= CanonicalAddress)
					goto matched_iar;

			skip:
				CurrRecord = (PIMP_ALLOC_RECORD)CurrRecord->Records.Blink;
			}

			ImpDebugPrint("Translation with no entry... '%p'\n", CanonicalAddress);
			continue;

		matched_iar:
			ImpDebugPrint("Matched translation '%p' to IMP_ALLOC_RECORD %p\n", CanonicalAddress, CurrRecord);
			continue;

		matched_vpte:
			ImpDebugPrint("Matched translation '%p' to VPTE range\n", CanonicalAddress);
		}
	}
}
#endif

VSC_API
NTSTATUS
MmSetupHostPageDirectory(
	_Inout_ PMM_INFORMATION MmSupport
)
/*++
Routine Description:
	This function allocates a host page directory and copys mappings from the current page tables
	for allocations made by Imp* functions, and the VMM driver which is added separately to the host
	'allocations' list.
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	// Map 0x0000 to trap page?

	PMM_PTE HostPml4 = NULL;
	if (!NT_SUCCESS(MmAllocateHostPageTable(&HostPml4)))
	{
		ImpDebugPrint("Couldn't allocate host PML4...\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	MmSupport->Cr3.PageDirectoryBase = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(HostPml4));

#ifdef _DEBUG
	// TODO: Correct this in the future, Write function to extract section info
	// Organise the VMM's PE image sections before mapping/shadowing memory ranges once the VMM starts
	Status = MmPrepareVmmImageData(&__ImageBase);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to map VMM host data... (%x)\n", Status);
		return Status;
	}
#else
	// TODO: Implement mapping from shared memory block given on startup from client
	ImpDebugPrint("NOT IMPLEMENTED ...\n");
#endif

	// Allocate 512 pages to be used for arbitrary mapping
	Status = MmCreateGuestMappingRange(HostPml4, VPTE_BASE, PAGE_SIZE * 512);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Couldn't allocate guest mapping range...\n");
		return Status;
	}

	// Loop condition is not wrong, head is always the last one used, one is ignored at the end
	PIMP_ALLOC_RECORD CurrRecord = gHostAllocationsHead;
	while (CurrRecord != NULL)
	{
		// Only map allocations which need to be mapped
		if ((CurrRecord->Flags & (IMP_SHARED_ALLOCATION | IMP_HOST_ALLOCATION)) == 0)
			goto skip;

		SIZE_T SizeMapped = 0;
		while (CurrRecord->Size > SizeMapped)
		{
			Status = MmCopyAddressTranslation(HostPml4, RVA_PTR(CurrRecord->Address, SizeMapped), &SizeMapped);
			if (!NT_SUCCESS(Status))
			{
				ImpDebugPrint("Failed to copy translation for '%llX'...\n", RVA_PTR(CurrRecord->Address, SizeMapped));
				return Status;
			}
		}

	skip:
		CurrRecord = (PIMP_ALLOC_RECORD)CurrRecord->Records.Blink;
	}

	return STATUS_SUCCESS;
}

VMM_API
VOID
MmMapGuestPhys(
	_Inout_ PMM_VPTE Vpte,
	_In_ UINT64 PhysAddr
)
/*++
Routine Description:
	This function maps a guest physical address to a VPTE
--*/
{
	Vpte->MappedPhysAddr = PhysAddr;
	Vpte->MappedVirtAddr = RVA_PTR(Vpte->MappedAddr, PAGE_OFFSET(PhysAddr));

	Vpte->Pte->Present = TRUE;
	Vpte->Pte->PageFrameNumber = PAGE_FRAME_NUMBER(PhysAddr);

	__invlpg(Vpte->MappedVirtAddr);
}

VMM_API
NTSTATUS
MmMapGuestVirt(
	_Inout_ PMM_VPTE Vpte,
	_In_ UINT64 TargetCr3,
	_In_ UINT64 VirtAddr
)
/*++
Routine Description:
	Maps a virtual address to `Vpte` by translating `VirtAddr` through `GuestCr3` and assigning `Vpte` the physical address obtained
--*/
{
	// TODO: Cut down on VPTE allocations during this call, can cause a lot of noise with CPUs trying to access MmAllocateVpte and MmFreeVpte
	NTSTATUS Status = STATUS_SUCCESS;

	X86_LA48 LinearAddr = {
		.Value = (UINT64)VirtAddr
	};

	X86_CR3 Cr3 = {
		.Value = TargetCr3
	};

	MM_PTE Pte = { 0 };

	Status = MmReadGuestPhys(PAGE_ADDRESS(Cr3.PageDirectoryBase) + sizeof(MM_PTE) * LinearAddr.Pml4Index, sizeof(MM_PTE), &Pte);
	if (!NT_SUCCESS(Status))
		return Status;

	if (!Pte.Present)
		return STATUS_INVALID_PARAMETER;

	Status = MmReadGuestPhys(PAGE_ADDRESS(Pte.PageFrameNumber) + sizeof(MM_PTE) * LinearAddr.PdptIndex, sizeof(MM_PTE), &Pte);
	if (!NT_SUCCESS(Status))
		return Status;

	if (!Pte.Present)
		return STATUS_INVALID_PARAMETER;

	Status = MmReadGuestPhys(PAGE_ADDRESS(Pte.PageFrameNumber) + sizeof(MM_PTE) * LinearAddr.PdIndex, sizeof(MM_PTE), &Pte);
	if (!NT_SUCCESS(Status))
		return Status;

	if (!Pte.Present)
		return STATUS_INVALID_PARAMETER;

	Status = MmReadGuestPhys(PAGE_ADDRESS(Pte.PageFrameNumber) + sizeof(MM_PTE) * LinearAddr.PtIndex, sizeof(MM_PTE), &Pte);
	if (!NT_SUCCESS(Status))
		return Status;

	if (!Pte.Present)
		return STATUS_INVALID_PARAMETER;

	MmMapGuestPhys(Vpte, PAGE_ADDRESS(Pte.PageFrameNumber) + PAGE_OFFSET(VirtAddr));

	return STATUS_SUCCESS;
}
