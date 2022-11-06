#ifndef IMP_MTRR_H
#define IMP_MTRR_H

#include <ntdef.h>

typedef union _IA32_MTRR_CAPABILITIES_MSR
{
	UINT64 Value;

	struct
	{
		UINT64 VariableRangeRegCount : 8;
		UINT64 FixedRangeRegSupport : 1;
		UINT64 Reserved1 : 1;
		UINT64 WriteCombineTypeSupport : 1;
		UINT64 SmrrInterfaceSupport : 1;
		UINT64 Reserved2 : 52;
	};
} IA32_MTRR_CAPABILITIES_MSR, * PIA32_MTRR_CAPABILITIES_MSR;

typedef union _IA32_MTRR_DEFAULT_TYPE_MSR
{
	UINT64 Value;

	struct
	{
		UINT64 Type : 8;
		UINT64 Reserved1 : 2;
		UINT64 EnableFixedMtrr : 1;
		UINT64 EnableMtrr : 1;
		UINT64 Reserved2 : 52;
	};
} IA32_MTRR_DEFAULT_TYPE_MSR, * PIA32_MTRR_DEFAULT_TYPE_MSR;

typedef union _IA32_MTRR_PHYSBASE_N_MSR
{
	UINT64 Value;

	struct
	{
		UINT64 Type : 8;
		UINT64 Reserved1 : 4;
		UINT64 Base : 36;
		UINT64 Reserved2 : 16;
	};
} IA32_MTRR_PHYSBASE_N_MSR, * PIA32_MTRR_PHYSBASE_N_MSR;

typedef union _IA32_MTRR_PHYSMASK_N_MSR
{
	UINT64 Value;

	struct
	{
		UINT64 Reserved1 : 11;
		UINT64 Valid : 1;
		UINT64 Mask : 36;
		UINT64 Reserved2 : 16;
	};
} IA32_MTRR_PHYSMASK_N_MSR, * PIA32_MTRR_PHYSMASK_N_MSR;

typedef union _IA32_MTRR_FIXED_RANGE_MSR
{
	UINT64 Value;

	struct
	{
		UINT8 Types[8];
	};
} IA32_MTRR_FIXED_RANGE_MSR, * PIA32_MTRR_FIXED_RANGE_MSR;

typedef enum _MEMORY_TYPE
{
	MT_UNCACHABLE = 0,
	MT_WRITE_COMBINING = 1,
	MT_WRITE_THROUGH = 4,
	MT_WRITE_PROTECTED = 5,
	MT_WRITEBACK = 6,
	MT_UNCACHED = 7,          //<! Table 11-10. Memory Types That Can Be Encoded With PAT

	MT_INVALID = 0xFF
} MEMORY_TYPE;

UINT64
MtrrGetRegionSize(
	_In_ UINT64 PhysAddr
);

UINT64
MtrrGetRegionBase(
	_In_ UINT64 PhysAddr
);

MEMORY_TYPE
MtrrGetRegionType(
	_In_ UINT64 PhysAddr
);

UINT64
MtrrGetRegionEnd(
	_In_ UINT64 PhysAddr
);

NTSTATUS
MtrrInitialise(VOID);

#endif
