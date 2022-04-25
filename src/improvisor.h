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

typedef struct _IMP_LOG_RECORD
{
    LIST_ENTRY Links;
    CHAR Buffer[512];
} IMP_LOG_RECORD, * PIMP_LOG_RECORD;

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
    IMP_SHADOW_ALLOCATION = (1 << 1)
} IMP_ALLOC_FLAGS, *PIMP_ALLOC_FLAGS;

extern PIMP_ALLOC_RECORD gHostAllocationsHead;
extern PIMP_LOG_RECORD gLogRecordsHead;
extern PIMP_LOG_RECORD gLogRecordsTail;

VOID
ImpLog(
    _In_ LPCSTR Fmt, ...
);

NTSTATUS
ImpRetrieveLogRecord(
    _Out_ PIMP_LOG_RECORD* Record
);

NTSTATUS
ImpReserveAllocationRecords(
    _In_ SIZE_T Count
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
