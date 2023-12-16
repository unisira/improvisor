#pragma once
#include <Windows.h>
#include <winternl.h>

typedef NTSTATUS(NTAPI ZW_CREATE_SECTION)(
	OUT PHANDLE            SectionHandle,
	IN ULONG               DesiredAccess,
	IN POBJECT_ATTRIBUTES  ObjectAttributes OPTIONAL,
	IN PLARGE_INTEGER      MaximumSize OPTIONAL,
	IN ULONG               PageAttributess,
	IN ULONG               SectionAttributes,
	IN HANDLE              FileHandle OPTIONAL
);


typedef NTSTATUS(NTAPI NT_MAP_VIEW_OF_SECTION)(
	HANDLE			SectionHandle,
	HANDLE			ProcessHandle,
	PVOID*			BaseAddress,
	ULONG_PTR		ZeroBits,
	SIZE_T			CommitSize,
	PLARGE_INTEGER	SectionOffset,
	PSIZE_T			ViewSize,
	DWORD			InheritDisposition,
	ULONG			AllocationType,
	ULONG			Win32Protect
);

ZW_CREATE_SECTION ZwCreateSection;
NT_MAP_VIEW_OF_SECTION NtMapViewOfSection;