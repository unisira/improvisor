#include "improvisor.h"
#include "util/spinlock.h"
#include "arch/memory.h"
#include "arch/cr.h"
#include "mm.h"

#define VPTE_BASE (0xfffffc9ec0000000)

#define BASE_POOL_CHUNK_SIZE (512)
#define CHUNK_ADDR(Hdr) ((PVOID)((ULONG_PTR)Hdr + sizeof(MM_POOL_CHUNK_HDR)))
#define CHUNK_HDR(Chunk) ((PMM_POOL_CHUNK_HDR)((ULONG_PTR)Chunk - sizeof(MM_POOL_CHUNK_HDR)))

typedef struct _MM_RESERVED_PT
{
    LIST_ENTRY Links;
    PVOID TableAddr;
    UINT64 TablePhysAddr;
} MM_RESERVED_PT, *PMM_RESERVED_PT;

// TODO: For host page tables, include MM_RESERVED_PT header in the raw PT list allocation
PMM_RESERVED_PT gHostPageTablesHead = NULL;
PMM_RESERVED_PT gHostPageTablesTail = NULL;

PMM_VPTE gVirtualPTEHead = NULL;

// The raw array of page tables, each comprising of 512 possible entries
static PVOID sPageTableListRaw = NULL;
// The raw list of page table entries for the linked list
static PMM_RESERVED_PT sPageTableListEntries = NULL;

// The raw list of virtual PTE list entries
static PMM_VPTE sVirtualPTEListRaw = NULL;
static SPINLOCK sVirtualPTEListLock;

// TODO: Move away from use of NTSTATUS for non-setup / windows related functions

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
        CurrTable->TablePhysAddr = MmGetPhysicalAddress(CurrTable->TableAddr);

        CurrTable->Links.Flink = i < Count  ? &(CurrTable + 1)->Links : NULL;
        CurrTable->Links.Blink = i > 0      ? &(CurrTable - 1)->Links : NULL;
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

