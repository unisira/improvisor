#include "improvisor.h"
#include "arch/memory.h"
#include "arch/mtrr.h"
#include "arch/msr.h"
#include "intrin.h"
#include "mtrr.h"

typedef struct _MTRR_REGION_CACHE_ENTRY
{
    LIST_ENTRY Links;
    UINT64 Base;
    UINT64 Size;
    MEMORY_TYPE Type;
} MTRR_REGION_CACHE_ENTRY, * PMTRR_REGION_CACHE_ENTRY;


static PMTRR_REGION_CACHE_ENTRY sMtrrRegionCacheRaw = NULL;

// The head of the MTRR variable range cache list
PMTRR_REGION_CACHE_ENTRY gMtrrRegionCacheTail = NULL;

PMTRR_REGION_CACHE_ENTRY
MtrrGetContainingRegion(
    _In_ UINT64 PhysAddr
)
{
    PMTRR_REGION_CACHE_ENTRY CurrEntry = gMtrrRegionCacheTail;
    while (CurrEntry != NULL)
    {
        if (CurrEntry->Base <= PhysAddr && PhysAddr < CurrEntry->Base + CurrEntry->Size)
            return CurrEntry;

        CurrEntry = (PMTRR_REGION_CACHE_ENTRY)CurrEntry->Links.Flink;
    } 

    return NULL;
}

UINT64 
MtrrGetRegionSize(
    _In_ UINT64 PhysAddr
)
/*++
Routine Description:
    Returns the size of the containing MTRR region
--*/
{
    PMTRR_REGION_CACHE_ENTRY Entry = MtrrGetContainingRegion(PhysAddr);
    if (Entry == NULL)
        return 0;

    return Entry->Size;
}

UINT64
MtrrGetRegionBase(
    _In_ UINT64 PhysAddr
)
/*++
Routine Description:
    Returns the size of the containing MTRR region
--*/
{
    PMTRR_REGION_CACHE_ENTRY Entry = MtrrGetContainingRegion(PhysAddr);
    if (Entry == NULL)
        return 0;

    return Entry->Base;
}

MEMORY_TYPE
MtrrGetDefaultType(VOID)
/*++
Routine Description:
    Returns the default memory type for any range outside of the VRR's
--*/
{
    IA32_MTRR_DEFAULT_TYPE_MSR DefaultMtrr = { 
        .Value = __readmsr(IA32_MTRR_DEFAULT_TYPE)
    };

    return DefaultMtrr.Type;
}

MEMORY_TYPE
MtrrGetRegionType(
    _In_ UINT64 PhysAddr
)
/*++
Routine Description:
    Returns the MTRR region type of the region containing `PhysAddr`
--*/
{
    PMTRR_REGION_CACHE_ENTRY Entry = MtrrGetContainingRegion(PhysAddr);
    if (Entry == NULL)
        return MtrrGetDefaultType();

    return Entry->Type;
}

UINT64
MtrrGetRegionEnd(
    _In_ UINT64 PhysAddr
)
/*++
Routine Description:
    Returns the size of the containing MTRR region
--*/
{
    PMTRR_REGION_CACHE_ENTRY Entry = MtrrGetContainingRegion(PhysAddr);
    if (Entry == NULL)
        return 0;

    return Entry->Base + Entry->Size;
}


NTSTATUS
MtrrInitialise(VOID)
{
    NTSTATUS Status = STATUS_SUCCESS;

    IA32_MTRR_CAPABILITIES_MSR MtrrCap = {
        .Value = __readmsr(IA32_MTRR_CAPABILITIES)
    };

    IA32_MTRR_DEFAULT_TYPE_MSR DefaultMtrr = {
        .Value = __readmsr(IA32_MTRR_DEFAULT_TYPE)
    };

    sMtrrRegionCacheRaw = ImpAllocateHostNpPool(sizeof(MTRR_REGION_CACHE_ENTRY) * MtrrCap.VariableRangeRegCount);
    if (sMtrrRegionCacheRaw == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    gMtrrRegionCacheTail = sMtrrRegionCacheRaw;

    if (MtrrCap.FixedRangeRegSupport && DefaultMtrr.EnableFixedMtrr)
    {
        // TODO: Support fixed MTRR ranges
    }

    for (SIZE_T i = 0; i < MtrrCap.VariableRangeRegCount; i++)
    {
        PMTRR_REGION_CACHE_ENTRY CurrMtrrEntry = sMtrrRegionCacheRaw + i;

        IA32_MTRR_PHYSBASE_N_MSR Base = {
            .Value = __readmsr(IA32_MTRR_PHYSBASE_0 + i * 2)
        };

        IA32_MTRR_PHYSMASK_N_MSR Mask = {
            .Value = __readmsr(IA32_MTRR_PHYSMASK_0 + i * 2)
        };

        if (!Mask.Valid)
            continue;

        CurrMtrrEntry->Type = Base.Type;
        CurrMtrrEntry->Base = PAGE_ADDRESS(Base.Base);

        const ULONG SizeShift = 0;
        _BitScanForward64(&SizeShift, Mask.Mask << 12);

        CurrMtrrEntry->Size = (1ULL << SizeShift);

        CurrMtrrEntry->Links.Flink = i < MtrrCap.VariableRangeRegCount  ? &(CurrMtrrEntry + 1)->Links : NULL;
        CurrMtrrEntry->Links.Blink = i > 0                              ? &(CurrMtrrEntry - 1)->Links : NULL;
    }

    return STATUS_SUCCESS;
}
