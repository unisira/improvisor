#ifndef IMP_DETOUR_H
#define IMP_DETOUR_H

#include <hash.h>

#include <ntdef.h>
#include <wdm.h>

typedef enum _EH_DETOUR_STATE
{
	EH_DETOUR_INVALID = 0,
	EH_DETOUR_REGISTERED,
	EH_DETOUR_INSTALLED,
	EH_DETOUR_DISABLED
} EH_DETOUR_STATE, * PEH_DETOUR_STATE;

typedef struct _EH_HOOK_REGISTRATION
{
	LIST_ENTRY Links;
	FNV1A Hash;
	EH_DETOUR_STATE State;
	PVOID TargetFunction;
	PVOID ShadowPage;
	PVOID Trampoline;
	PVOID CallbackFunction;
	SIZE_T PrologueSize;
	UINT64 ShadowPhysAddr;
	UINT64 GuestPhysAddr;
	PMDL LockedTargetPage;
} EH_DETOUR_REGISTRATION, *PEH_DETOUR_REGISTRATION;

NTSTATUS
EhRegisterDetour(
	_In_ FNV1A Hash,
	_In_ PVOID Target,
	_In_ PVOID Callback
);

VOID
EhDisableDetour(
	_In_ PEH_DETOUR_REGISTRATION Hook
);

VOID
EhEnableDetour(
	_In_ PEH_DETOUR_REGISTRATION Hook
);

NTSTATUS
EhInitialise(VOID);

BOOLEAN
EhHandleEptViolation(
	_In_ PVCPU Vcpu
);

BOOLEAN
EhHandleBreakpoint(
	_In_ PVCPU Vcpu
);

#endif
