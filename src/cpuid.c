#include "cpuid.h"

typedef struct _CPUID_CACHE_ENTRY
{
    X86_CPUID_ARGS Result; 
} CPUID_CACHE_ENTRY, *PCPUID_CACHE_ENTRY;

VOID
CpuidSetupCache(VOID)
{
    // TODO: Complete this
}

PCPUID_CACHE_ENTRY
CpuidGetCacheEntry(
    _In_ UINT32 Function,
    _In_ UINT32 SubFunction
)
/*++
Routine Description:
	Gets a CPUID cache entry for a certain function and subfunction combo
--*/
{
    // TODO: Complete this
    return NULL;
}

VOID
CpuidEmulateGuestCallUncached(
    _Inout_ PX86_CPUID_ARGS Args
)
/*++
Routine Description:
    Emulates a guest CPUID call without using the caches 
--*/
{
    // TODO: Complete this
    return;
}

VOID
CpuidEmulateGuestCall(
    _Inout_ PX86_CPUID_ARGS Args
)
/*++
Routine Description:
    Emulates a call to the CPUID instruction from the guest operating system
--*/
{
    PCPUID_CACHE_ENTRY CacheEntry = CpuidGetCacheEntry(Args->Eax, Args->Ecx);

    if (CacheEntry == NULL)
        CpuidEmulateGuestCallUncached(Args);
    else
    {
        for (UINT8 i = 0; i < 4; i++)
        {
            Args->Data[i] = CacheEntry->Result.Data[i];
        }
    }
}
