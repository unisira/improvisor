#include <improvisor.h>
#include <arch/memory.h>
#include <os/pe.h>


BOOLEAN
PeValidateImageBaseAddress(
	_In_ PVOID BaseAddress
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

	return RVA_PTR(BaseAddress, Dir.VirtualAddress);
}

PVOID
PePdbSymbolResultToAddress(
	_In_ PVOID BaseAddress,
	_In_ PPDB_SYMBOL_RESULT Res
)
{
	PIMAGE_NT_HEADERS NTHeaders = PeGetNTHeaders(BaseAddress);
	if (NTHeaders == NULL)
		return NULL;
	
	// Make sure the specified segment number is within the bounds of this PE image
	if (Res->Segment >= NTHeaders->FileHeader.NumberOfSections)
		return NULL;

	PIMAGE_SECTION_HEADER Section = &IMAGE_FIRST_SECTION(NTHeaders)[Res->Segment];

	if (Section->VirtualAddress == 0 || Section->Misc.VirtualSize == 0)
		return NULL;

	return RVA_PTR(BaseAddress, Section->VirtualAddress + Res->Offset);
}