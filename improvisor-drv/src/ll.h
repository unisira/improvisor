#ifndef IMP_LL_H
#define IMP_LL_H

#include <wdm.h>
#include <hash.h>
#include <spinlock.h>
#include <macro.h>

// Linked list pool macros.
//
// These should be used over calling functions directly, they help avoid bugs

// Small wrapper to get rid of ugly `sizeof`. The `LIST_ENTRY` member of `Ty` must be called 'Links'
#define LL_CREATE_POOL(Pool, Ty, Count)       						\
    LlCreatePool(Pool, offsetof(Ty, Links), sizeof(Ty), (Count))  	\

// A linked list pool.
//
// A lot of structures in the improvisor are variable size linked lists where
// entries are allocated and free'd very often. This structure contains a raw buffer
// and information about the structure of a linked list
typedef struct _LINKED_LIST_POOL
{
	// Buffer containing all entries
	PVOID Buffer;
	// Maximum amount of elements that `Buffer` can store
	SIZE_T MaxElements;
	// Size of each element within the buffer
	SIZE_T ElementSize;
    // Offset to the `LIST_ENTRY` structure within the element's type
    SIZE_T ElementEntryOffset;
	// The amount of elements used
	SIZE_T ElementsUsed;
    // Head of the list of elements
    LIST_ENTRY Used;
	// head of the list of unused elements
	LIST_ENTRY Free;
	// The lock for the list
	SPINLOCK Lock;
} LINKED_LIST_POOL, *PLINKED_LIST_POOL;

// Predicate callback for searching linked lists
typedef BOOLEAN(*LINKED_LIST_SEARCH_PREDICATE)(PVOID, PVOID);

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
);

FORCEINLINE
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
	// Lock the entire pool for concurrency
	SpinLock(&Pool->Lock);

	// Remove `Entry` from the list of used elements
	// TODO: Validate state of `Entry`?
	RemoveEntryList(Entry);
	// Insert `Entry` as the last inserted element in the list of free elements
	InsertTailList(&Pool->Free, Entry);

	Pool->ElementsUsed--;

	SpinUnlock(&Pool->Lock);
}

FORCEINLINE
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

		Pool->ElementsUsed++;
	}

	SpinUnlock(&Pool->Lock);

	return Element;
}

FORCEINLINE
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

FORCEINLINE
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

FORCEINLINE
PVOID
LlFind(
	_In_ PLINKED_LIST_POOL Pool,
	_In_ LINKED_LIST_SEARCH_PREDICATE Pred,
	_In_ PVOID Context
)
/*++
Routine Description:
	Iterates all elements within `Pool` and calls `Pred`, returns the element for which `Pred` returns `TRUE`
--*/
{
	PVOID Element = NULL;

	SpinLock(&Pool->Lock);

	PLIST_ENTRY CurrLink = Pool->Used.Flink;
	// Search until we hit the end of the list
	while (CurrLink != &Pool->Used && Element == NULL)
	{
		if (Pred(LlLinkToElement(Pool, CurrLink), Context) == TRUE)
			Element = LlLinkToElement(Pool, CurrLink);

		CurrLink = CurrLink->Flink;
	}

	SpinUnlock(&Pool->Lock);

	return Element;
}

FORCEINLINE
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

#endif
