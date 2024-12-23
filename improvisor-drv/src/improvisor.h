#ifndef IMP_IMPROVISOR_H
#define IMP_IMPROVISOR_H

#include <ntifs.h>
#include <ntddk.h>
#include <ntdef.h>
#include <stddef.h>
#include <wdm.h>
#include <stdarg.h>
#include <section.h>

#define POOL_TAG 'IMPV'

#define BUGCHECK_FAILED_SHUTDOWN 0x00001000
#define BUGCHECK_UNKNOWN_VMEXIT_REASON 0x00002000

// Make sure the size of buffer is a factor of PAGE_SIZE to make 
// HYPERCALL_GET_LOG_RECORDS faster by not needing to map multiple pages
#define IMP_LOG_SIZE (512ULL - sizeof(LIST_ENTRY))

typedef struct _IMP_LOG_RECORD
{
	LIST_ENTRY Links;
	CHAR Buffer[IMP_LOG_SIZE];
} IMP_LOG_RECORD, *PIMP_LOG_RECORD;

typedef struct _IMP_ALLOC_RECORD
{
	LIST_ENTRY Records;
	PVOID Address;
	SIZE_T Size;
	UINT64 Flags;
	UINT64 PhysAddr;
} IMP_ALLOC_RECORD, *PIMP_ALLOC_RECORD;

typedef enum _IMP_ALLOC_FLAGS
{
	IMP_DEFAULT = (1 << 0),
	// Memory shouldn't be mapped into host memory, and should be hidden in guest memory
	IMP_SHADOW_ALLOCATION = (1 << 1),
	// Memory should remain mapped in guest memory and be mapped into host memory
	IMP_SHARED_ALLOCATION = (1 << 2),
	// Memory is only mapped into host memory and is hidden in guest memory
	IMP_HOST_ALLOCATION = (1 << 3)
} IMP_ALLOC_FLAGS, *PIMP_ALLOC_FLAGS;

extern PIMP_ALLOC_RECORD gHostAllocationsHead;

VOID 
ImpLog(
	_In_ LPCSTR Fmt, ...
);

NTSTATUS
ImpAllocateLogRecord(
	_Out_ PIMP_LOG_RECORD *LogRecord
);

NTSTATUS
ImpRetrieveLogRecord(
	_Out_ PIMP_LOG_RECORD *Record
);

NTSTATUS
ImpReserveAllocationRecords(
	_In_ SIZE_T Count
);

VOID
ImpFreeLogRecord(
	_In_ PIMP_LOG_RECORD Record
);

NTSTATUS
ImpInsertAllocRecord(
	_In_ PVOID Address,
	_In_ SIZE_T Size,
	_In_ UINT64 Flags
);

NTSTATUS
ImpReserveLogRecords(
	_In_ SIZE_T Count
);

PVOID
ImpAllocateHostContiguousMemory(
	_In_ SIZE_T Size
);

PVOID
ImpAllocateContiguousMemory(
	_In_ SIZE_T Size
);

PVOID
ImpAllocateContiguousMemoryEx(
	_In_ SIZE_T Size,
	_In_ UINT64 Flags
);

PVOID
ImpAllocateHostNpPool(
	_In_ SIZE_T Size
);

PVOID
ImpAllocateNpPool(
	_In_ SIZE_T Size
);

PVOID
ImpAllocateNpPoolEx(
	_In_ SIZE_T Size,
	_In_ UINT64 Flags
);

VOID 
ImpFreeAllocation(
	_In_ PVOID Memory
);

VOID
ImpFreeAllAllocations(VOID);

UINT64
ImpGetPhysicalAddress(
	_In_ PVOID Address
);

VOID 
ImpDebugPrint(
	_In_ PCSTR Str, ...
);

#endif
