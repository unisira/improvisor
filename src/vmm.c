#include "vmm.h"

NTSTATUS
VmmEnsureFeatureSupport(VOID);

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
    _In_ PVMM_CONTEXT VmmContext
);

VOID
VmmFreeResources(
    _In_ PVMM_CONTEXT VmmContext
);

NTSTATUS 
VmmStartHypervisor(VOID)
/*++
Routine Description:
    Starts the hypervisor. On failure, this function will clean up any resources allocated up until the
    point of failure and the driver will fail to load.
--*/
{
    NTSTATUS Status = STATUS_SUCCESS;

    Status = ImpReserveAllocationRecords(0x2000);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Couldn't reserve host allocation records...\n");
        return Status;
    }

    /* TODO: Debug why VMX isn't showing up as supported
    Status = VmmEnsureFeatureSupport();
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("VMX, or another feature required is not supported on this system...\n");
        return Status;
    }
    */

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

    VCPU_SPAWN_PARAMS Params = {
        .VmmContext = VmmContext,
        .ActiveVcpuCount = 0,
        .FailedCoreMask = 0,
        .Status = STATUS_SUCCESS
    };

    Status = VmmSpawnVcpuDelegates(VcpuSpawnPerCpu, &Params);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to spawn VCPU on cores (%x)... (%x)", Params.FailedCoreMask, Status);
        VmmShutdownHypervisor(VmmContext);
        goto panic;
    }

    ImpDebugPrint("Successfully launched hypervisor on %d cores...\n", Params.ActiveVcpuCount);

    // TODO: Here we should use hypercalls to perform initialisation of anything not VMM related...
    //
    // Hide Imp* allocation records using HYPERCALL_EPT_MAP_PAGE
    // Certain sections of this driver should also be hidden, on start up a structure containing info about driver sections will be passed
    // which will be used to hide certain sections containing VMM only code from the guest OS
    //
    // Install basic detours using EhInstallDetour
    //
    //

    return STATUS_SUCCESS;

panic:
    VmmFreeResources(VmmContext);
    ExFreePoolWithTag(VmmContext, POOL_TAG); 
    return Status;
}

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

    /*
    TODO: wrap this into nice func
    X86_PSEUDO_DESCRIPTOR Idtr;
    __sidt(&Idtr);

    PVOID HostIdtBaseAddr = ImpAllocateContiguousMemory(Idtr.Limit);
    if (HostIdtBaseAddr == NULL)
    {
        ImpDebugPrint("Failed to allocate host IDT...\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Idtr.BaseAddress, HostIdtBaseAddr, Idtr.Limit);

    VmmContext->HostInterruptDescriptor.BaseAddress = HostIdtBaseAddr;
    VmmContext->HostInterruptDescriptor.Limit = Idtr.Limit;

    // TODO: Write a generic interrupt handler in vmm_intr.asm and make it look up array of host interrupt
    // handlers and jmp to it
    VmmSetHostInterruptHandler(EXCEPTION_NMI, VmmHandleHostNMI);
    */

    /*
    Status = MmInitialise(&VmmContext->MmSupport);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to initialise memory manager... (%X)\n", Status);
        return Status;
    }
    */
    VmmContext->UseUnrestrictedGuests = FALSE;
    VmmContext->UseTscSpoofing = FALSE;

    return Status;
}

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

NTSTATUS
VmmPostLaunchInitialisation(
    _In_ PVMM_CONTEXT VmmContext
)
/*++
Routine Description:
    This function does all VMM initialisation after it has launched, this includes things like detours.
--*/
{
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
    PKIPI_BROADCAST_WORKER Worker = (PKIPI_BROADCAST_WORKER)VcpuShutdownPerCpu;

    VCPU_SHUTDOWN_PARAMS Params = {
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
   
    return Status;
}

