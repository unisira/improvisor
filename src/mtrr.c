#include "improvisor.h"
#include "arch/memory.h"
#include "arch/mtrr.h"
#include "arch/msr.h"
#include "section.h"
#include "intrin.h"
#include "mtrr.h"

typedef struct _MTRR_STATIC_REGION
{
	UINT32 Msr;
	UINT32 Base;
	UINT32 BlockSize;
} MTRR_STATIC_REGION, *PMTRR_STATIC_REGION;

typedef struct _MTRR_REGION_CACHE_ENTRY
{
	LIST_ENTRY Links;
	UINT64 Base;
	UINT64 Size;
	MEMORY_TYPE Type;
} MTRR_REGION_CACHE_ENTRY, *PMTRR_REGION_CACHE_ENTRY;

static const MTRR_STATIC_REGION sFixedMtrrRanges[] = {
	{IA32_MTRR_FIX64K_00000, 0x00000ul, KB(64)},
	{IA32_MTRR_FIX16K_80000, 0x80000ul, KB(16)},
	{IA32_MTRR_FIX16K_A0000, 0xA0000ul, KB(16)},
	{IA32_MTRR_FIX4K_C0000, 0xC0000ul, KB(4)},
	{IA32_MTRR_FIX4K_C8000, 0xC8000ul, KB(4)},
	{IA32_MTRR_FIX4K_D0000, 0xD0000ul, KB(4)},
	{IA32_MTRR_FIX4K_D8000, 0xD8000ul, KB(4)},
	{IA32_MTRR_FIX4K_E0000, 0xE0000ul, KB(4)},
	{IA32_MTRR_FIX4K_E8000, 0xE8000ul, KB(4)},
	{IA32_MTRR_FIX4K_F0000, 0xF0000ul, KB(4)},
	{IA32_MTRR_FIX4K_F8000, 0xF8000ul, KB(4)}
};

VMM_DATA static PMTRR_REGION_CACHE_ENTRY sMtrrRegionCacheRaw = NULL;

// The head of the MTRR variable range cache list
VMM_DATA PMTRR_REGION_CACHE_ENTRY gMtrrRegionCacheHead = NULL;

VMM_API
PMTRR_REGION_CACHE_ENTRY
MtrrGetContainingRegion(
	_In_ UINT64 PhysAddr
)
{
	PMTRR_REGION_CACHE_ENTRY CurrEntry = gMtrrRegionCacheHead;
	while (CurrEntry != NULL)
	{
		if (CurrEntry->Base <= PhysAddr && PhysAddr < CurrEntry->Base + CurrEntry->Size)
			return CurrEntry;

		CurrEntry = (PMTRR_REGION_CACHE_ENTRY)CurrEntry->Links.Blink;
	}

	return NULL;
}

VMM_API
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

VMM_API
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

VMM_API
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

VMM_API
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

VMM_API
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

VSC_API
NTSTATUS
MtrrSaveVariableRange(
	_In_ IA32_MTRR_PHYSBASE_N_MSR Base,
	_In_ IA32_MTRR_PHYSMASK_N_MSR Mask
)
{
	PMTRR_REGION_CACHE_ENTRY CurrMtrrEntry = gMtrrRegionCacheHead;
	if (CurrMtrrEntry->Links.Flink == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	CurrMtrrEntry->Type = Base.Type;
	CurrMtrrEntry->Base = PAGE_ADDRESS(Base.Base);

	ULONG SizeShift = 0;
	_BitScanForward64(&SizeShift, PAGE_ADDRESS(Mask.Mask));

	CurrMtrrEntry->Size = 1ULL << SizeShift;

	gMtrrRegionCacheHead = CurrMtrrEntry->Links.Flink;

	return STATUS_SUCCESS;
}

VSC_API
NTSTATUS
MtrrSaveStaticRange(
	_In_ MTRR_STATIC_REGION FixedRegion
)
{
	for (SIZE_T i = 0; i < 8; i++)
	{
		PMTRR_REGION_CACHE_ENTRY CurrMtrrEntry = gMtrrRegionCacheHead;
		if (CurrMtrrEntry->Links.Flink == NULL)
			return STATUS_INSUFFICIENT_RESOURCES;

		IA32_MTRR_FIXED_RANGE_MSR FixedRangeMsr = {
			.Value = __readmsr(FixedRegion.Msr)
		};

		CurrMtrrEntry->Base = FixedRegion.Base + i * FixedRegion.BlockSize;
		CurrMtrrEntry->Size = FixedRegion.BlockSize;
		CurrMtrrEntry->Type = FixedRangeMsr.Types[i];

		gMtrrRegionCacheHead = CurrMtrrEntry->Links.Flink;
	}

	return STATUS_SUCCESS;
}

VSC_API
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

	SIZE_T MtrrRegionCount = MtrrCap.VariableRangeRegCount;

	if (MtrrCap.FixedRangeRegSupport && DefaultMtrr.EnableFixedMtrr)
	{
		// Each static MTRR region represents 8 blocks.
		for (SIZE_T i = 0; i < sizeof(sFixedMtrrRanges) / sizeof(*sFixedMtrrRanges); i++)
			MtrrRegionCount += 8;
	}

	sMtrrRegionCacheRaw = ImpAllocateHostNpPool(sizeof(MTRR_REGION_CACHE_ENTRY) * MtrrRegionCount + 1);
	if (sMtrrRegionCacheRaw == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	gMtrrRegionCacheHead = sMtrrRegionCacheRaw;

	for (SIZE_T i = 0; i < MtrrRegionCount + 1; i++)
	{
		PMTRR_REGION_CACHE_ENTRY CurrMtrrEntry = sMtrrRegionCacheRaw + i;

		CurrMtrrEntry->Links.Flink = i < MtrrRegionCount	? &(CurrMtrrEntry + 1)->Links : NULL;
		CurrMtrrEntry->Links.Blink = i > 0					? &(CurrMtrrEntry - 1)->Links : NULL;
	}

	// Save all variable MTRR range registers
	for (SIZE_T i = 0; i < MtrrCap.VariableRangeRegCount; i++)
	{
		IA32_MTRR_PHYSBASE_N_MSR Base = {
			.Value = __readmsr(IA32_MTRR_PHYSBASE_0 + i * 2)
		};

		IA32_MTRR_PHYSMASK_N_MSR Mask = {
			.Value = __readmsr(IA32_MTRR_PHYSMASK_0 + i * 2)
		};

		if (!Mask.Valid)
			continue;

		if (!NT_SUCCESS(MtrrSaveVariableRange(Base, Mask)))
		{
			ImpDebugPrint("Failed to save variable MTRR region (IA32_MTRR_PHYSBASE_N: %i)...\n", IA32_MTRR_PHYSBASE_0 + i * 2);
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}

	// Save all static MTRR ranges
	for (SIZE_T i = 0; i < sizeof(sFixedMtrrRanges) / sizeof(*sFixedMtrrRanges); i++)
	{
		if (!NT_SUCCESS(MtrrSaveStaticRange(sFixedMtrrRanges[i])))
		{
			ImpDebugPrint("Failed to save static MTRR region #%i...\n", i);
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}

	return STATUS_SUCCESS;
}
