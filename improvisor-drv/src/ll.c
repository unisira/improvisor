#include <improvisor.h>
#include <section.h>
#include <macro.h>
#include <ll.h>

// LlAllocate - Removes entry from free list and inserts into used list, returning it
// LlMarkUsed - Helper function to move from free to used list
// LlMarkFree - Helper function to move from used to free list
// LlPop 	  - Grabs from 

// 'allocate entry', insert into used and allow modifications ???? seems mega retarded but idfk, not sure how else to go about this
// 
//

FORCEINLINE
PVOID
LlLinkToElement(
	_In_ PLINKED_LIST_POOL Pool,
	_In_ PLIST_ENTRY Link
)
/*++
Routine Description:
	Returns the element containing the list entry link `Link`
--*/
{
	return RVA_PTR(Link, -Pool->ElementEntryOffset);
}

FORCEINLINE
PLIST_ENTRY
LlElementToLink(
	_In_ PLINKED_LIST_POOL Pool,
	_In_ PVOID Element
)
/*++
Routine Description:
	Returns the linked list entry within the element `Element`
--*/
{
	return RVA_PTR_T(LIST_ENTRY, Element, Pool->ElementEntryOffset);
}

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

	InitializeListHead(&Pool->Used);
	InitializeListHead(&Pool->Free);

	// Insert all elements into the free list
	// We have some big loops, how long will this take realistically?
	// NOTE: Either trace or optimise or something
	for (SIZE_T i = 0; i < MaxElements; i++)
	{
		PVOID CurrElement = RVA_PTR(Buffer, i * ElementSize);
		// Insert the empty element into the list of unused entries
		InsertTailList(&Pool->Free, LlElementToLink(Pool, CurrElement));
	}

	return STATUS_SUCCESS;
}

VOID
LlUse(
	_Inout_ PLINKED_LIST_POOL Pool,
	_In_ PLIST_ENTRY Entry
)
/*++
Routine Description:
	Removes an element from the list of free elements and inserts it into the used elements list. Ideally not called directly,
	LlAllocate should be used instead as this emulates allocating memory and is the main source of access to entries.
--*/
{
	// Lock the entire pool for concurrency
	SpinLock(&Pool->Lock);

	// Remove `Entry` from the list of free elements
	// TODO: Validate state of `Entry`?
	RemoveEntryList(Entry);
	// Insert `Entry` as the last inserted element in the list of used elements
	InsertTailList(&Pool->Used, Entry);

	SpinUnlock(&Pool->Lock);
}

VOID
LlFree(
	_Inout_ PLINKED_LIST_POOL Pool,
	_In_ PLIST_ENTRY Entry
)
/*++
Routine Description:
	Removes an element from the list of used elements and inserts it into the free elements list
--*/
{
	// VPTE's don't really get iterated all that often, it is just a limited resource with a list of ones that are still to be used
	// But other resources do, logs, allocations, hooks
	// all of these need a linked list structure or something equivalent
	// None of them need random access (lies, hooks would be useful, but fuck it)
	// Possibly implement a hash map for above, could be useful to have, especially for PDB parsing more efficiently
	
	// Lock the entire pool for concurrency
	SpinLock(&Pool->Lock);

	// Remove `Entry` from the list of used elements
	// TODO: Validate state of `Entry`?
	RemoveEntryList(Entry);
	// Insert `Entry` as the last inserted element in the list of free elements
	InsertTailList(&Pool->Free, Entry);

	SpinUnlock(&Pool->Lock);
}

PVOID
LlAllocate(
	_Inout_ PLINKED_LIST_POOL Pool
)
/*++
Routine Description:
	This function allocates a new record in the linked list pool and returns it.
--*/
{
	PVOID Element = NULL;

	SpinLock(&Pool->Lock);

	if (IsListEmpty(&Pool->Free) == FALSE)
	{
		// Get the first inserted entry in the list of free elements
		PLIST_ENTRY Link = Pool->Free.Flink;
		// TODO: Validate state of `Link`?
		Element = LlLinkToElement(Pool, Link);	
		// Remove this element from the list
		RemoveEntryList(Link);
		// Insert `Entry` as the last inserted element in the list of used elements
		InsertTailList(&Pool->Used, Link);
	}

	SpinUnlock(&Pool->Lock);

	return Element;
}

PVOID
LlRelease(
	_Inout_ PLINKED_LIST_POOL Pool
)
/*++
Routine Description:
	Removes the last entry inserted in the `Used` list and removes it without inserting it 
	into the list of free elements

	TODO: Should I make this generic between both lists? Will I never need to remove an entry from `Free` without re-inserting it?
		  That does defeat the purpose of it being called a `Free` entry, and a `Used` entry might just not be recorded...
--*/
{
	PVOID Element = NULL;
	
	SpinLock(&Pool->Lock);

	if (IsListEmpty(&Pool->Used) == FALSE)
	{
		// Get the last inserted entry in the list of used elements
		PLIST_ENTRY Link = Pool->Used.Blink;
		// TODO: Validate state of `Link`?
		Element = LlLinkToElement(Pool, Link);	
		// Remove this element from the list
		RemoveEntryList(Link);
	}

	SpinUnlock(&Pool->Lock);

	return Element;
}

PVOID
LlBegin(
	_Inout_ PLINKED_LIST_POOL Pool
)
/*++
Routine Description:
	Returns the first inserted element from the list of used elements
--*/
{
	PVOID Element = NULL;

	SpinLock(&Pool->Lock);

	if (!IsListEmpty(&Pool->Used))
		Element = LlLinkToElement(Pool, Pool->Used.Flink);

	SpinUnlock(&Pool->Lock);

	return Element;
}

PVOID
LlEnd(
	_Inout_ PLINKED_LIST_POOL Pool
)
/*++
Routine Description:
	Returns the last inserted element from the list of used elements
--*/
{
	PVOID Element = NULL;

	SpinLock(&Pool->Lock);

	if (!IsListEmpty(&Pool->Used))
		Element = LlLinkToElement(Pool, Pool->Used.Blink);

	SpinUnlock(&Pool->Lock);

	return Element;
}

BOOLEAN
LlIsEmpty(
	_Inout_ PLINKED_LIST_POOL Pool
)
/*++
Routine Description:
	Returns if the list of used elements is empty or not
--*/
{
	return IsListEmpty(&Pool->Used);
}