NTSTATUS
MmAllocateHostPageTable(
    _Out_ PVOID* pTable
)
/*++
Routine Description:
    Allocates a host page table from the linked list and returns it
--*/
{
    PMM_RESERVED_PT ReservedPt = gHostPageTablesHead;
    if (ReservedPt == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    *pTable = ReservedPt->TableAddr;

    gHostPageTablesHead = (PMM_RESERVED_PT)ReservedPt->Links.Flink;

    return STATUS_SUCCESS;
}

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
    sVirtualPTEListRaw = ImpAllocateHostNpPool(sizeof(MM_VPTE) * Count);
    if (sPageTableListEntries == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    gVirtualPTEHead = sVirtualPTEListRaw;

    for (SIZE_T i = 0; i < Count; i++)
    {
        PMM_VPTE CurrVpte = &sVirtualPTEListRaw[i];

        CurrVpte->Links.Flink = i < Count   ? &(CurrVpte + 1)->Links : NULL;
        CurrVpte->Links.Blink = i > 0       ? &(CurrVpte - 1)->Links : NULL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MmAllocateVpte(
    _Out_ PMM_VPTE* pVpte
)
/*++
Routine Description:
    This function takes a VPTE entry from the head of the list.
--*/
{
    // TODO: Make this not waste an entry ?? idk how
    SpinLock(&sVirtualPTEListLock);

    if (gVirtualPTEHead->Links.Flink == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    *pVpte = gVirtualPTEHead;

    gVirtualPTEHead = (PMM_VPTE)gVirtualPTEHead->Links.Flink;

    SpinUnlock(&sVirtualPTEListLock);

    return STATUS_SUCCESS;
}

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

    PMM_VPTE HeadVpte = gVirtualPTEHead;
   
    Vpte->MappedPhysAddr = 0;
    Vpte->MappedAddr = NULL;
    Vpte->MappedVirtAddr = NULL;

    // Remove this entry from its current position
    PMM_VPTE NextVpte = (PMM_VPTE)Vpte->Links.Flink;
    PMM_VPTE PrevVpte = (PMM_VPTE)Vpte->Links.Blink;
    if (NextVpte != NULL)
        NextVpte->Links.Blink = &PrevVpte->Links;
    if (PrevVpte != NULL)
        PrevVpte->Links.Flink = &NextVpte->Links;

    gVirtualPTEHead = Vpte;
    // Update the new head entry with the old head entry's forward link
    Vpte->Links.Flink = HeadVpte->Links.Flink;
    // Update the old head entry's forward link to the new head entry
    HeadVpte->Links.Flink = &Vpte->Links;
    // Set the new head entry's backwards link to the old head entry
    Vpte->Links.Blink = &HeadVpte->Links;

    SpinUnlock(&sVirtualPTEListLock);
}

NTSTATUS
MmCopyAddressTranslation(
    _Inout_ PMM_PTE Pml4,
    _In_ PVOID Address
)
/*++
Routine Description:
    This function copies an address translation from the current page tables to `Pml4` 
--*/
{
    MM_PTE Pml4e = {0}, Pdpte = {0}, Pde = {0}, Pte = {0};
	
    if (!NT_SUCCESS(MmWinTranslateAddrVerbose(Address, &Pml4e, &Pdpte, &Pde, &Pte)))
    {
        ImpDebugPrint("Failed to translate '%llX' in current page directory...\n", Address);
        return STATUS_INVALID_PARAMETER;
    }

    X86_LA48 LinearAddr = {
        .Value = Address
    };

    PMM_PTE HostPml4e = &Pml4[LinearAddr.Pml4Index];

    PMM_PTE HostPdpte = NULL;
    if (!HostPml4e->Present)
    {
        PMM_PTE HostPdpt = NULL;
        if (!NT_SUCCESS(MmAllocateHostPageTable(&HostPdpt)))
        {
            ImpDebugPrint("Couldn't allocate host PDPT for '%llX'...\n", Address);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        HostPdpte = &HostPdpt[LinearAddr.PdptIndex];

        *HostPml4e = Pml4e;
        HostPml4e->PageFrameNumber = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(HostPdpt));
    }
    else
        HostPdpte = MmWinReadPageTableEntry(HostPml4e->PageFrameNumber, LinearAddr.PdptIndex);

    PMM_PTE HostPde = NULL;
    if (!HostPdpte->Present)
    {
        *HostPdpte = Pdpte;

        if (Pdpte.LargePage)
            return STATUS_SUCCESS;

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

        HostPde = MmWinReadPageTableEntry(HostPdpte->PageFrameNumber, LinearAddr.PdIndex);
    }

    PMM_PTE HostPte = NULL;
    if (!HostPde->Present)
    {
        *HostPde = Pde;

        if (Pde.LargePage)
            return STATUS_SUCCESS;

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

        HostPte = MmWinReadPageTableEntry(HostPde->PageFrameNumber, LinearAddr.PtIndex);
    }

    if (!HostPte->Present)
        *HostPte = Pte;

    return STATUS_SUCCESS;
}

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
            Pdpte = MmWinReadPageTableEntry(Pml4e->PageFrameNumber, LinearAddr.PdptIndex);

        PMM_PTE Pde = NULL;
        if (!Pdpte->Present)
        {
            Pdpte->Present = TRUE;
            Pdpte->WriteAllowed = TRUE;

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
            Pde = MmWinReadPageTableEntry(Pdpte->PageFrameNumber, LinearAddr.PdIndex);

        PMM_PTE Pte = NULL;
        if (!Pde->Present)
        {
            Pde->Present = TRUE;
            Pde->WriteAllowed = TRUE;

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
            Pte = MmWinReadPageTableEntry(Pde->PageFrameNumber, LinearAddr.PtIndex);

        if (!Pte->Present)
        {
            Pte->Present = TRUE;
            Pte->WriteAllowed = TRUE;
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

NTSTATUS
MmSetupHostPageDirectory(
    _Inout_ PMM_SUPPORT MmSupport
)
/*++
Routine Description:
    This function allocates a host page directory and copys mappings from the current page tables
    for allocations made by Imp* functions, and the VMM driver which is added separately to the host 
    'allocations' list. 
--*/
{
    NTSTATUS Status = STATUS_SUCCESS;

    PMM_PTE HostPml4 = NULL;
    if (!NT_SUCCESS(MmAllocateHostPageTable(&HostPml4)))
    {
        ImpDebugPrint("Couldn't allocate host PML4...\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    MmSupport->HostDirectoryPhysical = ImpGetPhysicalAddress(HostPml4);

    // TODO: Think about if that while loop condition is wrong, possibly missing the first after the refactor
    PIMP_ALLOC_RECORD CurrRecord = (PIMP_ALLOC_RECORD)gHostAllocationsHead;
    while (CurrRecord != NULL)
    {
        SIZE_T SizeMapped = 0;
        while (CurrRecord->Size > SizeMapped)
        {
            Status = MmCopyAddressTranslation(HostPml4, (PCHAR)CurrRecord->Address + SizeMapped);
            if (!NT_SUCCESS(Status))
            {
                ImpDebugPrint("Failed to copy translation for '%llX'...\n", CurrRecord->Address);
                return Status;
            }

            SizeMapped += PAGE_SIZE;
        } 

        CurrRecord = (PIMP_ALLOC_RECORD)CurrRecord->Records.Blink;
    }
 
    // Allocate 512 pages to be used for arbitrary mapping
    Status = MmCreateGuestMappingRange(HostPml4, VPTE_BASE, PAGE_SIZE * 512);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Couldn't allocate guest mapping range...\n");
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MmInitialise(
    _Inout_ PMM_SUPPORT MmSupport
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    // Reserve 1000 page tables for the host to create its own page tables and still
    // have access to them post-VMLAUNCH
    Status = MmHostReservePageTables(1000);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to reserve page tables for the host...\n");
        return Status;
    }

    Status = MmSetupHostPageDirectory(MmSupport);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to setup the host page directory... (%X)\n", Status);
        return Status;
    }

    // TODO: Pool allocator

    return Status;
}

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
    Vpte->MappedVirtAddr = (PVOID)((ULONG_PTR)Vpte->MappedAddr + PhysAddr & 0xFFF);
    Vpte->Pte->PageFrameNumber = PAGE_FRAME_NUMBER(PhysAddr);
}

NTSTATUS
MmReadGuestPhys(
    _In_ UINT64 PhysAddr,
    _In_ SIZE_T Size,
    _Inout_ PVOID Buffer
)
/*++
Routine Description:
    This function reads physical memory by using one of the VPTEs from the guest mapping range
--*/
{
    // TODO: Cache VPTE translations
    PMM_VPTE Vpte = NULL;
    if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
        return STATUS_INSUFFICIENT_RESOURCES;

    SIZE_T SizeRead = 0;
    while (Size > SizeRead)
    {
        MmMapGuestPhys(Vpte, PhysAddr + SizeRead);

        SIZE_T SizeToRead = Size - SizeRead > PAGE_SIZE ? PAGE_SIZE : Size - SizeRead;
        RtlCopyMemory((PCHAR)Buffer + SizeRead, Vpte->MappedVirtAddr, SizeToRead);

        SizeRead += PAGE_SIZE;
    }

    MmFreeVpte(Vpte);

    return STATUS_SUCCESS;
}

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
        MmMapGuestPhys(&Vpte, PhysAddr + SizeWritten);

        SIZE_T SizeToWrite = Size - SizeWritten > PAGE_SIZE ? PAGE_SIZE : Size - SizeWritten;
        RtlCopyMemory(Vpte->MappedVirtAddr, (PCHAR)Buffer + SizeWritten, SizeToWrite);

        SizeWritten += PAGE_SIZE;
    }

    MmFreeVpte(Vpte);

    return STATUS_SUCCESS;
}

NTSTATUS
MmMapGuestVirt(
    _Inout_ PMM_VPTE Vpte,
    _In_ UINT64 GuestCr3,
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
        .Value = GuestCr3
    };

    MM_PTE Pte = {0};

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

    // TODO: Store this PTE's physaddr so we can read it inside of MmReadGuestVirt

    if (!Pte.Present)
        return STATUS_INVALID_PARAMETER;

    MmMapGuestPhys(Vpte, PAGE_ADDRESS(Pte.PageFrameNumber) + VirtAddr & 0xFFF);

    return STATUS_SUCCESS;
}

NTSTATUS
MmReadGuestVirt(
    _In_ UINT64 GuestCr3,
    _In_ UINT64 VirtAddr,
    _In_ SIZE_T Size,
    _Inout_ PVOID Buffer
)
/*++
Routine Description:
    Maps a virtual address using the translation in `GuestCr3` and reads `Size` bytes into `Buffer`. Size must be <= PAGE_SIZE
--*/
{
    if (Size >= PAGE_SIZE)
        return STATUS_INVALID_BUFFER_SIZE;

    PMM_VPTE Vpte = NULL;
    if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
        return STATUS_INSUFFICIENT_RESOURCES;

    if (!NT_SUCCESS(MmMapGuestVirt(Vpte, GuestCr3, VirtAddr)))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(Buffer, Vpte->MappedVirtAddr, Size);

    MmFreeVpte(Vpte);

    return STATUS_SUCCESS;
}

NTSTATUS
MmWriteGuestVirt(
    _In_ UINT64 GuestCr3,
    _In_ UINT64 VirtAddr,
    _In_ SIZE_T Size,
    _In_ PVOID Buffer
)
/*++
Routine Description:
    Maps a virtual address using the translation in `GuestCr3` and writes `Buffer` to it. Size must be <= PAGE_SIZE
--*/
{
    if (Size >= PAGE_SIZE)
        return STATUS_INVALID_BUFFER_SIZE;

    PMM_VPTE Vpte = NULL;
    if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
        return STATUS_INSUFFICIENT_RESOURCES;
    
    if (!NT_SUCCESS(MmMapGuestVirt(&Vpte, GuestCr3, VirtAddr)))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(Vpte->MappedVirtAddr, Buffer, Size);

    MmFreeVpte(Vpte);

    return STATUS_SUCCESS;
}
