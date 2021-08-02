#include "improvisor.h"
#include "arch/memory.h"
#include "arch/cr.h"
#include "mm.h"

/* TODO:
MM_PTE, PDE, PDPTE, PML4E definitions
*/

#define PAGE_FRAME_NUMBER(Addr) ((UINT64)(Addr) >> 12)
#define PAGE_ADDRESS(Pfn) ((UINT64)(Pfn) << 12)

typedef struct _MM_RESERVED_PT
{
    LIST_ENTRY Links;
    PVOID TableAddr;
} MM_RESERVED_PT, *PMM_RESERVED_PT;

PMM_RESERVED_PT gHostPageTablesHead = NULL;

// The raw array of page tables, each comprising of 512 possible entries
static PVOID sPageTableListRaw = NULL;
// The raw list of page table entries for the linked list
static PMM_RESERVED_PT sPageTableListEntries = NULL;

NTSTATUS
MmWinMapPhysicalMemory(
    _In_ UINT64 PhysAddr,
    _In_ SIZE_T Size,
    _Out_ PVOID* pVirtAddr
)
/*++
Routine Description:
    Maps a physical address into memory using Windows' memory manager.
--*/
{
    PHYSICAL_ADDRESS Addr = {
        .QuadPart = PhysAddr
    };

    PVOID Address = MmMapIoSpace(Addr, Size, MmNonCached);

    if (Address == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    *pVirtAddr = Address;

    return STATUS_SUCCESS;
}

NTSTATUS
MmWinTranslateAddrVerbose(
    _In_ PVOID Address,
    _Out_ PMM_PML4E pPml4e,
    _Out_ PMM_PDPTE pPdpte,
    _Out_ PMM_PDE pPde,
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

    PMM_PML4E Pml4 = NULL;
    PMM_PDPTE Pdpt = NULL;
    PMM_PDE Pd = NULL;
    PMM_PTE Pt = NULL;

    X86_LA48 LinearAddr = {
        .Value = Address
    };
    
    Status = MmWinMapPhysicalMemory(PAGE_ADDRESS(Cr3.PageDirectoryBase), PAGE_SIZE, &Pml4);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to map current PML4...\n");
        return Status;
    }

    MM_PML4E Pml4e = Pml4[LinearAddr.Pml4Index];

    MmUnmapIoSpace(Pml4, PAGE_SIZE);

    if (!Pml4e.Present)
    {
        ImpDebugPrint("PML4E for '%llX' is invalid...\n", Address);
        return STATUS_INVALID_PARAMETER;
    }

    *pPml4e = Pml4e;
    
    Status = MmWinMapPhysicalMemory(PAGE_ADDRESS(Pml4e.Pdpt), PAGE_SIZE, &Pdpt);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to map PDPT for '%llX'...\n", Address);
        return Status; 
    }
    
    MM_PDPTE Pdpte = Pdpt[LinearAddr.PdptIndex];

    MmUnmapIoSpace(Pdpt, PAGE_SIZE);

    if (!Pdpte.Present)
    {
        ImpDebugPrint("PDPTE for '%llX' is invalid...\n", Address);
        return STATUS_INVALID_PARAMETER;
    }

    *pPdpte = Pdpte;

    Status = MmWinMapPhysicalMemory(PAGE_ADRESS(Pdpte.Pd), PAGE_SIZE, &Pd);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to map PD for '%llX'...\n", Address);
        return Status;
    }

    MM_PDE Pde = Pd[LinearAddr.PdIndex];

    MmUnmapIoSpace(Pd, PAGE_SIZE);

    if (!Pde.Present)
    {
        ImpDebugPrint("PDE for '%llX' is invalid...\n", Address);
        return STATUS_INVALID_PARAMETER;
    }

    *pPde = Pde;

    Status = MmWinMapPhysicalMemory(PAGE_ADDRESS(Pde.Pt), PAGE_SIZE, &Pt);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to map PT for '%llX'...\n");
        return Status;
    }
    
    MM_PTE Pte = Pt[LinearAddr.PtIndex];

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

    sPageTableListRaw = ImpAllocateContiguousMemory(PAGE_SIZE * Count);
    if (sPageTableListRaw == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto panic;
    }

    sPageTableListEntries = ImpAllocateNpPool(sizeof(MM_RESERVED_PT) * Count);
    if (sPageTableListEntries == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto panic;
    }

    // Setup the head as the first entry in this array
    gHostPageTablesHead = sPageTableListRaw;

    for (SIZE_T i = 0; i < Count; i++)
    {
        PMM_RESERVED_PT CurrTable = sPageTableListEntries + i;

        CurrTable->TableAddr = (PCHAR)sPageTableListRaw + (i * PAGE_SIZE);

        CurrTable->Links.Flink = i < Count  ? &(CurrTable + 1)->Links : NULL;
        CurrTable->Links.Blink = i > 0      ? &(CurrTable - 1)->Links : NULL;
    }

