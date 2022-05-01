#ifndef IMP_ARCH_SEGMENT_H
#define IMP_ARCH_SEGMENT_H

#include <ntdef.h>

#define SEGMENT_SELECTOR_TABLE_GDT 0
#define SEGMENT_SELECTOR_TABLE_LDT 1

// For the host segments, the RPL should be 0 so that on VM-exit the CPL becomes 0.
#define HOST_SEGMENT_SELECTOR_MASK (~0x03ULL)

#pragma pack(push, 1)
typedef struct _X86_PSEUDO_DESCRIPTOR
{
	UINT16 Limit;
	UINT64 BaseAddress;
} X86_PSEUDO_DESCRIPTOR, *PX86_PSEUDO_DESCRIPTOR;
#pragma pack(pop)

typedef union _X86_SEGMENT_SELECTOR
{
	UINT16 Value;

	struct
	{
		UINT16 Rpl : 2;
		UINT16 Table : 1;
		UINT16 Index : 13;
	};
} X86_SEGMENT_SELECTOR, *PX86_SEGMENT_SELECTOR;

typedef struct _X86_SEGMENT_DESCRIPTOR
{
    UINT16 LimitLow;
	UINT16 BaseLow;
	UINT32 BaseMiddle : 8;
	UINT32 Type : 4;
	UINT32 System : 1;
	UINT32 Dpl : 2;
	UINT32 Present : 1;
	UINT32 LimitHigh : 4;
	UINT32 OsDefined : 1;
	UINT32 Long : 1;
	UINT32 DefaultOperationSize : 1;
	UINT32 Granularity : 1;
	UINT32 BaseHigh : 8;
} X86_SEGMENT_DESCRIPTOR, *PX86_SEGMENT_DESCRIPTOR;

typedef struct _X86_SYSTEM_DESCRIPTOR
{
    UINT16 LimitLow;
	UINT16 BaseLow;
	UINT32 BaseMiddle : 8;
	UINT32 Type : 4;
	UINT32 DescriptorType : 1;
	UINT32 Dpl : 2;
	UINT32 Present : 1;
	UINT32 LimitHigh : 4;
	UINT32 System : 1;
	UINT32 Long : 1;
	UINT32 DefaultOperationSize : 1;
	UINT32 Granularity : 1;
	UINT32 BaseHigh : 8;
	UINT32 BaseUpper;
	UINT32 Reserved;
} X86_SYSTEM_DESCRIPTOR, *PX86_SYSTEM_DESCRIPTOR;

typedef union _X86_SEGMENT_ACCESS_RIGHTS
{
	UINT32 Value;

	struct
	{
		UINT32 Type : 4;
		UINT32 System : 1;
		UINT32 Dpl : 2;
		UINT32 Present : 1;
		UINT32 Reserved1 : 4;
		UINT32 Available : 1;
		UINT32 Long : 1;
		UINT32 DefaultOperationSize : 1;
		UINT32 Granularity : 1;
		UINT32 Unusable : 1;
		UINT32 Reserved2 : 15;
	};
} X86_SEGMENT_ACCESS_RIGHTS, *PX86_SEGMENT_ACCESS_RIGHTS;

typedef enum _X86_SEGMENT_TYPE
{
	/* Ones marked _32 or _64 are only available in those modes. */
	SEGMENT_TYPE_WORD_TSS_AVAILABLE_32 = 1,
	SEGMENT_TYPE_LOCAL_DESCRIPTOR_TABLE = 2,
	SEGMENT_TYPE_WORD_TSS_BUSY_32 = 3,
	SEGMENT_TYPE_WORD_CALL_GATE_32 = 4,
	SEGMENT_TYPE_TASK_GATE_32 = 5,
	SEGMENT_TYPE_WORD_INTERRUPT_GATE_32 = 6,
	SEGMENT_TYPE_WORD_TRAP_GATE_32 = 7,
	SEGMENT_TYPE_NATURAL_TSS_AVAILABLE = 9,
	SEGMENT_TYPE_NATURAL_TSS_BUSY = 11,
	SEGMENT_TYPE_NATURAL_CALL_GATE = 12,
	SEGMENT_TYPE_NATURAL_INTERRUPT_GATE = 14,
	SEGMENT_TYPE_NATURAL_TRAP_GATE = 15,
} X86_SEGMENT_TYPE, *PX86_SEGMENT_TYPE;


UINT64
SegmentBaseAddress(
    _In_ X86_SEGMENT_SELECTOR Selector
);

UINT32
SegmentAr(
    _In_ X86_SEGMENT_SELECTOR Selector
);

#endif
