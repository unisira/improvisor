#include "improvisor.h"
#include "util/fmt.h"

// TODO: Add a logger, and add routine descriptions for everything

PIMP_ALLOC_RECORD gHostAllocationsHead = NULL;

// The raw buffer containing the host allocation records
static PIMP_ALLOC_RECORD sImpAllocRecordsRaw = NULL;

VOID
ImpLog(
    _In_ LPCSTR Fmt, ...
)
/*++
Routine Description:
    Creates a log entry with the contents of `Fmt` formatted with variadic args
--*/
{
    CHAR Buffer[512];

    va_list Arg;
    va_start(Arg, Fmt);
    vsprintf_s(Buffer, 512, Fmt, Arg);
    va_end(Arg);

    // TODO: Create log record and enter it in list, update log index
}

NTSTATUS
ImpInsertAllocRecord(
    _In_ PVOID Address,
    _In_ SIZE_T Size,
    _In_ UINT64 Flags
)
/*++
Routine Description:
    Inserts a new allocation record into the list head

    These records will be used later in VmmStartHypervisor once the hypervisor has started running successfully, they will be iterated over
    and hidden using HYPERCALL_EPT_MAP_PAGES if the allocation flags say it should be hidden from guest OS 
--*/
{
    // TODO: Make this not waste 1 allocation record, same for other funcs in mm.c
    if (gHostAllocationsHead->Records.Flink == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    PIMP_ALLOC_RECORD AllocRecord = gHostAllocationsHead;

    AllocRecord->Address = Address;
    AllocRecord->Size = Size;
    AllocRecord->Flags = Flags;

    gHostAllocationsHead = (PIMP_ALLOC_RECORD)AllocRecord->Records.Flink; 

    return STATUS_SUCCESS;
}

NTSTATUS
ImpReserveAllocationRecords(
    _In_ SIZE_T Count
)
/*++
Routine Description:
    Reserves Count+1 allocation records so we can log them in a linked list fashion, the +1 is because we 
    are making 1 more allocation for the actual block of memory containing these records
--*/
{
    // TODO: Add allocation flags (HIDE FROM EPT etc...)
    sImpAllocRecordsRaw = (PIMP_ALLOC_RECORD)ExAllocatePoolWithTag(NonPagedPool, sizeof(IMP_ALLOC_RECORD) * (Count + 1), POOL_TAG);

    if (sImpAllocRecordsRaw == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlSecureZeroMemory(sImpAllocRecordsRaw, sizeof(IMP_ALLOC_RECORD) * (Count + 1));

    // Set the head to be the first entry
    gHostAllocationsHead = sImpAllocRecordsRaw;

    for (SIZE_T i = 0; i < Count + 1; i++)
    {
        PIMP_ALLOC_RECORD CurrAllocRecord = sImpAllocRecordsRaw + i;

        // Set up Flink and Blink
        CurrAllocRecord->Records.Flink = i < Count + 1  ? &(CurrAllocRecord + 1)->Records : NULL;
        CurrAllocRecord->Records.Blink = i > 0          ? &(CurrAllocRecord - 1)->Records : NULL;
    }

    // Insert the record for block of records 
    if (!NT_SUCCESS(ImpInsertAllocRecord(sImpAllocRecordsRaw, sizeof(IMP_ALLOC_RECORD) * (Count + 1), IMP_DEFAULT)))
        return STATUS_INSUFFICIENT_RESOURCES;

    return STATUS_SUCCESS;
}

PVOID
ImpAllocateHostContiguousMemory(
    _In_ SIZE_T Size
)
/*++
Routine Description:
    This function allocates a zero'd contiguous block of memory using ImpAllocateContiguousMemoryEx and 
    signals that it should be mapped to an empty page using EPT
--*/
{
    return ImpAllocateContiguousMemory(Size, IMP_SHADOW_ALLOCATION);
}

PVOID
ImpAllocateContiguousMemory(
    _In_ SIZE_T Size
)
/*++
Routine Description:
    This function allocates a zero'd contiguous block of memory using ImpAllocateContiguousMemoryEx 
--*/

{
    return ImpAllocateContiguousMemoryEx(Size, 0);
}

PVOID
ImpAllocateContiguousMemoryEx(
    _In_ SIZE_T Size,
    _In_ UINT64 Flags
)
/*++
Routine Description:
    This function allocates a zero'd contiguous block of memory using MmAllocateContiguousMemory and records the
    allocation in the pool records if needed
--*/
{
    PVOID Address = NULL;

    PHYSICAL_ADDRESS MaxAcceptableAddr;
    MaxAcceptableAddr.QuadPart = ~0ULL;

    Address = MmAllocateContiguousMemory(Size, MaxAcceptableAddr);
    if (Address == NULL)
        return NULL;
	
    RtlSecureZeroMemory(Address, Size);

    if (!NT_SUCCESS(ImpInsertAllocRecord(Address, Size, Flags)))
    {
        ImpDebugPrint("Couldn't record Alloc(%llX, %x), no more allocation records...\n", Address, Size);
        return NULL;
    }

    return Address;
}

PVOID
ImpAllocateHostNpPool(
    _In_ SIZE_T Size
)
/*++
Routine Description:
    This function allocates a zero'd non-paged pool using ImpAllocateNpPoolEx and signals that it should be
    mapped to an empty page using EPT
--*/
{
    return ImpAllocateNpPoolEx(Size, IMP_SHADOW_ALLOCATION);
}

PVOID
ImpAllocateNpPool(
    _In_ SIZE_T Size
)
/*++
Routine Description:
    This function allocates a zero'd non-paged pool of memory using ImpAllocateNpPoolEx
--*/
{
    return ImpAllocateNpPoolEx(Size, 0);
}    

PVOID
ImpAllocateNpPoolEx(
    _In_ SIZE_T Size,
    _In_ UINT64 Flags
)
/*++
Routine Description:
    This function allocates a zero'd non-paged pool of memory using ExAllocatePoolWithTag and records the
    allocation in the pool records if needed
--*/
{
    PVOID Address = NULL;
    
    Address = ExAllocatePoolWithTag(NonPagedPool, Size, POOL_TAG);
    if (Address == NULL)
        return NULL;

    RtlSecureZeroMemory(Address, Size);

    if (!NT_SUCCESS(ImpInsertAllocRecord(Address, Size, Flags)))
    {
        ImpDebugPrint("Couldn't record Alloc(%llX, %x), no more allocation records...\n", Address, Size);
        return NULL;
    }

    return Address;

}

UINT64
ImpGetPhysicalAddress(
    _In_ PVOID Address
)
/*++
Routine Description:
    Wrapper around MmGetPhysicalAddress to get rid of the annoying return type to make code cleaner
--*/
{
    return MmGetPhysicalAddress(Address).QuadPart;
}

VOID 
ImpDebugPrint(
    _In_ PCSTR Str, ...
)
/*++
Routine Description:
    Just a wrapper around DbgPrint, making sure to print to the correct output depending on the build
--*/
{
#ifdef _DEBUG
    va_list Args;
	va_start(Args, Str);

	vDbgPrintExWithPrefix("[Improvisor DEBUG]: ", DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, Str, Args);

	va_end(Args);
#else
    DbgPrint(Str, ...);
#endif
}
