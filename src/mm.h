#ifndef IMP_MM_H
#define IMP_MM_H

#include <ntdef.h>

typedef struct _MM_SUPPORT
{
	UINT64 HostDirectoryPhysical;
} MM_SUPPORT, *PMM_SUPPORT;

typedef union _MM_PTE
{
    UINT64 Value;

    struct
    {
        UINT64 Present : 1;
        UINT64 WriteAllowed : 1;
        UINT64 SupervisorOwned : 1;
        UINT64 WriteThrough : 1;
        UINT64 CacheDisable : 1;
        UINT64 Accessed : 1;
        UINT64 Dirty : 1; 
        UINT64 LargePage : 1; 
        UINT64 Global : 1;
        UINT64 Ignored1 : 3; // Windows has Write (1), CoW (3) here 
        UINT64 PageFrameNumber : 40;
        UINT64 Ignored3 : 11;
        UINT64 ExecuteDisable : 1;
    };
} MM_PTE, *PMM_PTE;

NTSTATUS
MmInitialise(
	_Inout_ PMM_SUPPORT MmSupport
);

#endif
