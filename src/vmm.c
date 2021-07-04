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
    _In_ PVCPU_DELEGATE_PARAMS Param
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

    Status = VmmEnsureFeatureSupport();
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("VMX, or another feature required is not supported on this system...\n");
        return Status;
    }

    PVMM_CONTEXT VmmContext = (PVMM_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(VMM_CONTEXT), POOL_TAG);
    if (VmmContext == NULL)
    {
        ImpDebugPrint("Failed to allocate VMM context...\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    VmmContext->CpuCount = (UINT8)KeQueryActiveProcessorCount(ALL_PROCESSOR_GROUPS);

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

    VCPU_DELEGATE_PARAMS Params = (VCPU_DELEGATE_PARAMS){
        .VmmContext = VmmContext,
        .ActiveVcpuCount = 0,
        .FaultyCoreId = -1,
        .Status = STATUS_SUCCESS
    };

    Status = VmmSpawnVcpuDelegates(VcpuSpawnPerCpu, &Params);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to spawn VCPU on core #%d... (%x)\n", Params.FaultyCoreId, Status);
        
        Params.Status = STATUS_SUCCESS;
        Status = VmmSpawnVcpuDelegates(VcpuShutdownPerCpu, &Params);
        if (!NT_SUCCESS(Status))
        {
            ImpDebugPrint("Failed to shutdown active VCPUs, core #%d encountered an error... (%x)\n", Params.FaultyCoreId, Status);
            KeBugCheckEx(HYPERVISOR_ERROR, BUGCHECK_FAILED_SHUTDOWN, Params.FaultyCoreId, Status, 0);
        }

        goto panic;
    }

    ImpDebugPrint("Successfully launched hypervisor on %d cores...\n", Params.ActiveVcpuCount);

    return STATUS_SUCCESS;

panic:
    ExFreePoolWithTag(VmmContext, POOL_TAG); 
    VmmFreeResources(VmmContext);
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

    VmmContext->VcpuTable = (PVCPU)ExAllocatePoolWithTag(NonPagedPool, sizeof(VCPU) * CpuCount, POOL_TAG);
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

    return Status;
}

NTSTATUS
VmmSpawnVcpuDelegates(
    _In_ PVOID Func, 
    _In_ PVCPU_DELEGATE_PARAMS Param
)
/*++
Routine Description:
    Spawns a delegate on each CPU core responsible for setting up/shutting down each VCPU
--*/ 
{
    PKIPI_BROADCAST_WORKER Worker   = (PKIPI_BROADCAST_WORKER)&Func;
    ULONG_PTR Context               = (ULONG_PTR)Param;

    KeIpiGenericCall(Worker, Context);
      
    return Param->Status;
}

VOID
VmmShutdownHypervisor(
    VOID
)
/*++
Routine Description:
	Shuts down the hypervisor by sending an IPI out to all active processors, and shutting them down individually
 */
{
	// TODO: Complete this
    return;
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

