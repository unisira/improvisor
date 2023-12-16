#include "pe.h"
#include "macro.h"

#include <winternl.h>
#include <Windows.h>

BOOL
PeValidateImageBaseAddress(
	PVOID BaseAddress
)
/*++
Routine Description:
	Validates a `BaseAddress` to see if a PE image starts there
--*/
{
	// TODO: Improve this
	if (BaseAddress == NULL)
		return FALSE;

	PIMAGE_DOS_HEADER DosHeader = BaseAddress;
	if (DosHeader->e_magic != (USHORT)'ZM')
		return FALSE;

	return TRUE;
}

PIMAGE_NT_HEADERS
PeGetNTHeaders(
	_In_ PVOID BaseAddress
)
{
	if (!PeValidateImageBaseAddress(BaseAddress))
		return NULL;

	PIMAGE_DOS_HEADER DosHeader = BaseAddress;

	return (PIMAGE_NT_HEADERS)((PUCHAR)BaseAddress + DosHeader->e_lfanew);
}

PIMAGE_SECTION_HEADER
PeGetSectionHeaders(
	_In_ PVOID BaseAddress,
	_Out_ PSIZE_T SectionCount
)
{
	PIMAGE_NT_HEADERS NTHeaders = PeGetNTHeaders(BaseAddress);
	if (NTHeaders == NULL)
	{
		if (SectionCount)
			*SectionCount = -1;

		return NULL;
	}

	if (SectionCount)
		*SectionCount = NTHeaders->FileHeader.NumberOfSections;

	// Sections are contiguous, return a pointer to the first one
	return IMAGE_FIRST_SECTION(NTHeaders);
}

UINT_PTR
PeRVAToFileOffset(
	PVOID BaseAddress,
	UINT_PTR RVA
)
{
	PIMAGE_NT_HEADERS NTHeaders = PeGetNTHeaders(BaseAddress);
	if (NTHeaders == NULL)
		return NULL;

	for (SIZE_T i = 0; i < NTHeaders->FileHeader.NumberOfSections; i++)
	{
		PIMAGE_SECTION_HEADER Section = &IMAGE_FIRST_SECTION(NTHeaders)[i];

		if (RVA >= Section->VirtualAddress && RVA < Section->VirtualAddress + Section->Misc.VirtualSize)
			return RVA - Section->VirtualAddress + Section->PointerToRawData;
	}

	return -1;
}

PVOID
PeImageDirectoryEntryToData(
	_In_ PVOID BaseAddress,
	_In_ UINT32 Index,
	_Out_ PSIZE_T Size
)
{
	PIMAGE_NT_HEADERS NTHeaders = PeGetNTHeaders(BaseAddress);
	if (NTHeaders == NULL)
		return NULL;

	IMAGE_DATA_DIRECTORY Dir = NTHeaders->OptionalHeader.DataDirectory[Index];
	if (Dir.VirtualAddress == 0 || Dir.Size == 0)
		return NULL;

	// Write the size of the data
	if (Size != NULL)
		*Size = Dir.Size;

	return RVA_PTR(BaseAddress, PeRVAToFileOffset(BaseAddress, Dir.VirtualAddress));
}