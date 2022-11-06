#ifndef IMP_LDR_H
#define IMP_LDR_H

#include <hash.h>
#include <ntdef.h>
#include <ntimage.h>
#include <wdm.h>

// PDB 'requesting':

typedef enum _LDR_LAUNCH_FLAGS
{
	// Boot the improvisor with mitigations specific to BattlEye
	LDR_LAUNCH_BATTLEYE,
	// Boot the improvisor with mitigations specific to EasyAntiCheat
	LDR_LAUNCH_EAC,
	// Boot the improvisor and install all Watchmen features for analysis of anti-cheats
	LDR_LAUNCH_WATCHMEN,
	// The improvisor was loaded in debug mode, meaning no vulnerable driver was used
	LDR_LAUNCH_DEBUG
} LDR_LAUNCH_FLAGS, *PLDR_LAUNCH_FLAGS;

typedef struct _LDR_LAUNCH_PARAMS
{
	FNV1A BuildHash;
	PVOID ImageBase;
	SIZE_T ImageSize;
	SIZE_T SectionCount;
	PIMAGE_SECTION_HEADER Sections;
	PIMAGE_NT_HEADERS Headers;
	LDR_LAUNCH_FLAGS Flags;
	// The path to the exe which launched the improvisor, NULL if LDR_LAUNCH_PARAMS::Flags & LDR_LAUNCH_DEBUG is non-zero
	UNICODE_STRING ClientPath;
	// The process id of the client
	HANDLE ClientID;
} LDR_LAUNCH_PARAMS, *PLDR_LAUNCH_PARAMS;

extern PLDR_LAUNCH_PARAMS gLdrLaunchInfo;

PIMAGE_SECTION_HEADER
LdrGetSectionByName(
	_In_ FNV1A Name
);

PVOID
LdrImageDirectoryEntryToData(
	_In_ UINT32 Index,
	_Out_ PSIZE_T Size
);

NTSTATUS
LdrInitialise(
	_In_ PUNICODE_STRING SharedSectionName
);

#endif