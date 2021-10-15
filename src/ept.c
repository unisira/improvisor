#include "improvisor.h"
#include "ept.h"
#include "vmx.h"
#include "mm.h"

#define PAGE_FRAME_NUMBER(Addr) ((UINT64)(Addr) >> 12)
#define PAGE_ADDRESS(Pfn) ((UINT64)(Pfn) << 12)

typedef struct _PHYSICAL_MEMORY_RANGE
{
    PHYSICAL_ADDRESS BaseAddress;
    LARGE_INTEGER NumberOfBytes;
} PHYSICAL_MEMORY_RANGE, *PPHYSICAL_MEMORY_RANGE;

NTKERNELAPI
PPHYSICAL_MEMORY_RANGE
MmGetPhysicalMemoryRanges(
    VOID
);

PEPT_PTE 
EptReadExistingPte(
    _In_ UINT64 TablePfn,
    _In_ SIZE_T Index
)
/*++
Routine Description:
    Maps a page table into memory using a page frame number `TablePfn` and reads the entry selected by 
    `Index` into `Entry`
--*/
{
    // EPT allocates host page tables first, scan from the tail forward.
    PMM_RESERVED_PT CurrPt = gHostPageTablesTail;
    while (CurrPt != NULL)
    {
        if (CurrPt->TablePhysAddr == PAGE_ADDRESS(TablePfn))
            return &((PEPT_PTE)CurrPt->TableAddr)[Index];

        CurrPt = (PMM_RESERVED_PT)CurrPt->Links.Flink;
    }

    // TODO: Should never happen, panic here
    return NULL;
}

NTSTATUS
EptIdentityMapMemoryRange(
    _In_ PEPT_PTE Pml4,
    _In_ UINT64 PhysAddr,
    _In_ UINT64 Size,
    _In_ UINT16 Permissions
)
/*++
Routine Description:
    Identity maps a block of physical memory, this function works in VMX-root mode, it doesn't call any
    Windows API functions
--*/
{
    SIZE_T SizeMapped = 0;
    while (Size > SizeMapped)
    {
        EPT_GPA Gpa = {
            .Value = PhysAddr + SizeMapped;
        };

        PEPT_PTE Pml4e = &Pml4[Gpa.Pml4Index];

        PEPT_PTE Pdpte = NULL;
        if (!Pml4e->Present)
        {
            PEPT_PTE Pdpt = NULL;
            if (!NT_SUCCESS(MmAllocateHostPageTable(&Pdpt)))
            {
                ImpDebugPrint("Couldn't allocate EPT PDPT for '%llx'...\n", PhysAddr + SizeMapped);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Pdpte = &Pdpt[Gpa.PdptIndex];

            Pml4e->Present = TRUE;

            // TODO: Define and apply permissions properly from `Permissions`
            Pml4e->ReadAccess = TRUE;
            Pml4e->WriteAccess = TRUE;
            Pml4e->ExecuteAccess = TRUE;

            // TODO: Define MmGetLastAllocatedPageTableEntry
            Pml4e->PageFrameNumber = 
                PAGE_FRAME_NUMBER(((PMM_RESERVED_PT)gHostPageTablesHead->Links.Blink)->TablePhysAddr);
        }
        else
            Pdpte = EptReadExistingPte(Pml4e->PageFrameNumber, Gpa.PdptIndex);

        PEPT_PTE Pde = NULL;
        if (!Pdpte->Present)
        {
            PEPT_PTE Pd = NULL;
            if (!NT_SUCCESS(MmAllocateHostPageTable(&Pd)))
            {
                ImpDebugPrint("Couldn't allocate EPT PD for '%llx'...\n", PhysAddr + SizeMapped);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Pde = &Pd[Gpa.PdIndex];

            Pdpte->Present = TRUE;

            // TODO: Define and apply permissions properly from `Permissions`
            Pdpte->ReadAccess = TRUE;
            Pdpte->WriteAccess = TRUE;
            Pdpte->ExecuteAccess = TRUE;

            Pdpte->PageFrameNumber = 
                PAGE_FRAME_NUMBER(((PMM_RESERVED_PT)gHostPageTablesHead->Links.Blink)->TablePhysAddr);
        }
        else
            Pde = EptReadExistingPte(Pdpte->PageFrameNumber, Gpa.PdIndex);

        PEPT_PTE Pte = NULL;
        if (!Pde->Present)
        {
            PEPT_PTE Pt = NULL;
            if (!NT_SUCCESS(MmAllocateHostPageTable(&Pt)))
            {
                ImpDebugPrint("Couldn't allocate EPT PT for '%llx'...\n", PhysAddr + SizeMapped);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Pte = &Pt[Gpa.PtIndex];

            Pdpte->Present = TRUE;

            // TODO: Define and apply permissions properly from `Permissions`
            Pde->ReadAccess = TRUE;
            Pde->WriteAccess = TRUE;
            Pde->ExecuteAccess = TRUE;

            Pde->PageFrameNumber = 
                PAGE_FRAME_NUMBER(((PMM_RESERVED_PT)gHostPageTablesHead->Links.Blink)->TablePhysAddr);
        }
        else
            Pte = EptReadExistingPte(Pde->PageFrameNumber, Gpa.PtIndex);

        Pte->ReadAccess = TRUE;
        Pte->WriteAccess = TRUE;
        Pte->ExecuteAccess = TRUE;

        Pte->PageFrameNumber = PAGE_FRAME_NUMBER(PhysAddr);

        SizeMapped += PAGE_SIZE;
    }

}

NTSTATUS
EptSetupIdentityMap(
    _In_ PEPT_PTE Pml4
)
/*++
Routine Description:
    This function iterates over all memory ranges and identity maps them, 
--*/
{
    // NOTES:
    // 1. This function should allocate its host page tables from the memory manager,
    //    If EPT was to have its own it would be an exact repeat of the previous code so
    //
    // 2. This function will take care of hiding data from ANY allocations made by the hypervisor

    NTSTATUS Status = STATUS_SUCCESS;

    // TODO: Dont use this, use MTRR
    PPHYSICAL_MEMORY_RANGE Range = MmGetPhysicalMemoryRanges();
    while (Range != NULL)
    {
        Status = EptIdentityMapMemoryRange(Pml4, Range->BaseAddress.QuadPart, Range->NumberOfBytes.QuadPart);
        if (!NT_SUCCESS(Status))
            return Status;

        Range++;
    }

    return Status;
}

NTSTATUS
EptInitialise(VOID)
/*++
Routine Description:
    This function initialises the EPT identity map and hooking capabilities. This function should be called
    before other Mm* functions which allocate page tables to improve performance
--*/
{
    // TODO: Free stuff
    NTSTATUS Status = STATUS_SUCCESS;

    PEPT_PTE Pml4 = NULL;
    if (!NT_SUCCESS(MmAllocateHostPageTable(&Pml4)))
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = EptSetupIdentityMap(Pml4); 
    if (!NT_SUCCESS(Status))
        return Status;

    return Status;
}