panic:
    if (sPageTableListRaw)
        MmFreeContiguousMemory(sPageTableListRaw);
    if (sPageTableListEntries)
        ExFreePoolWithTag(sPageTableListEntries, POOL_TAG);

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
MmCopyAddressTranslation(
    _Inout_ PMM_PML4E Pml4,
    _In_ PVOID Address,
    _In_ SIZE_T Size,
)
/*++
Routine Description:
    This function copies an address translation from the current page tables to `Pml4` 
--*/
{
    if (!NT_SUCCESS(MmWinTranslateAddrVerbose(Address, &Pml4e, &Pdpte, &Pde, &Pte)))
    {
        ImpDebugPrint("Failed to translate '%llX' in current page directory...\n", Address);
        return STATUS_INVALID_PARAMETER;
    }

    X86_LA48 LinearAddr = {
        .Value = Address
    };

    PMM_PML4E HostPml4e = &Pml4[LinearAddr.Pml4Index];

    PMM_PDPTE HostPdpte = NULL;
    if (!HostPml4e->Present)
    {
        PMM_PDPTE HostPdpt = NULL;
        if (!NT_SUCCESS(MmAllocateHostPageTable(&HostPdpt)))
        {
            ImpDebugPrint("Couldn't allocate host PDPT for '%llX'...\n", Address);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        HostPdpte = &HostPdpt[LinearAddr.PdptIndex];

        *HostPml4e = Pml4e;
        HostPml4e->Pdpt = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(HostPdpt));
    }
    else
    {
        PMM_PDPTE HostPdpt = NULL;
        if (!NT_SUCCESS(MmWinMapPhysicalMemory(PAGE_ADDRESS(HostPml4e->Pdpt), PAGE_SIZE, &HostPdpt)))
        {
            ImpDebugPrint("Couldn't map existing PDPT into memory for '%llX'...\n", Address);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        HostPdpte = &HostPdpt[LinearAddr.PdptIndex];

        MmUnmapIoSpace(HostPdpt, PAGE_SIZE);
    }

    PMM_PDE HostPde = NULL
    if (!HostPdpte->Present)
    {
        PMM_PDPTE HostPdpt = NULL;
        if (!NT_SUCCESS(MmAllocateHostPageTable(&HostPd)))
        {
            ImpDebugPrint("Couldn't allocate host PD for '%llX'...\n", Address);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        HostPde = &HostPd[LinearAddr.PdIndex];

        *HostPdpte = Pdpte;
        HostPdpte->Pd = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(HostPd));
    }
    else
    {
        PMM_PDPTE HostPdpt = NULL;
        if (!NT_SUCCESS(MmWinMapPhysicalMemory(PAGE_ADDRESS(HostPdpte->Pd), PAGE_SIZE, &HostPdpt)))
        {
            ImpDebugPrint("Couldn't map existing PD into memory for '%llX'...\n", Address);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        HostPde = &HostPd[LinearAddr.PdIndex];

        MmUnmapIoSpace(HostPd, PAGE_SIZE);
    }

    PMM_PDE HostPte = NULL
    if (!HostPde->Present)
    {
        PMM_PDPTE HostPdpt = NULL;
        if (!NT_SUCCESS(MmAllocateHostPageTable(&HostPt)))
        {
            ImpDebugPrint("Couldn't allocate host PD for '%llX'...\n", CurrRecord->Address);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        HostPte = HostPt[LinearAddr.PtIndex];

        *HostPde = Pde;
        HostPde->Pt = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(HostPt));
    }
    else
    {
        PMM_PDPTE HostPdpt = NULL;
        if (!NT_SUCCESS(MmWinMapPhysicalMemory(PAGE_ADDRESS(HostPde->Pt), PAGE_SIZE, &HostPt)))
        {
            ImpDebugPrint("Couldn't map existing PD into memory for '%llX'...\n", CurrRecord->Address);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        HostPte = &HostPt[LinearAddr.PdIndex];

        MmUnmapIoSpace(HostPt, PAGE_SIZE);
    }

    if (!HostPte->Present)
        *HostPte = Pte;

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

    PMM_PML4E HostPml4 = NULL;
    if (!NT_SUCCESS(MmAllocateHostPageTable(&HostPml4)))
    {
        ImpDebugPrint("Couldn't allocate host PML4...\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    MmSupport->HostDirectoryPhysical = ImpGetPhysicalAddress(HostPml4);

    PIMP_ALLOC_RECORD CurrRecord = gImpHostAllocationsHead;
    while (CurrRecord->Records.Blink != NULL)
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

            SizeMapped += PAGE_SIZE
        } 

        CurrRecord = (PIMP_ALLOC_RECORD)CurrRecord->Records.Blink;
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
    Status = MmReserveHostPageTables(1000);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to reserve page tables for the host...\n");
        return Status;
    }

    return Status;
}

