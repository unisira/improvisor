#include "improvisor.h"
#include "util/macro.h"
#include "arch/memory.h"
#include "arch/mtrr.h"
#include "arch/msr.h"
#include "section.h"
#include "intrin.h"
#include "mtrr.h"
#include "ept.h"
#include "vmx.h"
#include "mm.h"

// Mask for when bits 12-29 need to be masked off for final translations
#define EPT_PTE_PHYSADDR_MASK XBITRANGE(12, 29)

VMM_API
BOOLEAN
EptCheckSuperPageSupport(VOID)
/*++
Routine Description:
    Checks if the current CPU supports EPT super PDPTEs for mapping 1GB regions at a time
--*/
{
    IA32_VMX_EPT_VPID_CAP_MSR EptVpidCap = {
        .Value = __readmsr(IA32_VMX_EPT_VPID_CAP)
    };

    return EptVpidCap.SuperPdpteSupport != 0;
}

VMM_API
BOOLEAN
EptCheckLargePageSupport(VOID)
/*++
Routine Description:
    Checks if the current CPU supports EPT large PDEs for mapping 2MB regions at a time
--*/
{
    IA32_VMX_EPT_VPID_CAP_MSR EptVpidCap = {
        .Value = __readmsr(IA32_VMX_EPT_VPID_CAP)
    };

    return EptVpidCap.LargePdeSupport != 0;
}

VMM_API
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

VMM_API
VOID
EptApplyPermissions(
    _In_ PEPT_PTE Pte,
    _In_ EPT_PAGE_PERMISSIONS Permissions
)
/*++
Routine Description:
    This function applies read, write and execute permissions to `Pte` based on the value of the bitmask `Permissions` 
--*/
{
    Pte->ReadAccess = (Permissions & EPT_PAGE_READ) != 0;
    Pte->WriteAccess = (Permissions & EPT_PAGE_WRITE) != 0;
    Pte->ExecuteAccess = (Permissions & EPT_PAGE_EXECUTE) != 0;
    Pte->UserExecuteAccess = (Permissions & EPT_PAGE_UEXECUTE) != 0;
}

VMM_API
NTSTATUS
EptMapLargeMemoryRange(
    _In_ PEPT_PTE Pde,
    _In_ UINT64 GuestPhysAddr,
    _In_ UINT64 PhysAddr,
    _In_ UINT64 Size,
    _In_ EPT_PAGE_PERMISSIONS Permissions
)
/*++
Routine Description:
    Converts `Pde` to a large PDPTE, mapping a 2MB region from `PhysAddr`
--*/
{
    // Make sure this memory region meets the requirements for super page mapping
    // 
    // 1. Super page technology must be supported on the current CPU
    // 2. The PhysAddr and GuestPhysAddr we are attempting to map must be 2MB aligned
    // 3. The size of the region we are mapping must be greater than 2MB
    // 4. The MTRR region we are trying to map must have more than 2MB remaining
    if (!EptCheckLargePageSupport() || Size < MB(2) || (PhysAddr & 0x1FFFFF) != 0 || (GuestPhysAddr & 0x1FFFFF) != 0 || MtrrGetRegionEnd(PhysAddr) - PhysAddr < MB(2))
        return STATUS_INVALID_PARAMETER;

    Pde->Present = TRUE;
    Pde->LargePage = TRUE;

    Pde->PageFrameNumber = PAGE_FRAME_NUMBER(PhysAddr);
    Pde->MemoryType = MtrrGetRegionType(PhysAddr);

    EptApplyPermissions(Pde, Permissions);

    return STATUS_SUCCESS;
}

