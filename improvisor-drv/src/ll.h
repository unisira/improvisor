#ifndef IMP_LL_H
#define IMP_LL_H

#include <wdm.h>
#include <spinlock.h>

// Linked list pool macros.
//
// These should be used over calling functions directl, they help avoid bugs

// Small wrapper to get rid of ugly `sizeof`. The `LIST_ENTRY` member of `Ty` must be called 'Links'
#define LL_CREATE_POOL(Pool, Ty, Count)       						\
    LlCreatePool(Pool, offsetof(Ty, Links), sizeof(Ty), (Count))  \

// 
#define LL_ALLOC(Pool)	\
	LlAllocate((Pool))	\	

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
    // Head of the list of elements
    LIST_ENTRY Used;
	// head of the list of unused elements
	LIST_ENTRY Free;
	// The lock for the list
	SPINLOCK Lock;
} LINKED_LIST_POOL, *PLINKED_LIST_POOL;

NTSTATUS
LlCreatePool(
	_Inout_ PLINKED_LIST_POOL Pool,
    _In_ SIZE_T ElementEntryOffset,
    _In_ SIZE_T ElementSize,
    _In_ SIZE_T MaxElements
);

VOID
LlUse(
	_Inout_ PLINKED_LIST_POOL Pool,
	_In_ PLIST_ENTRY Entry
);

VOID
LlFree(
	_Inout_ PLINKED_LIST_POOL Pool,
	_In_ PLIST_ENTRY Entry
);

PVOID
LlAllocate(
	_Inout_ PLINKED_LIST_POOL Pool
);

PVOID
LlRelease(
	_Inout_ PLINKED_LIST_POOL Pool
);

PVOID
LlBegin(
	_Inout_ PLINKED_LIST_POOL Pool
);

PVOID
LlEnd(
	_Inout_ PLINKED_LIST_POOL Pool
);

BOOLEAN
LlIsEmpty(
	_Inout_ PLINKED_LIST_POOL Pool
);

#endif
