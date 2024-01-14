#ifndef IMP_WIN_H
#define IMP_WIN_H

#include <ntdef.h>
#include <hash.h>

// Undocumented routine definitions

NTSTATUS 
NTAPI 
ObReferenceObjectByName(
	PUNICODE_STRING ObjectPath,
	ULONG Attributes,
	PACCESS_STATE PassedAccessState OPTIONAL,
	ACCESS_MASK DesiredAccess OPTIONAL,
	POBJECT_TYPE ObjectType,
	KPROCESSOR_MODE AccessMode,
	PVOID ParseContext OPTIONAL,
	_Out_ PVOID* ObjectPtr
);

NTSTATUS
WinInitialise(VOID);

PVOID
WinGetGuestGs(VOID);

PVOID
WinGetCurrentThread(VOID);

PVOID
WinGetCurrentProcess(VOID);

BOOLEAN
WinGetProcessName(
	_In_ PVOID Process,
	_Out_ PCHAR Buffer
);

ULONG_PTR
WinGetProcessID(
	_In_ PVOID Process
);

PVOID
WinFindProcess(
	_In_ FNV1A ProcessName
);

PVOID
WinFindProcessById(
	_In_ ULONG_PTR ProcessId
);

#endif
