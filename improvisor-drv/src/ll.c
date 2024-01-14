#include <improvisor.h>
#include <section.h>
#include <ll.h>

NTSTATUS
LlCreatePool(
	_Inout_ PLINKED_LIST_POOL Pool,
	_In_ SIZE_T ElementEntryOffset,
	_In_ SIZE_T ElementSize,
	_In_ SIZE_T MaxElements
)
/*++
Routine Description:
	Creates a pool that can contain `MaxElements` of size `ElementSize`. This function shouldn't be called directly,
	instead use the `LL_CREATE_POOL` macro
--*/
{
	PVOID Buffer = ImpAllocateHostNpPool(ElementSize * MaxElements);
	if (Buffer == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	Pool->Buffer = Buffer;
	Pool->MaxElements = MaxElements;
	Pool->ElementSize = ElementSize;
	Pool->ElementEntryOffset = ElementEntryOffset;
	Pool->ElementsUsed = 0;

	InitializeListHead(&Pool->Used);
	InitializeListHead(&Pool->Free);

	// Insert all elements into the free list
	for (SIZE_T i = 0; i < MaxElements; i++)
	{
		PVOID CurrElement = RVA_PTR(Buffer, i * ElementSize);
		// Insert the empty element into the list of unused entries
		InsertTailList(&Pool->Free, LlElementToLink(Pool, CurrElement));
	}

	return STATUS_SUCCESS;
}
