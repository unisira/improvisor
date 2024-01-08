#include <vcpu/vmcall.h>
#include <pdb/pdb.h>
#include <detour.h>
#include <vmm.h>

NTSTATUS
VmmEnsureFeatureSupport(
	VOID
);

NTSTATUS
VmmPrepareCpuResources(
	_Inout_ PVMM_CONTEXT VmmContext
);

NTSTATUS
VmmPrepareSystemResources(
	_Inout_ PVMM_CONTEXT VmmContext
);

NTSTATUS
VmmSpawnVcpuDelegates(
	_In_ PVOID Func,
	_In_ PVOID Param
);

NTSTATUS
VmmPostLaunchInitialisation(
	VOID
);

VOID
VmmFreeResources(
	_In_ PVMM_CONTEXT VmmContext
);

VSC_API
NTSTATUS 
VmmStartHypervisor(
	_In_ PUNICODE_STRING SharedObjectName
)
/*++
Routine Description:
	Starts the hypervisor. On failure, this function will clean up any resources allocated up until the
	point of failure and the driver will fail to load.
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	Status = ImpReserveAllocationRecords(0x200);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Couldn't reserve host allocation records...\n");
		return Status;
	}

	Status = ImpReserveLogRecords(0x1000);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to allocate log record pool (%X)...\n", Status);
		return Status;
	}

	Status = EhInitialise();
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to initialise detours...\n");
		return Status;
	}

	Status = VmmEnsureFeatureSupport();
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("VMX, or another feature required is not supported on this system...\n");
		return Status;
	}

	PVMM_CONTEXT VmmContext = (PVMM_CONTEXT)ImpAllocateHostNpPool(sizeof(VMM_CONTEXT));
	if (VmmContext == NULL)
	{
		ImpDebugPrint("Failed to allocate VMM context...\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	VmmContext->CpuCount = (UINT8)KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

	ImpDebugPrint("Preparing VMM context at %p for %d cores...\n", VmmContext, VmmContext->CpuCount);

	Status = VmmPrepareCpuResources(VmmContext);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to allocate VCPU resources... (%x)\n", Status);
		goto panic;
	}

	Status = VmmPrepareSystemResources(VmmContext);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to allocate system resources... (%x)\n", Status);
		goto panic;
	}

	volatile VCPU_SPAWN_PARAMS Params = {
		.VmmContext = VmmContext,
		.ActiveVcpuCount = 0,
		.FailedCoreMask = 0,
		.Status = STATUS_SUCCESS
	};

	Status = VmmSpawnVcpuDelegates(VcpuSpawnPerCpu, &Params);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to spawn VCPU on cores (%x)... (%x)", Params.FailedCoreMask, Status);
		VmmShutdownHypervisor();
		goto panic;
	}

	ImpLog("Successfully launched hypervisor on %d cores...\n", Params.ActiveVcpuCount);

	Status = VmmPostLaunchInitialisation();
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("VMM post-launch initialisation failed (%X)...\n", Status);
		VmmShutdownHypervisor();
		goto panic;
	}

	return STATUS_SUCCESS;

panic:
	// Free all allocations
	ImpFreeAllAllocations();

	return Status;
}

VSC_API
NTSTATUS 
VmmPrepareCpuResources(
	_Inout_ PVMM_CONTEXT VmmContext
)
/*++
Routine Description:
	Allocates and initialises VCPU resources, such as host stacks, MSR bitmaps etc.

	This fuction will allocate the VCPU table, and initialise each VCPU separately by calling 
	VcpuCreate. This function doesn't need to clean up resources as if it doesn't return STATUS_SUCCESS,
	VmmStartHypervisor will automatically clean up any allocated resources.
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;
	UINT8 CpuCount = VmmContext->CpuCount;

	VmmContext->VcpuTable = (PVCPU)ImpAllocateHostNpPool(sizeof(VCPU) * CpuCount);
	if (VmmContext->VcpuTable == NULL)
	{
		ImpDebugPrint("Failed to allocate VCPU table...\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	for (UINT8 i = 0; i < CpuCount; i++)
	{
		Status = VcpuSetup(&VmmContext->VcpuTable[i], i);
		if (!NT_SUCCESS(Status))
		{
			ImpDebugPrint("Failed to create VCPU for core #%d... (%x)\n", i, Status);
			return Status;
		}
	}

	return Status;
}

VSC_API
NTSTATUS
VmmPrepareSystemResources(
	_Inout_ PVMM_CONTEXT VmmContext
)
/*++
Routine Description:
	Allocates and initialises VMM resources such as host paging structures, EPT paging structures, MSR load
	and store regions etc.
--*/ 
{
	// TODO: Complete this
	NTSTATUS Status = STATUS_SUCCESS;

	Status = PdbReserveEntries(64);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to reserve 64 PDB entries... (%X)\n", Status);
		return Status;
	}

	Status = MmInitialise(&VmmContext->Mm);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to initialise memory manager... (%X)\n", Status);
		return Status;
	}

	Status = EptInitialise(&VmmContext->Ept);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to initialise EPT... (%X)\n", Status);
		return Status;
	}

	Status = PdbCacheOffsets();
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to cache structure offsets... (%X)\n", Status);
		return Status;
	}
	
	VmmContext->UseUnrestrictedGuests = FALSE;

