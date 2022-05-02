#include "vmm.h"

EXTERN_C_START
#define VMM_INTR_GATE(Index)    \
	VOID                        \
	__vmm_intr_gate_##Index()   \

VMM_INTR_GATE(2);

#undef VMM_INTR_GATE
EXTERN_C_END

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

static const X86_SEGMENT_SELECTOR sVmmCsSelector = {
	.Table = SEGMENT_SELECTOR_TABLE_GDT,
	.Index = 1,
	.Rpl = 0
};

static const X86_SEGMENT_SELECTOR sVmmTssSelector = {
	.Table = SEGMENT_SELECTOR_TABLE_GDT,
	.Index = 2,
	.Rpl = 0
};

VSC_API
NTSTATUS 
VmmStartHypervisor(VOID)
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
		ImpDebugPrint("Couldn't reserve log records...\n");
		return Status;
	}

	// TODO: Debug why VMX isn't showing up as supported
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

	const volatile VCPU_SPAWN_PARAMS Params = {
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

	ImpDebugPrint("Successfully launched hypervisor on %d cores...\n", Params.ActiveVcpuCount);

	Status = VmmPostLaunchInitialisation();
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to spawn VCPU on cores (%x)... (%x)", Params.FailedCoreMask, Status);
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
VmmPrepareHostGDT(
	_Inout_ PVMM_CONTEXT VmmContext
)
{
	NTSTATUS Status = STATUS_SUCCESS;

	PX86_SEGMENT_DESCRIPTOR Descriptors = ImpAllocateHostNpPool(sizeof(X86_SEGMENT_DESCRIPTOR) * 2 + sizeof(X86_SYSTEM_DESCRIPTOR));
	if (Descriptors == NULL)
	{
		ImpDebugPrint("Failed to allocate GDT descriptors...\n");
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto panic;
	}

	PX86_SEGMENT_DESCRIPTOR Cs = &Descriptors[sVmmCsSelector.Index];

	Cs->Type = CODE_DATA_TYPE_EXECUTE_READ;
	Cs->System = FALSE;
	Cs->Dpl = 0;
	Cs->Present = TRUE;
	Cs->Long = TRUE;
	Cs->DefaultOperationSize = 0;
	Cs->Granularity = 0;

	PX86_SYSTEM_DESCRIPTOR Tss = &Descriptors[sVmmTssSelector.Index];

	Tss->Type = SEGMENT_TYPE_WORD_TSS_BUSY_32;
	Tss->System = TRUE;
	Tss->Dpl = 0;
	Tss->Present = TRUE;
	Tss->Long = TRUE;
	Tss->DefaultOperationSize = 0;
	Tss->Granularity = 0;
	Tss->LimitLow = sizeof(X86_TASK_STATE_SEGMENT) & 0xFFFF;
	Tss->LimitHigh = (sizeof(X86_TASK_STATE_SEGMENT) >> 16) & 0xF;

	VmmContext->Tss = ImpAllocateHostNpPool(sizeof(X86_TASK_STATE_SEGMENT));
	if (VmmContext->Tss == NULL)
	{
		ImpDebugPrint("Failed to allocate TSS segment...\n");
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto panic;
	}

	const UINT64 Address = (UINT64)VmmContext->Tss;

	Tss->BaseLow = Address & 0xFFFF;
	Tss->BaseMiddle = (Address >> 16) & 0xFF;
	Tss->BaseHigh = (Address >> 24) & 0xFF;
	Tss->BaseUpper = (Address >> 32) & 0xFFFFFFFF;

	VmmContext->Gdt = Descriptors;

panic:
	if (!NT_SUCCESS(Status))
	{
		if (VmmContext->Tss != NULL)
			ExFreePoolWithTag(VmmContext->Tss, POOL_TAG);
		if (VmmContext->Gdt != NULL)
			ExFreePoolWithTag(VmmContext->Gdt, POOL_TAG);
	}

	return Status;
}

VSC_API
VOID
VmmCreateInterruptGate(
	PX86_INTERRUPT_TRAP_GATE Gates,
	UINT8 Vector,
	PVOID Handler
)
{
	PX86_INTERRUPT_TRAP_GATE Gate = &Gates[Vector];

	Gate->SegmentSelector = sVmmCsSelector.Index;                
	Gate->InterruptStackTable = FALSE;                                
	Gate->Type = SEGMENT_TYPE_NATURAL_INTERRUPT_GATE;  
	Gate->Dpl = 0;                                   
	Gate->Present = TRUE;                                 

	const UINT64 Address = (UINT64)Handler;
	Gate->OffsetLow = Address & 0xFFFF;
	Gate->OffsetMid = (Address >> 16) & 0xFFFF;
	Gate->OffsetHigh = (Address >> 32) & 0xFFFFFFFF;
}

VSC_API
NTSTATUS
VmmPrepareHostIDT(
	_Inout_ PVMM_CONTEXT VmmContext
)
/*++
Routine Description:
	Creates a host IDT pointing to VcpuHandleHostException
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	PX86_INTERRUPT_TRAP_GATE InterruptGates = ImpAllocateContiguousMemory(sizeof(X86_INTERRUPT_TRAP_GATE) * 32);
	if (InterruptGates == NULL)
	{
		ImpDebugPrint("Failed to allocate 32 interrupt gates for host IDT...\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	VmmCreateInterruptGate(InterruptGates, 2, __vmm_intr_gate_2);

	// TODO: Enable interrupts during root mode and write a small debugger to walk the stack after an interrupt occurs

	VmmContext->Idt = InterruptGates;

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

	Status = VmmPrepareHostGDT(VmmContext);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to create host GDT... (%X)\n", Status);
		return Status;
	}

	Status = VmmPrepareHostIDT(VmmContext);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to create host GDT... (%X)\n", Status);
		return Status;
	}

	Status = MmInitialise(&VmmContext->MmInformation);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to initialise memory manager... (%X)\n", Status);
		return Status;
	}

	Status = EptInitialise(&VmmContext->EptInformation);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to initialise EPT... (%X)\n", Status);
		return Status;
	}

	VmmContext->UseUnrestrictedGuests = FALSE;
	VmmContext->UseTscSpoofing = VmxCheckPreemptionTimerSupport();

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
NTSTATUS
VmmPostLaunchInitialisation(VOID)
/*++
Routine Description:
	This function does all VMM initialisation after it has launched, this includes things like detours.
--*/
{
	// TODO: Here we should use hypercalls to perform initialisation of anything not VMM related...
	//
	// Hide Imp* allocation records using HYPERCALL_EPT_MAP_PAGE
	// Certain sections of this driver should also be hidden, on start up a structure containing info about driver sections will be passed
	// which will be used to hide certain sections containing VMM only code from the guest OS
	//
	// Install basic detours using EhInstallDetour
	//

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
	const volatile VCPU_SHUTDOWN_PARAMS Params = {
		.Status = STATUS_SUCCESS,
		.FailedCoreMask = 0,
		.VmmContext = NULL
	};

	VmmSpawnVcpuDelegates(VcpuShutdownPerCpu, &Params);
	if (!NT_SUCCESS(Params.Status) || Params.VmmContext == NULL)
	{
		ImpDebugPrint("Failed to shutdown VCPU on cores (%x)... (%x)", Params.FailedCoreMask, Params.Status);
		// TODO: Panic
		return;
	}

	VmmFreeResources(Params.VmmContext);
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

