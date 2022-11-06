#ifndef IMP_PE_H
#define IMP_PE_H

#include <ntdef.h>
#include <ntimage.h>

PIMAGE_NT_HEADERS
PeGetNTHeaders(
	_In_ PVOID BaseAddress
);

PIMAGE_SECTION_HEADER
PeGetSectionHeaders(
	_In_ PVOID BaseAddress,
	_Out_ PSIZE_T SectionCount
);

PVOID
PeImageDirectoryEntryToData(
	_In_ PVOID BaseAddress,
	_In_ UINT32 Index,
	_Out_ PSIZE_T Size
);

#endif