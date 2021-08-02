#ifndef IMP_IMPROVISOR_H
#define IMP_IMPROVISOR_H

#include <ntddk.h>
#include <ntdef.h>
#include <wdm.h>
#include <stdarg.h>
#include <intrin.h>

#define POOL_TAG 'IMPV'

#define BUGCHECK_FAILED_SHUTDOWN 0x00001000
#define BUGCHECK_UNKNOWN_VMEXIT_REASON 0x00002000

// TODO Functions:
// To keep track of pool allocations so they can be hid from physical memory
// ImpAllocateNonPagedPool(
//     _In_ SIZE_T Size
// );

typedef struct _IMP_ALLOC_RECORD
{
    LIST_ENTRY Records;
    PVOID Address;
    SIZE_T Size;
} IMP_ALLOC_RECORD, *PIMP_ALLOC_RECORD;

extern PIMP_ALLOC_RECORD gHostAllocationsHead;

NTSTATUS
ImpReserveAllocationRecords(
    _In_ SIZE_T Count
);

PVOID
ImpAllocateContiguousMemory(
    _In_ SIZE_T Size
);

PVOID
ImpAllocateNpPool(
    _In_ SIZE_T Size
);

UINT64
ImpGetPhysicalAddress(
    _In_ PVOID Address
);

VOID 
ImpDebugPrint(
    _In_ PCSTR Str, ...
);

#endif
