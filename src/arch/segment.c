#include "../improvisor.h"
#include "../intrin.h"
#include "segment.h"

PX86_SEGMENT_DESCRIPTOR
LookupSegmentDescriptor(
	_In_ X86_SEGMENT_SELECTOR Selector
)
/*++
Routine Description:
	Looks up a segment descriptor in the GDT using a provided selector
--*/
{
	X86_PSEUDO_DESCRIPTOR Gdtr;
	__sgdt(&Gdtr);

	return (PX86_SEGMENT_DESCRIPTOR)(Gdtr.BaseAddress + Selector.Index * sizeof(X86_SEGMENT_DESCRIPTOR));
}

UINT64
SegmentBaseAddress(
	_In_ X86_SEGMENT_SELECTOR Selector
)
/*++
Routine Description:
	Calculates the base address of a segment using a segment selector
--*/
{
	UINT64 Address = 0;

	// The LDT is unuseable on Windows 10, and the first entry of the GDT isn't used
	if (Selector.Table != SEGMENT_SELECTOR_TABLE_GDT || Selector.Index == 0)
		return 0;

	PX86_SEGMENT_DESCRIPTOR Segment = LookupSegmentDescriptor(Selector);

	if (Segment == NULL)
	{
		ImpDebugPrint("Invalid segment selector '%u'...\n", Selector.Value);
		return 0;
	}

	Address = (((UINT64)Segment->BaseHigh << 24) | 
			   ((UINT64)Segment->BaseMiddle << 16) | 
			   ((UINT64)Segment->BaseLow));
	
	if (Segment->System == 0)
	{
		PX86_SYSTEM_DESCRIPTOR SystemSegment = (PX86_SYSTEM_DESCRIPTOR)Segment;
		Address |= (UINT64)SystemSegment->BaseUpper << 32;
	}
	
	return Address;
}

UINT32
SegmentAr(
	_In_ X86_SEGMENT_SELECTOR Selector
)
/*++
Routine Description:
	Gets the access rights of a segment
--*/
{
	PX86_SEGMENT_DESCRIPTOR Segment = LookupSegmentDescriptor(Selector);

	if (Segment == NULL)
	{
		ImpDebugPrint("Invalid segment selector '%i'...\n", Selector.Value);
		return 0;
	}
	
	X86_SEGMENT_ACCESS_RIGHTS SegmentAccessRights = {
		.Value = (UINT32)(__segmentar(Selector) >> 8)
	};

	SegmentAccessRights.Unusable = !Segment->Present;
	SegmentAccessRights.Reserved1 = 0;
	SegmentAccessRights.Reserved2 = 0;
	
	return SegmentAccessRights.Value;
}
