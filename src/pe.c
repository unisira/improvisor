#include "improvisor.h"
#include "pe.h"

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