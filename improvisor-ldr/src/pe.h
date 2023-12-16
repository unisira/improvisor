#ifndef IMP_PE_H
#define IMP_PE_H

#include <winternl.h>
#include <Windows.h>

typedef struct _IMAGE_DEBUG_INFORMATION
{
	DWORD     Signature;
	GUID      Guid;
	DWORD     Age;
	CHAR      PdbFileName[1];
} IMAGE_DEBUG_INFORMATION, * PIMAGE_DEBUG_INFORMATION;

PIMAGE_NT_HEADERS
PeGetNTHeaders(
	_In_ PVOID BaseAddress
);

PIMAGE_SECTION_HEADER
PeGetSectionHeaders(
	_In_ PVOID BaseAddress,
	_Out_ PSIZE_T SectionCount
);

UINT_PTR
PeRVAToFileOffset(
	PVOID BaseAddress,
	UINT_PTR RVA
);

PVOID
PeImageDirectoryEntryToData(
	_In_ PVOID BaseAddress,
	_In_ UINT32 Index,
	_Out_ PSIZE_T Size
);

#endif