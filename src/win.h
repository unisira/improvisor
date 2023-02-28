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
WinFindProcess(
	_In_ FNV1A ProcessName
);

#endif
