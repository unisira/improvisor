#ifndef IMP_EPT_H
#define IMP_EPT_H

#include <ntdef.h>

typedef enum _EPT_MEMORY_TYPE
{
	EPT_MEMORY_UNCACHEABLE = 0,
	EPT_MEMORY_WRITECOMBINING = 1,
	EPT_MEMORY_WRITETHROUGH = 4,
	EPT_MEMORY_WRITEPROTECTED = 5,
	EPT_MEMORY_WRITEBACK = 6,
} EPT_MEMORY_TYPE, *PEPT_MEMORY_TYPE;


typedef union _EPT_POINTER
{
	UINT64 Value;

	struct
	{
		UINT64 MemoryType : 3;
		UINT64 PageWalkLength : 3;
		UINT64 AccessDirtyFlags : 1;
		UINT64 SupervisorShadowStackAccess : 1;
		UINT64 Reserved1 : 4;
		UINT64 PML4PageFrameNumber : 36;
		UINT64 Reserved2 : 16;
	};
} EPT_POINTER, *PEPT_POINTER;

typedef struct _EPT_INVEPT_DESCRIPTOR
{
	EPT_POINTER EptPointer;
	UINT64 Reserved;
} EPT_INVEPT_DESCRIPTOR, *PEPT_INVEPT_DESCRIPTOR;

typedef union _EPT_VIOLATION_EXIT_QUALIFICATION
{
	UINT64 Value;

	struct
	{
		UINT64 ReadAccessed : 1;
		UINT64 WriteAccessed : 1;
		UINT64 ExecuteAccessed : 1;
		UINT64 ReadPermission : 1;
		UINT64 WritePermission : 1;
		UINT64 ExecutePermission : 1;
		UINT64 UsermodeExecute : 1;
		UINT64 IsGuestLinearAddrValid : 1;
		UINT64 PageWalkOrTranslationFail : 1;
		UINT64 Reserved1 : 3;
		UINT64 NmiUnblocking : 1;
		UINT64 Reserved2 : 51;
	};
} EPT_VIOLATION_EXIT_QUALIFICATION, *PEPT_VIOLATION_EXIT_QUALIFICATION;

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

typedef struct _EPT_INFORMATION
{
	PEPT_PTE SystemPml4;
	PVOID DummyPage;
	UINT64 DummyPagePhysAddr;
} EPT_INFORMATION, *PEPT_INFORMATION;

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

VOID
EptInvalidateCache(VOID);

BOOLEAN
EptCheckSupport(VOID);

NTSTATUS
EptMapMemoryRange(
	_In_ PEPT_PTE Pml4,
	_In_ UINT64 GuestPhysAddr,
	_In_ UINT64 PhysAddr,
	_In_ UINT64 Size,
	_In_ EPT_PAGE_PERMISSIONS Permissions
);

NTSTATUS
EptInitialise(
	_Inout_ PEPT_INFORMATION
);

#endif
