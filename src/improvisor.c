#include "improvisor.h"

PIMP_ALLOC_RECORD gHostAllocationsHead;

// The raw buffer containing the host allocation records
static PIMP_ALLOC_RECORD sImpAllocRecordsRaw = NULL;

NTSTATUS
ImpInsertAllocRecord(
    _In_ PVOID Address,
    _In_ SIZE_T Size
)
/*++
Routine Description:
    Inserts a new allocation record into the list head
--*/
{
    PIMP_ALLOC_RECORD AllocRecord = gHostAllocationsHead;
    if (AllocRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    AllocRecord->Address = Address;
    AllocRecord->Size = Size;

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
    sImpAllocRecordsRaw = (PIMP_ALLOC_RECORD)ExAllocatePoolWithTag(NonPagedPool, sizeof(IMP_ALLOC_RECORD) * (Count + 1), POOL_TAG);

    if (sImpAllocRecordsRaw == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    // Set the head to be the first entry
    gHostAllocationsHead = sImpAllocRecordsRaw;

    for (UINT8 i = 0; i < Count + 1; i++)
    {
        PIMP_ALLOC_RECORD CurrAllocRecord = sImpAllocRecordsRaw + i;

        // Set up Flink and Blink
        CurrAllocRecord->Records.Flink = i < Count + 1  ? &(CurrAllocRecord + 1)->Records : NULL;
        CurrAllocRecord->Records.Blink = i > 0          ? &(CurrAllocRecord - 1)->Records : NULL
    }

    // Insert the record for block of records 
    if (!NT_SUCCESS(ImpInsertAllocRecord(sImpAllocRecordsRaw, sizeof(IMP_ALLOC_RECORD) * (Count + 1))))
        return STATUS_INSUFFICIENT_RESOURCES;

    return STATUS_SUCCESS;
}


PVOID
ImpAllocateContiguousMemory(
    _In_ SIZE_T Size
)
/*++
Routine Description:
    This function will allocate a zero'd block of contiguous memory and keep track of said allocations, 
    so that they can later be hidden when initialising the EPT identity map for the guest environment
--*/
{
    PVOID Address = NULL;

    PHYSICAL_ADDRESS MaxAcceptableAddr;
    MaxAcceptableAddr.QuadPart = ~0ULL;

    Address = MmAllocateContiguousMemory(Size, MaxAcceptableAddr);
    if (Address == NULL)
        return NULL;
	
    RtlSecureZeroMemory(Address, Size);

    if (!NT_SUCCESS(ImpInsertAllocRecord(Address, Size)))
    {
        ImpDebugPrint("Couldn't record Alloc(%llX, %x), no more allocation records...\n", Address, Size);
        return NULL;
    }

    return Address;
}

PVOID
ImpAllocateNpPool(
    _In_ SIZE_T Size
)
/*++
Routine Description:
    This function allocates a zero'd non-paged pool of memory using ExAllocatePoolWithTag and records the
    allocation in the pool records
--*/
{
    PVOID Address = NULL;
    
    Address = ExAllocatePoolWithTag(NonPagedPool, Size, POOL_TAG);
    if (Address == NULL)
        return NULL;

    RtlSecureZeroMemory(Address, Size);

    if (!NT_SUCCESS(ImpInsertAllocRecord(Address, Size)))
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
