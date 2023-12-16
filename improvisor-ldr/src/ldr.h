#pragma once
#include "win.h"

// TODO: LDR PE image that holds name, info for 
typedef struct _LDR_PE_IMAGE
{
	LIST_ENTRY Links;
	// The exe name for this 
	CHAR Name[MAX_PATH];
	// The memory storing the image
	PVOID ImageBuffer;
	// The size of the image buffer
	SIZE_T ImageSize;
	// The memory storing the PDB for this image
	PVOID PdbBuffer;
	// The size of the PDB buffer
	SIZE_T PdbSize;
} LDR_PE_IMAGE, * PLDR_PE_IMAGE;

VOID
LdrDownloadPdb(
	PLDR_PE_IMAGE Pe
);

VOID
LdrSetup(
	VOID
);