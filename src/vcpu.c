#include "arch/msr.h"
#include "vcpu.h"
#include "cpuid.h"
#include "vmm.h"

EXTERN_C
VOID
__vmexit_entry(VOID);

VMEXIT_HANDLER VcpuUnknownExitReason;
VMEXIT_HANDLER VcpuHandleCpuid;

static const VMEXIT_HANDLER* ExitHandlers[] = {
    VcpuUnknownExitReason
};

PVCPU_STACK
VcpuGetStack(VOID);

VOID
VcpuToggleExitOnMsr(
    _Inout_ PVCPU Vcpu,
    _In_ UINT32 Msr,
    _In_ MSR_ACCESS Access
);

VOID
VcpuHandleExit(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
);

NTSTATUS
VcpuSetup(
    _Inout_ PVCPU Vcpu,
    _In_ UINT8 Id
)
/*++
Routine Description:
    Initialises a VCPU structure. This function doesn't need to free any resources upon failure
    because VmmStartHypervisor will take care of it
--*/
{
    Vcpu->Vmcs = VmxAllocateRegion();
    if (Vcpu->Vmcs == NULL)
    {
        ImpDebugPrint("Failed to allocate VMCS region for VCPU #%d...\n", Id);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    Vcpu->VmcsPhysical = ImpGetPhysicalAddress(Vcpu->Vmcs);

    Vcpu->Vmxon = VmxAllocateRegion();
    if (Vcpu->Vmxon == NULL)
    {
        ImpDebugPrint("Failed to allocate VMCS region for VCPU #%d...\n", Id);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Vcpu->VmxonPhysical = ImpGetPhysicalAddress(Vcpu->Vmxon);

    Vcpu->MsrBitmap = (PCHAR)ImpAllocateContiguousMemory(PAGE_SIZE);
    if (Vcpu->MsrBitmap == NULL)
    {
        ImpDebugPrint("Failed to allocate MSR bitmap for VCPU #%d...\n", Id);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Vcpu->Stack = (PVCPU_STACK)ExAllocatePoolWithTag(NonPagedPool, sizeof(VCPU_STACK), POOL_TAG);
    if (Vcpu->Stack == NULL)
    {
        ImpDebugPrint("Failed to allocate a host stack for VCPU #%d...\n", Id);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Vcpu->Stack->Cache.Vcpu = Vcpu;

    RtlInitializeBitMap(
            &Vcpu->MsrLoReadBitmap, 
            Vcpu->MsrBitmap + VMX_MSR_READ_BITMAP_OFFS + VMX_MSR_LO_BITMAP_OFFS, 
            1024 * 8);

    RtlInitializeBitMap(
            &Vcpu->MsrHiReadBitmap, 
            Vcpu->MsrBitmap + VMX_MSR_READ_BITMAP_OFFS + VMX_MSR_HI_BITMAP_OFFS, 
            1024 * 8);
    
    RtlInitializeBitMap(
            &Vcpu->MsrLoWriteBitmap, 
            Vcpu->MsrBitmap + VMX_MSR_WRITE_BITMAP_OFFS + VMX_MSR_LO_BITMAP_OFFS, 
            1024 * 8);

    RtlInitializeBitMap(
            &Vcpu->MsrHiWriteBitmap, 
            Vcpu->MsrBitmap + VMX_MSR_WRITE_BITMAP_OFFS + VMX_MSR_HI_BITMAP_OFFS, 
            1024 * 8);

    VcpuToggleExitOnMsr(Vcpu, IA32_FEATURE_CONTROL, MSR_READ);

    return STATUS_SUCCESS;
}

DECLSPEC_NORETURN
VOID
VcpuLaunch(VOID)
/*++
Routine Description:
    Post-VMLAUNCH, sets the CPU's registers and RIP to previous execution.
--*/
{
    CpuRestoreState(&VcpuGetStack()->Cache.Vcpu->LaunchState);
}

NTSTATUS
VcpuSpawn(
    _In_ PVCPU Vcpu
)
/*++
Routine Description:
    Spawns a VCPU on this core 
--*/
{
    NTSTATUS Status = STATUS_SUCCESS;

    ImpDebugPrint("Spawning VCPU #%d...", Vcpu->Id);

    VmxRestrictControlRegisters();

    if (!VmxEnableVmxon())
    {
        ImpDebugPrint("Unable to enable VMX, it appears to be disabled in the BIOS...\n");
        return STATUS_NOT_SUPPORTED;
    }

    if (__vmx_on(&Vcpu->VmxonPhysical))
    {
        ImpDebugPrint("Failed to enter VMX operation on VCPU #%d...\n", Vcpu->Id);
        return STATUS_INVALID_PARAMETER;
    }

    if (__vmx_vmclear(&Vcpu->VmcsPhysical))
    {
        ImpDebugPrint("Failed to clear VMCS for VCPU #%d...\n", Vcpu->Id);
        return STATUS_INVALID_PARAMETER;
    }

    if (__vmx_vmptrld(&Vcpu->VmcsPhysical))
    {
        ImpDebugPrint("Failed to set active VMCS on VCPU #%d...\n", Vcpu->Id);
        return STATUS_INVALID_PARAMETER;
    }

    VmxWrite(HOST_RSP, &Vcpu->Stack->Limit + KERNEL_STACK_SIZE);
    VmxWrite(GUEST_RSP, &Vcpu->Stack->Limit + KERNEL_STACK_SIZE);

    VmxWrite(HOST_RIP, (UINT64)__vmexit_entry);
    VmxWrite(GUEST_RIP, (UINT64)VcpuLaunch);

    Vcpu->IsLaunched = TRUE;

    __vmx_vmlaunch();

    return STATUS_FATAL_APP_EXIT;
}

VOID
VcpuSpawnPerCpu(
    _Inout_ PVCPU_DELEGATE_PARAMS Params
)
/*++
Routine Description:
    Spawns a VCPU on this core by enabling VMX, entering VMX root operation, setting up
    this VCPUs VMCS and entering non root operation from a previously saved snapshot. 
    
    VCPU_DELEGATE_PARAMS::Status only gets updated when VcpuSpawn fails, if it does not
    control will be returned to the moment after the CpuSaveState call, and it will return
    with VCPU_DELEGATE_PARAMS::Status as STATUS_SUCCESS, indicating everything went well
--*/
{
    ULONG CpuId = KeGetCurrentProcessorNumber();
    PVCPU Vcpu = &Params->VmmContext->VcpuTable[CpuId];
    
    Vcpu->Vmm = Params->VmmContext;

    CpuSaveState(&Vcpu->LaunchState);

    // Control flow is restored here upon successful virtualisation of the CPU
    if (Vcpu->IsLaunched)
    {
        InterlockedIncrement(&Params->ActiveVcpuCount);
        ImpDebugPrint("VCPU #%d is now running...\n", Vcpu->Id);
        return;
    }
    
    Params->Status = VcpuSpawn(Vcpu);

    if (!NT_SUCCESS(Params->Status))
    {
        Params->FaultyCoreId = CpuId;

        // Shutdown VMX operation on this CPU core if we failed VM launch
        if (Params->Status == STATUS_FATAL_APP_EXIT)
        {
            ImpDebugPrint("VMLAUNCH failed on VCPU #%d... (%x)", Vcpu->Id, VmxRead(VM_INSTRUCTION_ERROR));
            __vmx_off();
        }
    }
}

VOID
VcpuShutdownPerCpu(
    _Inout_ PVCPU_DELEGATE_PARAMS Params
)
/*++
Routine Description:
    Checks if this core has been virtualised, and stops VMX operation if so.
--*/
{
    // TODO: Complete this
    ULONG CpuId = KeGetCurrentProcessorNumber(); 

	/*
    if (Params->VmmContext->VcpuTable[CpuId].IsLaunched)
        __vmcall(HYPERCALL_SHUTDOWN_VCPU);
    */
}

PVCPU_STACK
VcpuGetStack(VOID)
/*++
Routine Description:
	Gets the VCPU_STACK pointer from the top of the host stack
 */
{
    return (PVCPU_STACK)((UINT64)_AddressOfReturnAddress() - KERNEL_STACK_SIZE);
}

VOID
VcpuToggleExitOnMsr(
    _Inout_ PVCPU Vcpu,
    _In_ UINT32 Msr,
    _In_ MSR_ACCESS Access
)
/*++
Routine Description:
    Toggles VM-exits for a specific MSR via a VCPU's MSR bitmap
--*/
{
    PRTL_BITMAP MsrBitmap = NULL;

    switch (Access)
    {
    case MSR_READ:
        {
            if (Msr >= 0xC0000000)
                MsrBitmap = &Vcpu->MsrHiReadBitmap;
            else
                MsrBitmap = &Vcpu->MsrLoReadBitmap;
        } break;

    case MSR_WRITE:
        {
            if (Msr >= 0xC0000000)
                MsrBitmap = &Vcpu->MsrHiWriteBitmap;
            else
                MsrBitmap = &Vcpu->MsrLoWriteBitmap;
        } break;
    }
	
    RtlSetBit(MsrBitmap, Msr);
}

DECLSPEC_NORETURN
VMM_EVENT_STATUS
VcpuUnknownExitReason(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
/*++
Routine Description:
    Handles any unknown/unhandled VM-exit reasons
--*/
{
    // TODO: Shutdown entire hypervisor from here
    ImpDebugPrint("Unknown VM-exit reason on VCPU #%d...\n", Vcpu->Id);
	KeBugCheckEx(HYPERVISOR_ERROR, BUGCHECK_UNKNOWN_VMEXIT_REASON, 0, 0, 0);
}

DECLSPEC_NORETURN
VOID
VcpuResume(VOID)
/*++
Routine Description:
    Executes VMRESUME to resume VMX guest operation.
--*/
{
    __vmx_vmresume();

    ImpDebugPrint("VMRESUME failed on VCPU #%d... (%x)\n", VmxRead(VM_INSTRUCTION_ERROR));

    // TODO: Shutdown entire hypervisor from here
    __vmx_off();
    __debugbreak();
}

VOID
VcpuHandleExit(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
/*++
Routine Description:
    Loads the VCPU with new data post VM-exit and calls the correct VM-exit handler 
--*/
{
    Vcpu->Vmx.GuestRip = VmxRead(GUEST_RIP); 
    Vcpu->Vmx.ExitReason.Value = (UINT32)VmxRead(VM_EXIT_REASON);

    VMM_EVENT_STATUS Status = ExitHandlers[Vcpu->Vmx.ExitReason.BasicExitReason](Vcpu, GuestState); 

    GuestState->Rip = (UINT64)VcpuResume;

    if (Status == VMM_EVENT_CONTINUE)
        VmxAdvanceGuestRip();
}

VMM_EVENT_STATUS
VcpuHandleCpuid(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
/*++
Routine Description:
    Emulates the CPUID instruction.
--*/
{
    X86_CPUID_ARGS CpuidArgs = (X86_CPUID_ARGS){
        .Eax = (UINT32)GuestState->Rax,
        .Ebx = (UINT32)GuestState->Rbx,
        .Ecx = (UINT32)GuestState->Rcx,
        .Edx = (UINT32)GuestState->Rdx,
    };

    CpuidEmulateGuestCall(&CpuidArgs);

    GuestState->Rax = CpuidArgs.Eax;
    GuestState->Rbx = CpuidArgs.Ebx;
    GuestState->Rcx = CpuidArgs.Ecx;
    GuestState->Rdx = CpuidArgs.Edx;

    VmxToggleControl(&Vcpu->Vmx, VMX_CTL_VMX_PREEMPTION_TIMER);
    VmxToggleControl(&Vcpu->Vmx, VMX_CTL_RDTSC_EXITING);

    // Set the VMX preemption timer to a relatively low value taking the VM entry latency into account
    VmxWrite(GUEST_VMX_PREEMPTION_TIMER_VALUE, Vcpu->TscInfo.VmEntryLatency + 500);

    return VMM_EVENT_CONTINUE;
}
