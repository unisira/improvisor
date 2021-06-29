#ifndef IMP_IMPROVISOR_H
#ifndef IMP_IMPROVISOR_H

#include <wdm.h>
#include <ntddk.h>
#include <stdarg.h>

#define POOL_TAG 'IMPV'

#define BUGCHECK_FAILED_SHUTDOWN 0x00001000
#define BUGCHECK_UNKNOWN_VMEXIT_REASON 0x00002000

// TODO Functions:
// To keep track of pool allocations so they can be hid from physical memory
// ImpAllocateNonPagedPool(
//     _In_ SIZE_T Size
// );

PVOID
ImpAllocateContiguousMemory(
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
