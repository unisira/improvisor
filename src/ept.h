#ifndef IMP_EPT_H
#define IMP_EPT_H

#include <ntdef.h>

// Represents a guest physical address
typedef union _EPT_GPA
{
    UINT64 Value;

    struct
    {
        UINT64 PageOffset : 12;
        UINT64 PtIndex : 9;
        UINT64 PdIndex : 9;
        UINT64 PdptIndex : 9;
        UINT64 Pml4Index : 9;
        UINT64 Reserved1 : 16;
    };
} EPT_GPA, PEPT_GPA;

// Generic EPT PTE structure
typedef union _EPT_PTE
{
    UINT64 Value;

    struct
    {
        UINT64 ReadAccess : 1;
        UINT64 WriteAccess : 1;
        UINT64 ExecuteAccess : 1;
        UINT64 MemoryType : 3;
        UINT64 IgnorePatType : 1;
        UINT64 LargePage: 1;
        UINT64 Accessed : 1;
        UINT64 Dirty : 1;
        UINT64 UserExecuteAccess : 1;
        UINT64 Present: 1; // Ignored bit, used to check if entry is present here
        UINT64 PageFrameNumber : 36;
        UINT64 Reserved2 : 4;
        UINT64 Ignored2 : 8; // TODO: Use these bits to show what these pages are used for
                             //       (EPT hooks, shadow pages for host-owned memory etc.)
        UINT64 SupervisorShadowStack : 1;
        UINT64 Ignored3 : 2;
        UINT64 SuppressVE : 1;
    };
} EPT_PTE, *PEPT_PTE;

typedef enum _EPT_PAGE_PERMISSIONS
{
    EPT_PAGE_INVALID = 0,

    EPT_PAGE_READ = (1 << 0),
    EPT_PAGE_WRITE = (1 << 1),
    EPT_PAGE_EXECUTE = (1 << 2),
    EPT_PAGE_UEXECUTE = (1 << 3),

    EPT_PAGE_RX = EPT_PAGE_READ | EPT_PAGE_UEXECUTE,
    EPT_PAGE_RUX = EPT_PAGE_READ | EPT_PAGE_EXECUTE,
    EPT_PAGE_RW = EPT_PAGE_READ | EPT_PAGE_WRITE,
    EPT_PAGE_RWX = EPT_PAGE_READ | EPT_PAGE_WRITE | EPT_PAGE_EXECUTE,
    EPT_PAGE_RWUX = EPT_PAGE_READ | EPT_PAGE_WRITE | EPT_PAGE_UEXECUTE
} EPT_PAGE_PERMISSIONS, *PEPT_PAGE_PERMISSIONS;

NTSTATUS
EptInitialise(VOID);

#endif
