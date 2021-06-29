#include "improvisor.h"

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

    // TODO: Allocation logging
    PHYSICAL_ADDRESS MaxAcceptableAddr;
    MaxAcceptableAddr.QuadPart = ~0ULL;

    Address = MmAllocateContiguousMemory(Size, MaxAcceptableAddr);

    RtlSecureZeroMemory(Address, Size);

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
#end
}
