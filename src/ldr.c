#include <improvisor.h>
#include <arch/memory.h>
#include <pdb/pdb.h>
#include <ldr.h>

VMM_DATA PLDR_LAUNCH_PARAMS gLdrLaunchInfo;

typedef struct _LDR_PDB_PACKET {
	// Is this PDB packet valid?
	BOOLEAN Valid;
	// Name of the PDB file on disk
	CHAR FileName[64];
	// The base address of the executable associated with this PDB
	ULONG_PTR ImageBase;
	// The address of the buffer containing the PDB's contents
	ULONG_PTR PdbBase;
	// The size of the PDB buffer
	SIZE_T PdbSize;
} LDR_PDB_PACKET, *PLDR_PDB_PACKET;

typedef struct _LDR_SHARED_SECTION_FORMAT {
	// Parameters specific to booting the improvisor
	LDR_LAUNCH_PARAMS LdrParams;
	// The current PDB packet
	LDR_PDB_PACKET PdbPacket;
	// An event for signalling whenever one PDB has been consumed, and the next can be sent
	KEVENT PdbFinished;
	// An event which we poll to check if a PDB has been sent
	KEVENT PdbReady;
} LDR_SHARED_SECTION_FORMAT, *PLDR_SHARED_SECTION_FORMAT;

PIMAGE_SECTION_HEADER
LdrGetSectionByName(
	_In_ FNV1A Name
)
{
	PIMAGE_SECTION_HEADER Header = NULL;

	for (SIZE_T i = 0; i < gLdrLaunchInfo->SectionCount; i++)
	{
		PIMAGE_SECTION_HEADER Section = &gLdrLaunchInfo->Sections[i];
	}
}

PVOID
LdrImageDirectoryEntryToData(
	_In_ UINT32 Index,
	_Out_ PSIZE_T Size
)
{
	PIMAGE_DATA_DIRECTORY Dir = &gLdrLaunchInfo->Headers->OptionalHeader.DataDirectory[Index];

	if (Size != NULL)
		*Size = Dir->Size;

	return RVA_PTR(gLdrLaunchInfo->ImageBase, Dir->VirtualAddress);
}

VSC_API
NTSTATUS
LdrConsumePdbBinary(
	_In_ PLDR_PDB_PACKET Pdbp
)
/*++
Routine Description:
	This function maps the PDB packet's buffer into memory, and lets the PDB library parse it and store any information needed
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	PEPROCESS ClientProcess = NULL;
	// Get the EPROCESS for our client
	Status = PsLookupProcessByProcessId(gLdrLaunchInfo->ClientID, &ClientProcess);
	if (!NT_SUCCESS(Status))
	{
		ImpLog("LdrConsumePdbBinary: PsLookupProcessByProcessId failed for client process ID...\n");
		return Status;
	}

	KAPC_STATE ApcState;
	KeStackAttachProcess(ClientProcess, &ApcState);

	PMDL Mdl = IoAllocateMdl(Pdbp->PdbBase, Pdbp->PdbSize, FALSE, FALSE, NULL);
	if (Mdl == NULL)
	{
		ImpLog("LdrConsumePdbBinary: IoAllocateMdl failed for PDB buffer...\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	PVOID PdbBuffer = MmMapLockedPagesSpecifyCache(
		Mdl, 
		KernelMode, 
		MmNonCached, 
		NULL, 
		FALSE, 
		HighPagePriority
	);

	Status = PdbParseFile(FNV1A_HASH(Pdbp->FileName), Pdbp->ImageBase, PdbBuffer);
	if (!NT_SUCCESS(Status))
	{
		ImpLog("LdrConsumePdbBinary: PdbParseFile failed to parse %s...\n", Pdbp->FileName);
		return Status;
	}

	// Free the mapped pages
	MmUnmapLockedPages(PdbBuffer, Mdl);
	// Free the MDL for the PDB buffer
	IoFreeMdl(Mdl);

	// Detach from the client process
	KeUnstackDetachProcess(&ApcState);

	return Status;
}

VSC_API
NTSTATUS
LdrConsumePdbInformation(
	_In_ PLDR_SHARED_SECTION_FORMAT Section
)
/*++
Routine Description:
	This function collects all PDB information sent by the client by individually copying the contents of 
	the PDB buffer from the client, and signalling that the next one is ready to be sent
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	// Wait until a PDB is ready to be parsed
	Status = KeWaitForSingleObject(
		&Section->PdbReady,
		Suspended,
		KernelMode,
		FALSE,
		NULL
	);

	if (!NT_SUCCESS(Status))
	{
		ImpLog("Failed to wait for PDB binary... (%X)\n", Status);
		return Status;
	}

	// Continue collecting PDB information until there are no more sent.
	while (Section->PdbPacket.Valid) 
	{
		Status = LdrConsumePdbBinary(&Section->PdbPacket);

		if (!NT_SUCCESS(Status))
		{
			ImpLog("Failed to parse PDB %s... (%X)\n", Section->PdbPacket.FileName, Status);
			return Status;
		}
		
		// We have finished parsing this PDB, let the client know
		KeSetEvent(&Section->PdbFinished, IO_NO_INCREMENT, FALSE);

		Status = KeWaitForSingleObject(
			&Section->PdbReady,
			Suspended,
			KernelMode,
			FALSE,
			NULL
		);

		if (!NT_SUCCESS(Status))
		{
			ImpLog("Failed to wait for PDB binary... (%X)\n", Status);
			return Status;
		}
	}

	return Status;
}

VSC_API
NTSTATUS
LdrInitialise(
	_In_ PUNICODE_STRING SharedSectionName
)
{
	NTSTATUS Status = STATUS_SUCCESS;

	// MAP SECTION INTO MEMORY
	// FAIL IF NO SECTION IS PRESENT
	// USE THE SECTION TO COMMUNICATE WITH THE CLIENT
	// DO THE FOLLOWING:
	// 
	// When loading PDB's, there is essentially no need to actually dynamically request PDB's, we can just hardcode the client to send them
	// However, some dynamic communication could be necessary, still need to think what the hypervisor needs to do
}