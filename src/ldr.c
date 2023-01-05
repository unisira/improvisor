#include <improvisor.h>
#include <arch/memory.h>
#include <pdb/pdb.h>
#include <ldr.h>
#include <win.h>

VMM_DATA LDR_LAUNCH_PARAMS gLdrLaunchParams;

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
	// A handle to an event for signalling whenever one PDB has been consumed, and the next can be sent
	UNICODE_STRING PdbFinished;
	// The name for an event which we poll to check if a PDB has been sent
	UNICODE_STRING PdbReady;
} LDR_SHARED_SECTION_FORMAT, *PLDR_SHARED_SECTION_FORMAT;

PIMAGE_SECTION_HEADER
LdrGetSectionByName(
	_In_ FNV1A Name
)
{
	PIMAGE_SECTION_HEADER Header = NULL;

	for (SIZE_T i = 0; i < gLdrLaunchParams.SectionCount; i++)
	{
		PIMAGE_SECTION_HEADER Section = &gLdrLaunchParams.Sections[i];
	}
}

PVOID
LdrImageDirectoryEntryToData(
	_In_ UINT32 Index,
	_Out_ PSIZE_T Size
)
{
	PIMAGE_DATA_DIRECTORY Dir = &gLdrLaunchParams.Headers->OptionalHeader.DataDirectory[Index];

	if (Size != NULL)
		*Size = Dir->Size;

	return RVA_PTR(gLdrLaunchParams.ImageBase, Dir->VirtualAddress);
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
	Status = PsLookupProcessByProcessId(gLdrLaunchParams.ClientID, &ClientProcess);
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

	if (PdbBuffer == NULL)
	{
		ImpLog("LdrConsumePdbBinary: Failed to map and lock pages (%llX -> %llX) for %s...\n", Pdbp->PdbBase, Pdbp->PdbBase + Pdbp->PdbSize, Pdbp->FileName);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

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
LdrOpenPdbEvents(
	_In_ PLDR_SHARED_SECTION_FORMAT Section,
	_Out_ PRKEVENT* PdbReady,
	_Out_ PRKEVENT* PdbFinished
)
/*++
Routine Description:
	Gets a pointer to the two routine events used for parsing PDB's by name
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	// Open the PdbReady and PdbFinished events
	Status = ObReferenceObjectByName(
		&Section->PdbReady,
		OBJ_CASE_INSENSITIVE,
		NULL,
		EVENT_ALL_ACCESS,
		ExEventObjectType,
		KernelMode,
		NULL,
		PdbReady
	);

	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to reference the PdbReady event '%wZ'\n", Section->PdbReady);
		return Status;
	}

	// Open the PdbReady and PdbFinished events
	Status = ObReferenceObjectByName(
		&Section->PdbFinished,
		OBJ_CASE_INSENSITIVE,
		NULL,
		EVENT_ALL_ACCESS,
		ExEventObjectType,
		KernelMode,
		NULL,
		PdbFinished
	);

	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to reference the PdbFinished event '%wZ'\n", Section->PdbFinished);
		return Status;
	}

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

	PRKEVENT PdbReady = NULL, PdbFinished = NULL;
	// Open the PdbReady and PdbFinished events
	Status = LdrOpenPdbEvents(Section, &PdbReady, &PdbFinished);
	if (!NT_SUCCESS(Status))
		return Status;

	// Wait until a PDB is ready to be parsed
	Status = KeWaitForSingleObject(
		PdbReady,
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
		KeSetEvent(PdbFinished, IO_NO_INCREMENT, FALSE);

		Status = KeWaitForSingleObject(
			PdbReady,
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

	OBJECT_ATTRIBUTES ObjectAttributes;

	// Setup opject attributes for the shared section
	InitializeObjectAttributes(
		&ObjectAttributes,
		&SharedSectionName,
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
		NULL,
		NULL
	);

	HANDLE SharedSectionHandle = NULL;
	// Open the shared section with read write access
	Status = ZwOpenSection(&SharedSectionHandle, SECTION_MAP_READ | SECTION_MAP_WRITE, &ObjectAttributes);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to open the shared section '%wZ'\n", SharedSectionName);
		return Status;
	}

	// The shared section address
	PLDR_SHARED_SECTION_FORMAT SharedSection = NULL;

	SIZE_T ViewSize = sizeof(LDR_SHARED_SECTION_FORMAT);
	// Map the section into the current process
	Status = ZwMapViewOfSection(
		SharedSectionHandle,
		NtCurrentProcess(),
		&SharedSection,
		0,
		0,
		NULL,
		&ViewSize,
		ViewUnmap,
		0,
		PAGE_EXECUTE_READWRITE
	);

	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to map the shared section '%wZ'\n", SharedSectionName);
		return Status;
	}

	// Store launch parameters for hypervisor initialisation
	gLdrLaunchParams = SharedSection->LdrParams;

	// Store any PDB's the client tells us to
	Status = LdrConsumePdbInformation(SharedSection);
	if (!NT_SUCCESS(Status))
		return Status;

	// Unmap the shared section
	Status = ZwUnmapViewOfSection(ZwCurrentProcess(), SharedSection);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to unmap the shared section '%wZ'\n", SharedSectionName);
		return Status;
	}
		
	// Section mapped, close the handle to this section
	Status = ZwClose(SharedSection);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to close the handle for the shared section '%wZ'\n", SharedSectionName);
		return Status;
	}
}