VMM_API
NTSTATUS
EptSubvertLargePage(
    _In_ PEPT_PTE Pde,
    _In_ UINT64 GuestPhysAddr,
    _In_ UINT64 PhysAddr,
    _In_ EPT_PAGE_PERMISSIONS Permissions
)
/*++
Routine Description:
    Takes a Super PDE `Pde` and converts it into a normal PDPTE, mapping all necessary pages.
--*/
{
    static const sSize = MB(2);

    if (!Pde->LargePage)
        return STATUS_INVALID_PARAMETER;

    Pde->LargePage = FALSE;

    PEPT_PTE Pt = NULL;
    if (!NT_SUCCESS(MmAllocateHostPageTable(&Pt)))
    {
        ImpDebugPrint("Couldn't allocate EPT PD for '%llx' subversion...\n", GuestPhysAddr);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Pde->PageFrameNumber =
        PAGE_FRAME_NUMBER(MmGetLastAllocatedPageTableEntry()->TablePhysAddr);

    // Since we are subverting a large page, the MTRR region is constant throughout the whole memory range
    MEMORY_TYPE RegionType = MtrrGetRegionType(PhysAddr);

    SIZE_T SizeSubverted = 0;
    while (SizeSubverted < sSize)
    {
        EPT_GPA Gpa = {
            .Value = GuestPhysAddr + SizeSubverted
        };

        PEPT_PTE Pte = &Pt[Gpa.PtIndex];

        Pte->Present = TRUE;

        EptApplyPermissions(Pte, Permissions);

        Pte->PageFrameNumber = PAGE_FRAME_NUMBER(Gpa.Value);
        // Technically, doing PhysAddr + SizeSubverted is just pedantic because mapping a large PDE requires that it was within one MTRR region
        Pte->MemoryType = RegionType;

        SizeSubverted += PAGE_SIZE;
    }

    return STATUS_SUCCESS;
}

VMM_API
NTSTATUS
EptMapSuperMemoryRange(
    _In_ PEPT_PTE Pdpte,
    _In_ UINT64 GuestPhysAddr,
    _In_ UINT64 PhysAddr,
    _In_ UINT64 Size,
    _In_ EPT_PAGE_PERMISSIONS Permissions
)
/*++
Routine Description:
    Converts a PDPTE to a super PDPTE (mapping a 1GB region), returns STATUS_INVALID_PARAMETER if the region described cannot be mapped using
    super PDPTEs
--*/
{
    // Make sure this memory region meets the requirements for super page mapping
    // 
    // 1. Super page technology must be supported on the current CPU 
    // 2. The PhysAddr and GuestPhysAddr we are attempting to map must be 1GB aligned 
    // 3. The size of the region we are mapping must be greater than 1GB
    // 4. The MTRR region we are trying to map must have more than 1GB remaining
    if (!EptCheckSuperPageSupport() || Size < GB(1) || (PhysAddr & 0x3FFFFFFF) != 0 || (GuestPhysAddr & 0x3FFFFFFF) != 0 || (MtrrGetRegionEnd(PhysAddr) - PhysAddr) < GB(1))
        return STATUS_INVALID_PARAMETER;

    Pdpte->Present = TRUE;
    Pdpte->LargePage = TRUE;

    Pdpte->PageFrameNumber = PAGE_FRAME_NUMBER(PhysAddr);
    Pdpte->MemoryType = MtrrGetRegionType(PhysAddr);

    EptApplyPermissions(Pdpte, Permissions);

    return STATUS_SUCCESS;
}

VMM_API
NTSTATUS
EptSubvertSuperPage(
    _In_ PEPT_PTE Pdpte,
    _In_ UINT64 GuestPhysAddr,
    _In_ UINT64 PhysAddr,
    _In_ EPT_PAGE_PERMISSIONS Permissions
)
/*++
Routine Description:
    Takes a Super PDPTE and converts it into a normal PDPTE, mapping all necessary pages. This function attempts to map 
    the super page as large pages, and if that fails then it uses regular 4KB pages
--*/
{
    static const sSize = GB(1);

    if (!Pdpte->LargePage)
        return STATUS_INVALID_PARAMETER;

    Pdpte->LargePage = FALSE;
    
    PEPT_PTE Pd = NULL;
    if (!NT_SUCCESS(MmAllocateHostPageTable(&Pd)))
    {
        ImpDebugPrint("Couldn't allocate EPT PD for '%llx' subversion...\n", GuestPhysAddr);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Pdpte->PageFrameNumber =
        PAGE_FRAME_NUMBER(MmGetLastAllocatedPageTableEntry()->TablePhysAddr);

    // Since we are subverting a large page, the MTRR region is constant throughout the whole memory range
    MEMORY_TYPE RegionType = MtrrGetRegionType(PhysAddr);

    SIZE_T SizeSubverted = 0;
    while (SizeSubverted < sSize)
    {
        EPT_GPA Gpa = {
            .Value = GuestPhysAddr + SizeSubverted
        };

        PEPT_PTE Pde = &Pd[Gpa.PdIndex];

        PEPT_PTE Pte = NULL;
        if (!Pde->Present)
        {
            // Try to map as a large PDE, continue if that succeeds
            if (NT_SUCCESS(
                EptMapLargeMemoryRange(
                    Pde,
                    GuestPhysAddr + SizeSubverted,
                    PhysAddr + SizeSubverted,
                    sSize - SizeSubverted,
                    Permissions)
                ))
            {
                SizeSubverted += MB(2);
                continue;
            }
            else
            {
                PEPT_PTE Pt = NULL;
                if (!NT_SUCCESS(MmAllocateHostPageTable(&Pt)))
                {
                    ImpDebugPrint("Couldn't allocate EPT PT for '%llx' subversion...\n", PhysAddr + SizeSubverted);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                Pte = &Pt[Gpa.PtIndex];

                EptApplyPermissions(Pde, EPT_PAGE_RWX);

                Pde->Present = TRUE;
                Pde->PageFrameNumber =
                    PAGE_FRAME_NUMBER(MmGetLastAllocatedPageTableEntry()->TablePhysAddr);
            }
        }
        else
        {
            // No need to check for subversion, the entire PD for this 1GB region was created this call
            Pte = EptReadExistingPte(Pde->PageFrameNumber, Gpa.PtIndex);
        }

        if (!Pte->Present)
            Pte->Present = TRUE;

        EptApplyPermissions(Pte, Permissions);

        Pte->PageFrameNumber = PAGE_FRAME_NUMBER(Gpa.Value);
        Pte->MemoryType = RegionType;

        SizeSubverted += PAGE_SIZE;
    }

    return STATUS_SUCCESS;
}

VMM_API
NTSTATUS
EptMapMemoryRange(
    _In_ PEPT_PTE Pml4,
    _In_ UINT64 GuestPhysAddr,
    _In_ UINT64 PhysAddr,
    _In_ UINT64 Size,
    _In_ EPT_PAGE_PERMISSIONS Permissions
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
            .Value = GuestPhysAddr + SizeMapped
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

            EptApplyPermissions(Pml4e, EPT_PAGE_RWX);

            Pml4e->Present = TRUE;
            Pml4e->PageFrameNumber = 
                PAGE_FRAME_NUMBER(MmGetLastAllocatedPageTableEntry()->TablePhysAddr);
        }
        else
            Pdpte = EptReadExistingPte(Pml4e->PageFrameNumber, Gpa.PdptIndex);

        PEPT_PTE Pde = NULL;
        if (!Pdpte->Present)
        {
            // Try to map as a super PDPTE, continue if that succeeds
            if (NT_SUCCESS(
                EptMapSuperMemoryRange(
                    Pdpte,
                    GuestPhysAddr + SizeMapped,
                    PhysAddr + SizeMapped,
                    Size - SizeMapped,
                    Permissions)
                ))
            {
                SizeMapped += GB(1);
                continue;
            }
            else
            {
                PEPT_PTE Pd = NULL;
                if (!NT_SUCCESS(MmAllocateHostPageTable(&Pd)))
                {
                    ImpDebugPrint("Couldn't allocate EPT PD for '%llx'...\n", PhysAddr + SizeMapped);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                Pde = &Pd[Gpa.PdIndex];

                EptApplyPermissions(Pdpte, EPT_PAGE_RWX);

                Pdpte->Present = TRUE;
                Pdpte->PageFrameNumber =
                    PAGE_FRAME_NUMBER(MmGetLastAllocatedPageTableEntry()->TablePhysAddr);
            }
        }
        else
        {
            if (Pdpte->LargePage)
            {
                if (!NT_SUCCESS(EptSubvertSuperPage(Pdpte, (GuestPhysAddr + SizeMapped) & ~0x3FFFFFFF, (PhysAddr + SizeMapped) & ~0x3FFFFFFF, Permissions)))
                {
                    ImpDebugPrint("Failed to subvert PDPTE containing '%llx'...n");
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
            }
            
            Pde = EptReadExistingPte(Pdpte->PageFrameNumber, Gpa.PdIndex);
        }

        PEPT_PTE Pte = NULL;
        if (!Pde->Present)
        {
            // Try to map as a large PDE, continue if that succeeds
            if (NT_SUCCESS(
                EptMapLargeMemoryRange(
                    Pde,
                    GuestPhysAddr + SizeMapped,
                    PhysAddr + SizeMapped,
                    Size - SizeMapped,
                    Permissions)
                ))
            {
                SizeMapped += MB(2);
                continue;
            }
            else
            {
                PEPT_PTE Pt = NULL;
                if (!NT_SUCCESS(MmAllocateHostPageTable(&Pt)))
                {
                    ImpDebugPrint("Couldn't allocate EPT PD for '%llx'...\n", PhysAddr + SizeMapped);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                Pte = &Pt[Gpa.PtIndex];

                EptApplyPermissions(Pde, EPT_PAGE_RWX);

                Pde->Present = TRUE;
                Pde->PageFrameNumber =
                    PAGE_FRAME_NUMBER(MmGetLastAllocatedPageTableEntry()->TablePhysAddr);
            }
        }
        else
        {
            if (Pde->LargePage)
            {
                if (!NT_SUCCESS(EptSubvertLargePage(Pde, (GuestPhysAddr + SizeMapped) & ~0x1FFFFF, (PhysAddr + SizeMapped) & ~0x1FFFFF, Permissions)))
                {
                    ImpDebugPrint("Failed to subvert PDE containing '%llx'...n");
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
            }

            Pte = EptReadExistingPte(Pde->PageFrameNumber, Gpa.PtIndex);
        }

        if (!Pte->Present)
            Pte->Present = TRUE;

        EptApplyPermissions(Pte, Permissions);

        Pte->PageFrameNumber = PAGE_FRAME_NUMBER(Gpa.Value);
        Pte->MemoryType = MtrrGetRegionType(Gpa.Value);

        SizeMapped += PAGE_SIZE;
    }

    return STATUS_SUCCESS;
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
    NTSTATUS Status = STATUS_SUCCESS;

    PPHYSICAL_MEMORY_RANGE PhysMemRange = MmGetPhysicalMemoryRanges();
    if (PhysMemRange == NULL)
        return STATUS_NOT_SUPPORTED;

    while (PhysMemRange->BaseAddress.QuadPart != 0 && PhysMemRange->NumberOfBytes.QuadPart != 0)
    {
        // Map all of system memory as RWX for now
        if (!NT_SUCCESS(EptMapMemoryRange(Pml4, PhysMemRange->BaseAddress.QuadPart, PhysMemRange->BaseAddress.QuadPart, PhysMemRange->NumberOfBytes.QuadPart, EPT_PAGE_RWX)))
        {
            ImpDebugPrint("Failed to map region '%llx' with size '%llx'...\n", PhysMemRange->BaseAddress.QuadPart, PhysMemRange->NumberOfBytes.QuadPart);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    
        PhysMemRange++;
    }

    IA32_APIC_BASE_MSR ApicBase = {
        .Value = __readmsr(IA32_APIC_BASE)
    };

    if (ApicBase.APICEnable)
    {
        ImpDebugPrint("Mapping APIC %llX...\n", PAGE_ADDRESS(ApicBase.APICBase));

        if (!NT_SUCCESS(EptMapMemoryRange(Pml4, PAGE_ADDRESS(ApicBase.APICBase), PAGE_ADDRESS(ApicBase.APICBase), PAGE_SIZE, EPT_PAGE_RWX)))
        {
            ImpDebugPrint("Failed to map APIC...\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    return Status;
}

VOID
EptInvalidateCache(VOID)
{
    EPT_INVEPT_DESCRIPTOR InveptDescriptor = {0};

    __invept(INV_ALL_CONTEXT, &InveptDescriptor);
}

BOOLEAN
EptCheckSupport(VOID)
{
    IA32_VMX_EPT_VPID_CAP_MSR EptVpidCap = {
        .Value = __readmsr(IA32_VMX_EPT_VPID_CAP)
    };

    if (!EptVpidCap.PageWalkLength4Support ||
        !EptVpidCap.WbMemoryTypeSupport ||
        !EptVpidCap.ExecuteOnlyTranslationSupport)
        return FALSE;

    return TRUE;
}

NTSTATUS
EptInitialise(
    _Inout_ PEPT_INFORMATION EptInformation
)
/*++
Routine Description:
    This function initialises the EPT identity map and hooking capabilities. This function should be called
    before other Mm* functions which allocate page tables to improve performance
--*/
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!NT_SUCCESS(MtrrInitialise()))
        return STATUS_INSUFFICIENT_RESOURCES;

    PEPT_PTE Pml4 = NULL;
    if (!NT_SUCCESS(MmAllocateHostPageTable(&Pml4)))
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = EptSetupIdentityMap(Pml4); 
    if (!NT_SUCCESS(Status))
        return Status;

    EptInformation->SystemPml4 = Pml4;

    if (!NT_SUCCESS(MmAllocateHostPageTable(&EptInformation->DummyPage)))
        return STATUS_INSUFFICIENT_RESOURCES;

    EptInformation->DummyPagePhysAddr = MmGetLastAllocatedPageTableEntry()->TablePhysAddr;

    // Watermark the dummy page to avoid page folding
    *(PULONG)EptInformation->DummyPage = 'IMPV' ^ (ULONG)__rdtsc();

    return Status;
}