#if 0
	VmmContext->UseTscSpoofing = VmxCheckPreemptionTimerSupport();
#else
	VmmContext->UseTscSpoofing = FALSE;
#endif
	return Status;
}

VSC_API
NTSTATUS
VmmSpawnVcpuDelegates(
	_In_ PVOID Func, 
	_In_ PVOID Param
)
/*++
Routine Description:
	Spawns a delegate on each CPU core responsible for setting up/shutting down each VCPU
--*/ 
{
	PKIPI_BROADCAST_WORKER Worker   = (PKIPI_BROADCAST_WORKER)Func;
	ULONG_PTR Context               = (ULONG_PTR)Param;

	KeIpiGenericCall(Worker, Context);
	  
	return ((PVCPU_DELEGATE_PARAMS)Param)->Status;
}

VSC_API
UINT64
EhTargetFunction(VOID)
{
	return 0x12345;
}

UINT64
EhCallbackFunction(VOID)
{
	return 0xB00B5;
}

NTSTATUS
VmmPostLaunchInitialisation(VOID)
/*++
Routine Description:
	This function does all VMM initialisation after it has launched, this includes things like detours.
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	// TODO: Here we should use hypercalls to perform initialisation of anything not VMM related...
	//
	// Hide Imp* allocation records using HYPERCALL_EPT_MAP_PAGE
	// Certain sections of this driver should also be hidden, on start up a structure containing info about driver sections will be passed
	// which will be used to hide certain sections containing VMM only code from the guest OS
	//
	// Install basic detours using EhInstallDetour
	//

#if 1
	ImpDebugPrint("EhTargetFunction returned %llX...\n", EhTargetFunction());

	Status = EhRegisterDetour(FNV1A_HASH("Test"), EhTargetFunction, EhCallbackFunction);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to register test hook... (%X)\n", Status);
		return Status;
	}
	
	ImpDebugPrint("EhTargetFunction returned %llX...\n", EhTargetFunction());
#endif

	return STATUS_SUCCESS;
}

VOID
VmmShutdownHypervisor(VOID)
/*++
Routine Description:
	Shuts down the hypervisor by sending an NMI out to all active processors, and shutting them down   
	individually
 */
{
	volatile VCPU_SHUTDOWN_PARAMS Params = {
		.Status = STATUS_SUCCESS,
		.FailedCoreMask = 0,
		.VmmContext = NULL
	};

	VmmSpawnVcpuDelegates(VcpuShutdownPerCpu, &Params);
	if (!NT_SUCCESS(Params.Status) || Params.VmmContext == NULL)
	{
		ImpDebugPrint("Failed to shutdown VCPU on cores (%x)... (%x)\n", Params.FailedCoreMask, Params.Status);
		// TODO: Panic
		return;
	}

	// VmmFreeResources(Params.VmmContext);
}

VOID
VmmFreeResources(
	_In_ PVMM_CONTEXT VmmContext
)
/**
Routine Description:
	Frees any resources found in the VMM context that may have been allocated before this function
	was called.
--*/
{
	// TODO: Complete this
	return;
}

VSC_API
NTSTATUS
VmmEnsureFeatureSupport(VOID)
/*++
Routine Description:
	Checks if all the features required for this hypervisor are present and useable on the current hardware
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	// TODO: Check EPT feature support here in the future, maybe MTRR support too?
	if (!VmxCheckSupport())
		return STATUS_NOT_SUPPORTED;

	if (!EptCheckSupport())
		return STATUS_NOT_SUPPORTED;

	return Status;
}

