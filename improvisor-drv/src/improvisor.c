#include <improvisor.h>
#include <spinlock.h>
#include <fmt.h>
#include <ll.h>

#define IMPV_LOG_SIZE 512
#define IMPV_LOG_COUNT 512

VMM_DATA PIMP_ALLOC_RECORD gHostAllocationsHead = NULL;
// The raw buffer containing the host allocation records
VMM_DATA static PIMP_ALLOC_RECORD sImpAllocRecordsRaw = NULL;
// The linked list object pool containing log records
VMM_DATA static LINKED_LIST_POOL sLogRecordPool;

VSC_API
NTSTATUS
ImpReserveLogRecords(
	_In_ SIZE_T Count
)
{
	return LL_CREATE_POOL(&sLogRecordPool, IMP_LOG_RECORD, Count);
}

VMM_API
NTSTATUS
ImpAllocateLogRecord(
	_Out_ PIMP_LOG_RECORD* Record
)
{
	if (Record == NULL)
		return STATUS_INVALID_PARAMETER;

	PVOID Result = LlAllocate(&sLogRecordPool);
	// If `Result` is null, there are no used entries
	if (Result == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	*Record = Result;

	return STATUS_SUCCESS;
}

VMM_API
VOID
ImpLog(
	_In_ LPCSTR Fmt, ...
)
/*++
Routine Description:
	Creates a log entry with the contents of `Fmt` formatted with variadic args
--*/
{
	PIMP_LOG_RECORD Log = NULL;
	if (!NT_SUCCESS(ImpAllocateLogRecord(&Log)))
		return;

	va_list Arg;
	va_start(Arg, Fmt);
	vsprintf_s(Log->Buffer, 512, Fmt, Arg);
	va_end(Arg);
}

VMM_API
NTSTATUS
ImpRetrieveLogRecord(
	_Out_ PIMP_LOG_RECORD* Record
)
/*++
Routine Description:
	Returns the oldest entry from the log record pool 
--*/
{
	if (Record == NULL)
		return STATUS_INVALID_PARAMETER;

	// Grab the first inserted record
	PVOID Result = LlBegin(&sLogRecordPool);
	// If `Result` is null, there are no used entries
	if (Result == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	*Record = Result;

	return STATUS_SUCCESS;
}

VMM_API
VOID
ImpFreeLogRecord(
	_In_ PIMP_LOG_RECORD Record
)
/*++
Routine Description:
	Marks `Record` as free
--*/
{
	LlFree(&sLogRecordPool, &Record->Links);
}

VSC_API
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
	AllocRecord->PhysAddr = ImpGetPhysicalAddress(Address);

	gHostAllocationsHead = (PIMP_ALLOC_RECORD)AllocRecord->Records.Flink; 

	return STATUS_SUCCESS;
}

VSC_API
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

VSC_API
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
	return ImpAllocateContiguousMemoryEx(Size, IMP_HOST_ALLOCATION);
}

VSC_API
PVOID
ImpAllocateContiguousMemory(
	_In_ SIZE_T Size
)
/*++
Routine Description:
	This function allocates a zero'd contiguous block of memory using ImpAllocateContiguousMemoryEx 
--*/

{
	return ImpAllocateContiguousMemoryEx(Size, IMP_DEFAULT);
}

VSC_API
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

VSC_API
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
	return ImpAllocateNpPoolEx(Size, IMP_HOST_ALLOCATION);
}

VSC_API
PVOID
ImpAllocateNpPool(
	_In_ SIZE_T Size
)
/*++
Routine Description:
	This function allocates a zero'd non-paged pool of memory using ImpAllocateNpPoolEx
--*/
{
	return ImpAllocateNpPoolEx(Size, IMP_DEFAULT);
}    

VSC_API
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

VOID
ImpFreeAllocation(
	_In_ PVOID Memory
)
/*++
Routine Description:
	Frees a specific allocation and removes the allocation entry
--*/
{
	PIMP_ALLOC_RECORD CurrRecord = gHostAllocationsHead;
	while (CurrRecord != NULL)
	{
		if (CurrRecord->Address == Memory)
		{
			ExFreePoolWithTag(CurrRecord->Address, POOL_TAG);
			// Break and unlink this allocation
			break;
		}

		CurrRecord = (PIMP_ALLOC_RECORD)CurrRecord->Records.Blink;
	}

	// No allocation was found with address `Memory`
	if (CurrRecord == NULL)
		return;

	CurrRecord->Address = NULL;
	CurrRecord->PhysAddr = 0;
	CurrRecord->Size = 0;
	CurrRecord->Flags = 0;

	PIMP_ALLOC_RECORD Next = CurrRecord->Records.Flink;
	PIMP_ALLOC_RECORD Prev = CurrRecord->Records.Blink;

	if (Prev != NULL)
		Prev->Records.Flink = &Next->Records;
	if (Next != NULL)
		Next->Records.Blink = &Prev->Records;

	PIMP_ALLOC_RECORD Head = gHostAllocationsHead;
	// The original head is now the previous entry to the new head
	CurrRecord->Records.Blink = &Head->Records;
	// The original head's next entry is now the next entry for us too
	CurrRecord->Records.Flink = Head->Records.Flink;

	gHostAllocationsHead = CurrRecord;
}

VOID
ImpFreeAllAllocations(VOID)
/*++
Routine Description:
	Frees all allocations made by Imp* functions and the buffers used to record them
--*/
{
	PIMP_ALLOC_RECORD CurrRecord = gHostAllocationsHead;
	while (CurrRecord != NULL)
	{
		ExFreePoolWithTag(CurrRecord->Address, POOL_TAG);

		CurrRecord = (PIMP_ALLOC_RECORD)CurrRecord->Records.Blink;
	}

	ExFreePoolWithTag(sImpAllocRecordsRaw, POOL_TAG);
}

VSC_API
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

VMM_DATA static SPINLOCK sDebugPrintLock;

VSC_API
VOID 
ImpDebugPrint(
	_In_ PCSTR Str, ...
)
/*++
Routine Description:
	Just a wrapper around DbgPrint, making sure to print to the correct output depending on the build
--*/
{
	va_list Args;
	va_start(Args, Str);

	vDbgPrintExWithPrefix("", DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, Str, Args);

	va_end(Args);
}
