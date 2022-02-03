#include "arch/interrupt.h"
#include "arch/segment.h"
#include "arch/cpuid.h"
#include "arch/msr.h"
#include "arch/cr.h"
#include "intrin.h"
#include "vmcall.h"
#include "vcpu.h"
#include "vmm.h"

// The value of the VMX preemption timer used as a watchdog for TSC virtualisation
#define VTSC_WATCHDOG_QUANTUM 4000

EXTERN_C
VOID
__vmexit_entry(VOID);

VMEXIT_HANDLER VcpuUnknownExitReason;
VMEXIT_HANDLER VcpuHandleCpuid;
VMEXIT_HANDLER VcpuHandleInvalidGuestState;
VMEXIT_HANDLER VcpuHandleCrAccess;
VMEXIT_HANDLER VcpuHandleVmxInstruction;
VMEXIT_HANDLER VcpuHandleHypercall;
VMEXIT_HANDLER VcpuHandleRdtsc;
VMEXIT_HANDLER VcpuHandleRdtscp;
VMEXIT_HANDLER VcpuHandleTimerExpire;
VMEXIT_HANDLER VcpuHandleMsrRead;
VMEXIT_HANDLER VcpuHandleMsrWrite;
VMEXIT_HANDLER VcpuHandleXSETBV;
VMEXIT_HANDLER VcpuHandlePreemptionTimerExpire;
VMEXIT_HANDLER VcpuHandleExternalInterruptNmi;
VMEXIT_HANDLER VcpuHandleNmiWindow;
VMEXIT_HANDLER VcpuHandleInterruptWindow;
VMEXIT_HANDLER VcpuHandleMTFExit;
VMEXIT_HANDLER VcpuHandleWbinvd;
VMEXIT_HANDLER VcpuHandleXsetbv;
VMEXIT_HANDLER VcpuHandleInvlpg;

static VMEXIT_HANDLER* sExitHandlers[] = {
    VcpuHandleExternalInterruptNmi, // Exception or non-maskable interrupt (NMI)
    VcpuUnknownExitReason, 			// External interrupt
    VcpuUnknownExitReason, 			// Triple fault
    VcpuUnknownExitReason, 			// INIT signal
    VcpuUnknownExitReason, 			// Start-up IPI (SIPI)
    VcpuUnknownExitReason, 			// I/O system-management interrupt (SMI)
    VcpuUnknownExitReason, 			// Other SMI
    VcpuUnknownExitReason, 			// Interrupt window
    VcpuUnknownExitReason, 			// NMI window
    VcpuUnknownExitReason, 			// Task switch
    VcpuHandleCpuid,				// CPUID
    VcpuHandleVmxInstruction, 		// GETSEC
    VcpuUnknownExitReason, 			// HLT
    VcpuUnknownExitReason, 			// INVD
    VcpuUnknownExitReason, 			// INVLPG
    VcpuUnknownExitReason, 			// RDPMC
    VcpuHandleRdtsc, 			    // RDTSC
    VcpuUnknownExitReason, 			// RSM
    VcpuHandleHypercall, 			// VMCALL
    VcpuHandleVmxInstruction, 		// VMCLEAR
    VcpuHandleVmxInstruction, 		// VMLAUNCH
    VcpuHandleVmxInstruction, 		// VMPTRLD
    VcpuHandleVmxInstruction, 		// VMPTRST
    VcpuHandleVmxInstruction, 		// VMREAD
    VcpuHandleVmxInstruction, 		// VMRESUME
    VcpuHandleVmxInstruction, 		// VMWRITE
    VcpuHandleVmxInstruction, 		// VMXOFF
    VcpuHandleVmxInstruction, 		// VMXON
    VcpuHandleCrAccess, 			// Control-register accesses
    VcpuUnknownExitReason, 			// MOV DR
    VcpuUnknownExitReason,			// I/O instruction
    VcpuHandleMsrRead,     			// RDMSR
    VcpuHandleMsrWrite, 			// WRMSR
    VcpuHandleInvalidGuestState,	// VM-entry failure due to invalid guest state
    VcpuUnknownExitReason, 			// VM-entry failure due to MSR loading
    NULL,
    VcpuUnknownExitReason, 			// MWAIT
    VcpuUnknownExitReason, 			// Monitor trap flag
    NULL,
    VcpuUnknownExitReason, 			// MONITOR
    VcpuUnknownExitReason, 			// PAUSE
    VcpuUnknownExitReason, 			// VM-entry failure due to machine-check event
    NULL,
    VcpuUnknownExitReason, 			// TPR below threshold
    VcpuUnknownExitReason, 			// APIC access
    VcpuUnknownExitReason, 			// Virtualized EOI
    VcpuUnknownExitReason, 			// Access to GDTR or IDTR
    VcpuUnknownExitReason, 			// Access to LDTR or TR
    VcpuUnknownExitReason, 			// EPT violation
    VcpuUnknownExitReason, 			// EPT misconfiguration
    VcpuHandleVmxInstruction, 		// INVEPT
    VcpuHandleRdtscp,     			// RDTSCP
    VcpuHandleTimerExpire,          // VMX-preemption timer expired
    VcpuHandleVmxInstruction, 		// INVVPID
    VcpuUnknownExitReason, 			// WBINVD or WBNOINVD
    VcpuUnknownExitReason, 			// XSETBV
    VcpuUnknownExitReason, 			// APIC write
    VcpuUnknownExitReason, 			// RDRAND
    VcpuUnknownExitReason, 			// INVPCID
    VcpuHandleVmxInstruction, 		// VMFUNC
    VcpuUnknownExitReason, 			// ENCLS
    VcpuUnknownExitReason, 			// RDSEED
    VcpuUnknownExitReason, 			// Page-modification log full
    VcpuUnknownExitReason, 			// XSAVES
    VcpuUnknownExitReason, 			// XRSTORS
    NULL,
    VcpuUnknownExitReason, 			// SPP-related event
    VcpuUnknownExitReason, 			// UMWAIT
    VcpuUnknownExitReason, 			// TPAUSE
    VcpuUnknownExitReason, 			// LOADIWKEY
};

#define CREATE_MSR_ENTRY(Msr) \
    {Msr, 0UL, 0ULL}

static DECLSPEC_ALIGN(16) VMX_MSR_ENTRY sVmExitMsrStore[] = {
    CREATE_MSR_ENTRY(IA32_TIME_STAMP_COUNTER)
};

/*
static DECLSPEC_ALIGN(16) VMX_MSR_ENTRY sVmEntryMsrLoad[] = {
};
*/

#undef CREATE_MSR_ENTRY

VOID
VcpuSetControl(
    _Inout_ PVCPU Vcpu,
    _In_ VMX_CONTROL Control,
    _In_ BOOLEAN State
);

BOOLEAN
VcpuIsControlSupported(
    _Inout_ PVCPU Vcpu,
    _In_ VMX_CONTROL Control
);

VOID
VcpuCommitVmxState(
    _Inout_ PVCPU Vcpu
);

VOID
VcpuToggleExitOnMsr(
    _Inout_ PVCPU Vcpu,
    _In_ UINT32 Msr,
    _In_ MSR_ACCESS Access
);

BOOLEAN
VcpuHandleExit(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
);

VOID
VcpuDestroy(
    _Inout_ PVCPU Vcpu
);

NTSTATUS
VcpuPostSpawnInitialisation(
    _Inout_ PVCPU Vcpu
);

UINT64
VcpuGetVmExitStoreValue(
    _In_ UINT32 Index
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
    Vcpu->Id = Id;

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

    Vcpu->MsrBitmap = (PCHAR)ImpAllocateHostContiguousMemory(PAGE_SIZE);
    if (Vcpu->MsrBitmap == NULL)
    {
        ImpDebugPrint("Failed to allocate MSR bitmap for VCPU #%d...\n", Id);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Vcpu->MsrBitmapPhysical = ImpGetPhysicalAddress(Vcpu->MsrBitmap);
    
    // VCPU stack cant be hidden because it is used breifly after VMLaunch
    Vcpu->Stack = (PVCPU_STACK)ImpAllocateNpPool(sizeof(VCPU_STACK));
    if (Vcpu->Stack == NULL)
    {
        ImpDebugPrint("Failed to allocate a host stack for VCPU #%d...\n", Id);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Vcpu->Stack->Cache.Vcpu = Vcpu;

    Vcpu->SystemDirectoryBase = __readcr3();

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

    VmxSetupVmxState(&Vcpu->Vmx);

    // VM-entry cannot change CR0.CD or CR0.NW
    Vcpu->Cr0ShadowableBits = ~(__readmsr(IA32_VMX_CR0_FIXED0) ^ __readmsr(IA32_VMX_CR0_FIXED1));
    Vcpu->Cr4ShadowableBits = ~(__readmsr(IA32_VMX_CR4_FIXED0) ^ __readmsr(IA32_VMX_CR4_FIXED1));

    // Make sure the VM enters in IA32e
    VcpuSetControl(Vcpu, VMX_CTL_HOST_ADDRESS_SPACE_SIZE, TRUE);
    VcpuSetControl(Vcpu, VMX_CTL_GUEST_ADDRESS_SPACE_SIZE, TRUE);

    VcpuSetControl(Vcpu, VMX_CTL_USE_MSR_BITMAPS, TRUE);
    VcpuSetControl(Vcpu, VMX_CTL_SECONDARY_CTLS_ACTIVE, TRUE);

    VcpuSetControl(Vcpu, VMX_CTL_ENABLE_RDTSCP, TRUE);
    VcpuSetControl(Vcpu, VMX_CTL_ENABLE_XSAVES_XRSTORS, TRUE);
    VcpuSetControl(Vcpu, VMX_CTL_ENABLE_INVPCID, TRUE);

    // Save GUEST_EFER on VM-exit
    //VcpuSetControl(Vcpu, VMX_CTL_SAVE_EFER_ON_EXIT, TRUE);

    VTscInitialise(&Vcpu->TscInfo);

    return STATUS_SUCCESS;
}

VOID
VcpuDestroy(
    _Inout_ PVCPU Vcpu
)
/*++
Routine Description:
    Frees all resources allocated for a VCPU
--*/
{
    // TODO: Finish this 
}

DECLSPEC_NORETURN
VOID
VcpuLaunch(VOID)
/*++
Routine Description:
    Post-VMLAUNCH, sets the CPU's registers and RIP to previous execution. 
--*/
{
    PVCPU_STACK Stack = (PVCPU_STACK)((UINT64)_AddressOfReturnAddress() - 0x5FF0);
    __cpu_restore_state(&Stack->Cache.Vcpu->LaunchState);
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

    ImpDebugPrint("Spawning VCPU #%d...\n", Vcpu->Id);

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

    // TODO: Define exception bitmap later in vmx.h
    VmxWrite(CONTROL_EXCEPTION_BITMAP, 0);

    // All page faults should cause VM-exits
    VmxWrite(CONTROL_PAGE_FAULT_ERROR_CODE_MASK, 0);
    VmxWrite(CONTROL_PAGE_FAULT_ERROR_CODE_MATCH, 0);

    VmxWrite(CONTROL_MSR_BITMAP_ADDRESS, Vcpu->MsrBitmapPhysical);

    // No nested virtualisation, set invalid VMCS link pointer
    VmxWrite(GUEST_VMCS_LINK_POINTER, ~0ULL);

    VmxWrite(CONTROL_CR0_READ_SHADOW, __readcr0());
    VmxWrite(CONTROL_CR0_GUEST_MASK, Vcpu->Cr0ShadowableBits);
    VmxWrite(GUEST_CR0, __readcr0());
    VmxWrite(HOST_CR0, __readcr0());

    VmxWrite(CONTROL_CR4_READ_SHADOW, __readcr4());
    VmxWrite(CONTROL_CR4_GUEST_MASK, Vcpu->Cr4ShadowableBits);
    VmxWrite(GUEST_CR4, __readcr4());
    VmxWrite(HOST_CR4, __readcr4());

    VmxWrite(GUEST_CR3, __readcr3());
    VmxWrite(HOST_CR3, Vcpu->SystemDirectoryBase);
    //VmxWrite(HOST_CR3, Vcpu->Vmm->MmSupport->HostDirectoryPhysical);

    VmxWrite(GUEST_DR7, __readdr(7));

    VmxWrite(GUEST_RFLAGS, Vcpu->LaunchState.RFlags);
    VmxWrite(GUEST_DEBUGCTL, __readmsr(IA32_DEBUGCTL));
    VmxWrite(GUEST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
    VmxWrite(GUEST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
    VmxWrite(GUEST_SYSENTER_CS, __readmsr(IA32_SYSENTER_CS));

    X86_PSEUDO_DESCRIPTOR Gdtr = {0}, Idtr = {0};
    __sgdt(&Gdtr);
    __sidt(&Idtr);

    VmxWrite(GUEST_GDTR_LIMIT, Gdtr.Limit);
    VmxWrite(GUEST_IDTR_LIMIT, Idtr.Limit);

    VmxWrite(GUEST_GDTR_BASE, Gdtr.BaseAddress);
    VmxWrite(HOST_GDTR_BASE, Gdtr.BaseAddress);

    VmxWrite(GUEST_IDTR_BASE, Idtr.BaseAddress);
    VmxWrite(HOST_IDTR_BASE, Idtr.BaseAddress);
    //VmxWrite(HOST_IDTR_BASE, Vcpu->Vmm->HostInterruptDescriptor.BaseAddress);

    X86_SEGMENT_SELECTOR Segment = {0};

    Segment.Value = __readcs();

    VmxWrite(GUEST_CS_SELECTOR, Segment.Value);
    VmxWrite(GUEST_CS_LIMIT, __segmentlimit(Segment.Value));
    VmxWrite(GUEST_CS_ACCESS_RIGHTS, SegmentAr(Segment));
    VmxWrite(GUEST_CS_BASE, SegmentBaseAddress(Segment));
    VmxWrite(HOST_CS_SELECTOR, Segment.Value & HOST_SEGMENT_SELECTOR_MASK);

    Segment.Value = __readss();

    VmxWrite(GUEST_SS_SELECTOR, Segment.Value);
    VmxWrite(GUEST_SS_LIMIT, __segmentlimit(Segment.Value));
    VmxWrite(GUEST_SS_ACCESS_RIGHTS, SegmentAr(Segment));
    VmxWrite(GUEST_SS_BASE, SegmentBaseAddress(Segment));
    VmxWrite(HOST_SS_SELECTOR, Segment.Value & HOST_SEGMENT_SELECTOR_MASK);

    Segment.Value = __readds();

    VmxWrite(GUEST_DS_SELECTOR, Segment.Value);
    VmxWrite(GUEST_DS_LIMIT, __segmentlimit(Segment.Value));
    VmxWrite(GUEST_DS_ACCESS_RIGHTS, SegmentAr(Segment));
    VmxWrite(GUEST_DS_BASE, SegmentBaseAddress(Segment));
    VmxWrite(HOST_DS_SELECTOR, Segment.Value & HOST_SEGMENT_SELECTOR_MASK);

    Segment.Value = __reades();

    VmxWrite(GUEST_ES_SELECTOR, Segment.Value);
    VmxWrite(GUEST_ES_LIMIT, __segmentlimit(Segment.Value));
    VmxWrite(GUEST_ES_ACCESS_RIGHTS, SegmentAr(Segment));
    VmxWrite(GUEST_ES_BASE, SegmentBaseAddress(Segment));
    VmxWrite(HOST_ES_SELECTOR, Segment.Value & HOST_SEGMENT_SELECTOR_MASK);

    Segment.Value = __readfs();

    VmxWrite(GUEST_FS_SELECTOR, Segment.Value);
    VmxWrite(GUEST_FS_LIMIT, __segmentlimit(Segment.Value));
    VmxWrite(GUEST_FS_ACCESS_RIGHTS, SegmentAr(Segment));
    VmxWrite(GUEST_FS_BASE, SegmentBaseAddress(Segment));
    VmxWrite(HOST_FS_BASE, SegmentBaseAddress(Segment));
    VmxWrite(HOST_FS_SELECTOR, Segment.Value & HOST_SEGMENT_SELECTOR_MASK);

    Segment.Value = __readgs();

    VmxWrite(GUEST_GS_SELECTOR, Segment.Value);
    VmxWrite(GUEST_GS_LIMIT, __segmentlimit(Segment.Value));
    VmxWrite(GUEST_GS_ACCESS_RIGHTS, SegmentAr(Segment));
    VmxWrite(GUEST_GS_BASE, __readmsr(IA32_GS_BASE));
    VmxWrite(HOST_GS_BASE, __readmsr(IA32_GS_BASE));
    VmxWrite(HOST_GS_SELECTOR, Segment.Value & HOST_SEGMENT_SELECTOR_MASK);

    Segment.Value = __readldt();

    VmxWrite(GUEST_LDTR_SELECTOR, Segment.Value);
    VmxWrite(GUEST_LDTR_LIMIT, __segmentlimit(Segment.Value));
    VmxWrite(GUEST_LDTR_ACCESS_RIGHTS, SegmentAr(Segment));
    VmxWrite(GUEST_LDTR_BASE, SegmentBaseAddress(Segment));

    Segment.Value = __readtr();

    VmxWrite(GUEST_TR_SELECTOR, Segment.Value);
    VmxWrite(GUEST_TR_LIMIT, __segmentlimit(Segment.Value));
    VmxWrite(GUEST_TR_ACCESS_RIGHTS, SegmentAr(Segment));
    VmxWrite(GUEST_TR_BASE, SegmentBaseAddress(Segment));
    VmxWrite(HOST_TR_BASE, SegmentBaseAddress(Segment));
    VmxWrite(HOST_TR_SELECTOR, Segment.Value & HOST_SEGMENT_SELECTOR_MASK);

    // Setup MSR load & store regions
    VmxWrite(CONTROL_VMEXIT_MSR_STORE_COUNT, sizeof(sVmExitMsrStore) / sizeof(*sVmExitMsrStore));
    //VmxWrite(CONTROL_VMENTRY_MSR_LOAD_COUNT, sizeof(sVmEntryMsrLoad) / sizeof(*sVmEntryMsrLoad));

    VmxWrite(CONTROL_VMEXIT_MSR_STORE_ADDRESS, ImpGetPhysicalAddress(sVmExitMsrStore));
    //VmxWrite(CONTROL_VMENTRY_MSR_LOAD_ADDRESS, ImpGetPhysicalAddress(sVmEntryMsrLoad));

    VcpuCommitVmxState(Vcpu);
    
    VmxWrite(HOST_RSP, (UINT64)(Vcpu->Stack->Limit + 0x6000 - 16));
    VmxWrite(GUEST_RSP, (UINT64)(Vcpu->Stack->Limit + 0x6000 - 16));

    VmxWrite(HOST_RIP, (UINT64)__vmexit_entry);
    VmxWrite(GUEST_RIP, (UINT64)VcpuLaunch);

    Vcpu->IsLaunched = TRUE;
    
    __vmx_vmlaunch();

    return STATUS_APP_INIT_FAILURE;
}

VOID
VcpuSpawnPerCpu(
    _Inout_ PVCPU_SPAWN_PARAMS Params
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

    __cpu_save_state(&Vcpu->LaunchState);
    
    // Control flow is restored here upon successful virtualisation of the CPU
    if (Vcpu->IsLaunched)
    {
        InterlockedIncrement(&Params->ActiveVcpuCount);
        ImpDebugPrint("VCPU #%d is now running...\n", Vcpu->Id);

        /*
        if (!NT_SUCCESS(VcpuPostSpawnInitialisation(Vcpu)))
        {
            // TODO: Panic shutdown here
            ImpDebugPrint("VCPU #%d failed post spawn initialsation...\n", Vcpu->Id);
            return;
        }
        */

        return;
    }

    Params->Status = VcpuSpawn(Vcpu);

    if (!NT_SUCCESS(Params->Status))
    {
        InterlockedOr(&Params->FailedCoreMask, 1 << CpuId);

        // Shutdown VMX operation on this CPU core if we failed VM launch
        if (Params->Status == STATUS_APP_INIT_FAILURE)
        {
            ImpDebugPrint("VMLAUNCH failed on VCPU #%d... (%x)\n", Vcpu->Id, VmxRead(VM_INSTRUCTION_ERROR));
            __vmx_off();
        }
    }
}

VOID
VcpuShutdownPerCpu(
    _Inout_ PVCPU_SHUTDOWN_PARAMS Params
)
/*++
Routine Description:
    Checks if this core has been virtualised, and stops VMX operation if so. This function will call
    many Windows functions and therefore may be considered unsafe when operating under an anti-cheat.
    This function returns the VMM context pointer in VCPU::Vmm to VmmShutdownHypervisor so it can free VMM resources
--*/
{
    PVCPU Vcpu = NULL;
    if (VmShutdownVcpu(&Vcpu) != HRESULT_SUCCESS)
    {
        Params->Status = STATUS_FATAL_APP_EXIT;
        return;
    }

    // Free all VCPU related resources
    VcpuDestroy(Vcpu);
    
    // The VMM context and the VCPU table are allocated as host memory, but after VmShutdownVcpu, all EPT will be disabled
    // and any host memory will be visible again
    Params->VmmContext = Vcpu->Vmm;
}

DECLSPEC_NORETURN
VOID
VcpuShutdownVmx(
    _Inout_ PVCPU Vcpu,
    _Inout_ PCPU_STATE CpuState
)
/*++
Routine Description:
    This function is called by __vmexit_entry when either HYPERCALL_SHUTDOWN_VCPU has been requested, or the VM-exit handler 
    encountered a panic. It restores guest register and non-register state from the current VMCS and terminates VMX operation.
--*/
{
    __cpu_restore_state(CpuState);
}

NTSTATUS
VcpuPostSpawnInitialisation(
    _Inout_ PVCPU Vcpu
)
/*++
Routine Description:
    This function performs all post-VMLAUNCH initialisation that might be required, such as measuing VM-exit and VM-entry latencies
--*/
{
    VTscEstimateVmExitLatency(&Vcpu->TscInfo);
    //VTscEstimateVmEntryLatency(&Vcpu->TscInfo);

    return STATUS_SUCCESS;
}

UINT64
VcpuGetVmExitStoreValue(
    _In_ UINT32 Index
)
/*++
Routine Description:
    This function looks for `Index` inside an MSR load/store region and returns the value
--*/
{
    for (SIZE_T i = 0; i < sizeof(sVmExitMsrStore) / sizeof(*sVmExitMsrStore); i++)
        if (sVmExitMsrStore[i].Index == Index)
            return sVmExitMsrStore[i].Value;

    return -1;
}

VOID
VcpuSetControl(
    _Inout_ PVCPU Vcpu,
    _In_ VMX_CONTROL Control,
    _In_ BOOLEAN State
)
/*++
Routine Description:
    Sets the state of a VM execution control for a VCPU
 */
{
    VmxSetControl(&Vcpu->Vmx, Control, State);
}

BOOLEAN
VcpuIsControlSupported(
    _Inout_ PVCPU Vcpu,
    _In_ VMX_CONTROL Control
)
/*++
Routine Description:
    Checks if a control is supported by this processor by checking the VMX execution capability registers
--*/
{
    return VmxIsControlSupported(&Vcpu->Vmx, Control);
}

VOID
VcpuCommitVmxState(
    _Inout_ PVCPU Vcpu
)
/*++
Routine Description:
    Updates the VM execution control VMCS components with the VMX state
--*/
{
    VmxWrite(CONTROL_PINBASED_CONTROLS, Vcpu->Vmx.Controls.PinbasedCtls);
    VmxWrite(CONTROL_PRIMARY_PROCBASED_CONTROLS, Vcpu->Vmx.Controls.PrimaryProcbasedCtls);
    VmxWrite(CONTROL_SECONDARY_PROCBASED_CONTROLS, Vcpu->Vmx.Controls.SecondaryProcbasedCtls);
    VmxWrite(CONTROL_VMENTRY_CONTROLS, Vcpu->Vmx.Controls.VmEntryCtls);
    VmxWrite(CONTROL_VMEXIT_CONTROLS, Vcpu->Vmx.Controls.VmExitCtls);
    VmxWrite(CONTROL_TERTIARY_PROCBASED_CONTROLS, 0);
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
VOID
VcpuResume(VOID)
/*++
Routine Description:
    Executes VMRESUME to resume VMX guest operation.
--*/
{
    __vmx_vmresume();

    ImpDebugPrint("VMRESUME failed... (%x)\n", VmxRead(VM_INSTRUCTION_ERROR));

    // TODO: Shutdown entire hypervisor from here (VmmShutdownHypervisor)
    // TODO: Check out? Try using a VMM to get elevated threads by exiting VMX operation and letting
    //       the thread execute in kernel mode! Should fuck over every anticheat known to man
    __vmx_off();
    __debugbreak();
}

BOOLEAN
VcpuHandleExit(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
/*++
Routine Description:
    Loads the VCPU with new data post VM-exit and calls the correct VM-exit handler, handles any critical errors
    by immediately shutting down this VCPU and signaling to the other VCPU's that they should shutdown too
--*/
{	
    Vcpu->Vmx.GuestRip = VmxRead(GUEST_RIP); 
    Vcpu->Vmx.ExitReason.Value = (UINT32)VmxRead(VM_EXIT_REASON);

    VMM_EVENT_STATUS Status = sExitHandlers[Vcpu->Vmx.ExitReason.BasicExitReason](Vcpu, GuestState);

    if (Vcpu->IsShuttingDown)
        Status = VMM_EVENT_ABORT;

    if (Status == VMM_EVENT_ABORT)
        return FALSE;

    GuestState->Rip = (UINT64)VcpuResume;

    VcpuCommitVmxState(Vcpu);

    if (Status == VMM_EVENT_CONTINUE)
        VmxAdvanceGuestRip();

    return TRUE;
}

VMM_EVENT_STATUS
VcpuUnknownExitReason(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
/*++
Routine Description:
    Handles any unknown/unhandled VM-exit reasons by signalling the hypervisor to shutdown
--*/
{
    UNREFERENCED_PARAMETER(GuestState);

    ImpDebugPrint("Unknown VM-exit reason (%d) on VCPU #%d...\n", Vcpu->Vmx.ExitReason.BasicExitReason, Vcpu->Id);
    
    return VMM_EVENT_ABORT;
}

VOID
VcpuEnableTscSpoofing(
    _Inout_ PVCPU Vcpu
)
/*++
Routine Description:
    This function enables TSC spoofing for the next while
--*/
{
    Vcpu->TscInfo.SpoofEnabled = TRUE;

    VcpuSetControl(Vcpu, VMX_CTL_SAVE_VMX_PREEMPTION_VALUE, TRUE);
    VcpuSetControl(Vcpu, VMX_CTL_VMX_PREEMPTION_TIMER, TRUE);
    VcpuSetControl(Vcpu, VMX_CTL_RDTSC_EXITING, TRUE);

    // Write the TSC watchdog quantum
    VmxWrite(GUEST_VMX_PREEMPTION_TIMER_VALUE, VTSC_WATCHDOG_QUANTUM + Vcpu->TscInfo.VmEntryLatency);
}

UINT64
VcpuGetTscEventLatency(
    _In_ PVCPU Vcpu,
    _In_ TSC_EVENT_TYPE Type
)
{
    switch (Type) 
    {
    case TSC_EVENT_CPUID: return Vcpu->TscInfo.CpuidLatency;
    case TSC_EVENT_RDTSC: return Vcpu->TscInfo.RdtscLatency;
    case TSC_EVENT_RDTSCP: return Vcpu->TscInfo.RdtscpLatency;
    default: return 0;
    }
}

VOID
VcpuUpdateLastTscEventEntry(
    _Inout_ PVCPU Vcpu,
    _In_ TSC_EVENT_TYPE Type
)
{
    // TODO: Also spoof TSC values during EPT violations, some anti-cheats monitor those 
    
    // Try find a previous event to base our value off
    PTSC_EVENT_ENTRY PrevEvent = &Vcpu->TscInfo.PrevEvent; 
    if (PrevEvent->Valid)
    {
        // Calculate the time since the last event during this thread
        UINT64 ElapsedTime = VTSC_WATCHDOG_QUANTUM - VmxRead(GUEST_VMX_PREEMPTION_TIMER_VALUE);

        // Base the new timestamp off the last TSC event for this thread
        UINT64 Timestamp = PrevEvent->Timestamp + PrevEvent->Latency + ElapsedTime;

        PrevEvent->Type = Type;
        PrevEvent->Timestamp = Timestamp;
        PrevEvent->Latency = VcpuGetTscEventLatency(Vcpu, Type);

        ImpDebugPrint("[02%X] PrevEvent->Valid: TSC = %u + %u + %u\n", Vcpu->Id, PrevEvent->Timestamp, PrevEvent->Latency, ElapsedTime);
    } 
    else
    {
        // No preceeding records, approximate the value of the TSC before exiting using the stored MSR
        UINT64 Timestamp = 
            VcpuGetVmExitStoreValue(IA32_TIME_STAMP_COUNTER) - Vcpu->TscInfo.VmExitLatency;

        PrevEvent->Valid = TRUE;
        PrevEvent->Type = Type;
        PrevEvent->Timestamp = Timestamp;
        PrevEvent->Latency = VcpuGetTscEventLatency(Vcpu, Type);

        ImpDebugPrint("[02%X] !PrevEvent->Valid: TSC = %u + %u\n", Vcpu->Id, Timestamp, PrevEvent->Latency);
    }
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
    X86_CPUID_ARGS CpuidArgs = {
        .Eax = (UINT32)GuestState->Rax,
        .Ebx = (UINT32)GuestState->Rbx,
        .Ecx = (UINT32)GuestState->Rcx,
        .Edx = (UINT32)GuestState->Rdx,
    };

    __cpuidex(CpuidArgs.Data, GuestState->Rax, GuestState->Rcx);

    switch (GuestState->Rax)
    {
    case 0x00000001:
        CpuidArgs.Data[FEATURE_REG(X86_FEATURE_HYPERVISOR)] &= ~FEATURE_MASK(X86_FEATURE_HYPERVISOR);
        break;
    }

    GuestState->Rax = CpuidArgs.Eax;
    GuestState->Rbx = CpuidArgs.Ebx;
    GuestState->Rcx = CpuidArgs.Ecx;
    GuestState->Rdx = CpuidArgs.Edx;

    if (Vcpu->Vmm->UseTscSpoofing)
    {
        if (!Vcpu->TscInfo.SpoofEnabled)
            VcpuEnableTscSpoofing(Vcpu);

        VcpuUpdateLastTscEventEntry(Vcpu, TSC_EVENT_CPUID);
    }

    return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VcpuHandleInvalidGuestState(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
/*++
Routine Description:
    Emulates the checks made on the guest state outlined in the Intel SDM Vol. 3 Chapter 26.3.1
--*/
{
    UNREFERENCED_PARAMETER(GuestState);
    
    ImpDebugPrint("[%02X] VM-entry failed due to invalid guest state...\n", Vcpu->Id);
    
    return VMM_EVENT_CONTINUE;
}

PUINT64
LookupTargetReg(
    _In_ PGUEST_STATE GuestState,
    _In_ UINT64 RegisterId
)
/*++
Routine Description:
    Converts a register to a pointer to the corresponding register inside a GUEST_STATE structure 
--*/
{
    static const UINT64 sRegIdToGuestStateOffs[] = {
        /* RAX */ offsetof(GUEST_STATE, Rax),
        /* RCX */ offsetof(GUEST_STATE, Rcx),
        /* RDX */ offsetof(GUEST_STATE, Rdx),
        /* RBX */ offsetof(GUEST_STATE, Rbx),
        /* RSP */ 0,
        /* RBP */ 0,
        /* RSI */ offsetof(GUEST_STATE, Rsi),
        /* RDI */ offsetof(GUEST_STATE, Rdi),
        /* R8  */ offsetof(GUEST_STATE, R8),
        /* R9  */ offsetof(GUEST_STATE, R9),
        /* R10 */ offsetof(GUEST_STATE, R10),
        /* R11 */ offsetof(GUEST_STATE, R11),
        /* R12 */ offsetof(GUEST_STATE, R12),
        /* R13 */ offsetof(GUEST_STATE, R13),
        /* R14 */ offsetof(GUEST_STATE, R14),
        /* R15 */ offsetof(GUEST_STATE, R15)
    };

    return (PUINT64)((UINT64)GuestState + sRegIdToGuestStateOffs[RegisterId]);
}

VMM_EVENT_STATUS
VcpuHandleCrRead(
    _In_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState,
    _In_ VMX_MOV_CR_EXIT_QUALIFICATION ExitQual
)
{
    UINT64 ControlRegister = 0;
    switch (ExitQual.ControlRegisterId)
    {
    case 3: ControlRegister = VmxRead(GUEST_CR3); break; 
    case 8: ControlRegister = __readcr8(); break; 
    }

    // Write to GUEST_RSP if the target register was RSP instead of the RSP inside of GUEST_STATE
    if (ExitQual.ControlRegisterId == 4 /* RSP */)
        VmxWrite(GUEST_RSP, ControlRegister);
    else
    {
        *LookupTargetReg(GuestState, ExitQual.RegisterId) = ControlRegister;
    }

    return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VcpuUpdateGuestCr(
    _In_ UINT64 ControlReg,
    _In_ UINT64 NewValue,
    _In_ UINT64 DifferentBits,
    _In_ VMCS CrEncoding,
    _In_ VMCS RsEncoding,
    _In_ UINT64 ShadowableBits,
    _In_ UINT64 ReservedBits
)
/*++
Routine Description:
    Updates a guest CR and its read shadow, making sure that all bits inside of the guest/host mask are
    handled properly. TODO: Support for bits that will never change, and bits that can change in both and
    are just in the guest/host mask to intercept people changing them (TRAP and FIXED)
--*/
{
    UINT64 ReadShadow = VmxRead(RsEncoding);

    // Inject #GP(0) if there was an attempt to modify a reserved bit
    if (DifferentBits & ReservedBits)
    {
        VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
        return VMM_EVENT_INTERRUPT;
    }
    
    // Bits which can be updated inside of the read shadow TODO: (TRAP | SHADOWABLE)
    const UINT64 RsUpdateableBits = ShadowableBits;

    ReadShadow &= ~(DifferentBits & RsUpdateableBits);
    ReadShadow |= (NewValue & RsUpdateableBits);

    VmxWrite(RsEncoding, ReadShadow);

    // Bits which can be updated inside of the read shadow TODO: ~(SHADOWABLE | FIXED) 
    const UINT64 CrUpdateableBits = ~ShadowableBits;

    ControlReg &= ~(DifferentBits & CrUpdateableBits);
    ControlReg |= (NewValue & CrUpdateableBits);

    VmxWrite(CrEncoding, ControlReg);

    return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VcpuHandleCr0Write(
    _Inout_ PVCPU Vcpu,
    _In_ UINT64 NewValue
)
{
    VMM_EVENT_STATUS Status = VMM_EVENT_CONTINUE;

    UINT64 ControlRegister = VmxRead(GUEST_CR0);
    UINT64 ShadowableBits = Vcpu->Cr0ShadowableBits;

    const X86_CR0 DifferentBits = {
        .Value = NewValue ^ ControlRegister
    };

    const X86_CR0 NewCr = {
        .Value = NewValue
    };

    BOOLEAN PagingDisabled = (DifferentBits.Paging && NewCr.Paging == 0);
    BOOLEAN ProtectedModeDisabled = (DifferentBits.ProtectedMode && NewCr.ProtectedMode == 0);

    // VM-entry requires CR0.PG and CR0.PE to be enabled without unrestricted guest
    if (PagingDisabled || ProtectedModeDisabled)
    {
        if (Vcpu->Vmm->UseUnrestrictedGuests &&
            VcpuIsControlSupported(Vcpu, VMX_CTL_UNRESTRICTED_GUEST))
        {
            // TODO: Create identity map CR3
            VcpuSetControl(Vcpu, VMX_CTL_UNRESTRICTED_GUEST, TRUE);

            // Remove CR0.PG and CR0.PE from the shadowable bits bitmask, as they can now be modified
            ShadowableBits &= ~CR0_PE_PG_BITMASK;

            Vcpu->UnrestrictedGuest = TRUE;
        }
        else
        {
            // TODO: Shutdown the VMM and report to the client what happened.
            // Do we really need to abort here?
            return VMM_EVENT_ABORT;
        }
    }

    // Disable unrestricted guest if paging is enabled again
    if (!PagingDisabled && !ProtectedModeDisabled)
    {
        VcpuSetControl(Vcpu, VMX_CTL_UNRESTRICTED_GUEST, FALSE);
        Vcpu->UnrestrictedGuest = FALSE;
    }

    // Handle invalid states of CR0.PE and CR0.PG when unrestricted guest is on
    if (Vcpu->UnrestrictedGuest)
    {
        if (NewCr.Paging && !NewCr.ProtectedMode)
        {
            VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
            return VMM_EVENT_INTERRUPT;
        }

        X86_CR4 Cr4 = {
            .Value = VmxRead(GUEST_CR4)
        };

        if (DifferentBits.Paging && !NewCr.Paging && Cr4.PCIDEnable)
        {
            VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
            return VMM_EVENT_INTERRUPT;
        }

        // If CR0.PG is changed to a 0, all TLBs are invalidated
        if (DifferentBits.Paging && !NewCr.Paging)
            VmxInvvpid(INV_SINGLE_CONTEXT, 1);
    }

    // If CR0.PG has been changed, update IA32_EFER.LMA
    if (DifferentBits.Paging)
    {
        IA32_EFER_MSR Efer = {
            .Value = VmxRead(GUEST_EFER)
        };

        // IA32_EFER.LMA = CR0.PG & IA32_EFER.LME, VM-entry sets IA32_EFER.LMA for the guest
        // from the guest address space size control
        VcpuSetControl(Vcpu, VMX_CTL_GUEST_ADDRESS_SPACE_SIZE, (Efer.LongModeEnable && NewCr.Paging));

        X86_CR4 Cr4 = {
            .Value = VmxRead(GUEST_CR4)
        };

        // If CR0.PG has been enabled, we must check if we are in PAE paging
        if (NewCr.Paging && !Efer.LongModeEnable && Cr4.PhysicalAddressExtension)
        {
            // TODO: Setup PDPTR in VMCS
        }
    }

    if (!NewCr.CacheDisable && NewCr.NotWriteThrough)
    {
        VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
        return VMM_EVENT_INTERRUPT;
    }

    return VcpuUpdateGuestCr(
        ControlRegister,			// Control registers current value
        NewValue,					// The value being written
        DifferentBits.Value,		// Bits which have been modified
        GUEST_CR0,					// The VMCS encoding for the CR
        CONTROL_CR0_READ_SHADOW,	// The VMCS encoding for the CR's read shadow
        ShadowableBits,				// Host-maskable bits (bits that can be changed in the read shadow)
        CR0_RESERVED_BITMASK		// Reserved bits in the CR which shouldn't be set
    );

    // TODO: Remove this and emulate instead
    // If NW or CD has been modified, instead of emulating the effects of these bits, instead 
    // remove all changes to any host-owned bits in the register writing to CR0 then return 
    // VMM_EVENT_INTERRUPT so the instruction gets executed upon VM-entry but doesn't cause an exit
    // NOTE: This is kinda dangerous but I'm lazy and dont want to emulate CD or NW but i will in the 
    // future.
    /*
    if (DifferentBits.NotWriteThrough ||
        DifferentBits.CacheDisable)
    {
        const PUINT64 TargetReg = LookupTargetReg(GuestState, ExitQual.RegisterId);

        // Reset any host owned bits to the CR value 
        *TargetReg &= ~(DifferentBits.Value & ShadowableBits);
        *TargetReg |= ControlRegister & ShadowableBits;

        // TODO: Enable MTF interrupt and push a pending CR spoof
        return VMM_EVENT_INTERRUPT;
    }
    */
}

VMM_EVENT_STATUS
VcpuHandleCr4Write(
    _Inout_ PVCPU Vcpu,
    _Inout_ UINT64 NewValue
)
{
    VMM_EVENT_STATUS Status = VMM_EVENT_CONTINUE;

    UINT64 ControlRegister = VmxRead(GUEST_CR4);
    UINT64 ShadowableBits = Vcpu->Cr4ShadowableBits;

    const X86_CR4 DifferentBits = {
        .Value = NewValue ^ ControlRegister
    };

    const X86_CR4 NewCr = {
        .Value = NewValue
    };

    // TODO: Check CR4.VMXE being set, make sure its 0 in the read shadow and inject #UD upon trying
    // to change it

    const IA32_EFER_MSR Efer = {
        .Value = VmxRead(GUEST_EFER)
    };

    if (DifferentBits.PCIDEnable && NewCr.PCIDEnable == 1 &&
        ((VmxRead(GUEST_CR3) & 0xFFF) != 0 || (Efer.LongModeActive == 0)))
    {
        VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
        return VMM_EVENT_INTERRUPT;
    }

    if (DifferentBits.PhysicalAddressExtension && NewCr.PhysicalAddressExtension == 0 &&
        Efer.LongModeEnable)
    {
        VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
        return VMM_EVENT_INTERRUPT;
    }

    Status = VcpuUpdateGuestCr(
        ControlRegister,
        NewValue,
        DifferentBits.Value,
        GUEST_CR4,
        CONTROL_CR4_READ_SHADOW,
        ShadowableBits,
        CR4_RESERVED_BITMASK
    );

    if (DifferentBits.GlobalPageEnable ||
        DifferentBits.PhysicalAddressExtension ||
        (DifferentBits.PCIDEnable && NewCr.PCIDEnable == 0) ||
        (DifferentBits.SmepEnable && NewCr.SmepEnable == 1))
        VmxInvvpid(INV_SINGLE_CONTEXT, 1);
}

VMM_EVENT_STATUS
VcpuHandleCrWrite(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState,
    _In_ VMX_MOV_CR_EXIT_QUALIFICATION ExitQual
)
/*++
Routine Description:
    Emulates a write to a control register, as well as emulating changes in any bits that are
    required by VMX operation
--*/
{
    VMM_EVENT_STATUS Status = VMM_EVENT_CONTINUE;

    const PUINT64 TargetReg = LookupTargetReg(GuestState, ExitQual.RegisterId);

    UINT64 NewValue = 0;
    if (ExitQual.ControlRegisterId == 4 /* RSP */)
        NewValue = VmxRead(GUEST_RSP);
    else
        NewValue = *TargetReg;

    switch (ExitQual.ControlRegisterId)
    {
    case 0: VcpuHandleCr0Write(Vcpu, NewValue); break;
    case 3:
        {
            // TODO: Validate CR3 value
            VmxWrite(GUEST_CR3, NewValue);
        } break;

    case 4: VcpuHandleCr4Write(Vcpu, NewValue); break;
    case 8:
        {
            // TODO: Validate CR8 value
            __writecr8(NewValue);
        } break;
    }

    return Status;
}

VMM_EVENT_STATUS
VcpuEmulateCLTS(
    _Inout_ PVCPU Vcpu,
    _In_ VMX_MOV_CR_EXIT_QUALIFICATION ExitQual
)
{
    X86_CR0 Cr0 = {
        .Value = VmxRead(GUEST_CR0)
    };

    Cr0.TaskSwitched = FALSE;

    VmxWrite(GUEST_CR0, Cr0.Value);

    return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VcpuEmulateLMSW(
    _Inout_ PVCPU Vcpu,
    _In_ VMX_MOV_CR_EXIT_QUALIFICATION ExitQual
)
{
    // TODO: Finish this
    return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VcpuHandleCrAccess(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
{
    VMM_EVENT_STATUS Status = VMM_EVENT_CONTINUE;

    VMX_MOV_CR_EXIT_QUALIFICATION ExitQual = {
        .Value = VmxRead(VM_EXIT_QUALIFICATION)
    };

    X86_SEGMENT_ACCESS_RIGHTS GuestCsAr = {
        .Value = VmxRead(GUEST_CS_ACCESS_RIGHTS)
    };

    if (GuestCsAr.Dpl != 0)
    {
        VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
        return VMM_EVENT_INTERRUPT;
    }

    switch (ExitQual.AccessType)
    {
    case CR_ACCESS_READ: Status = VcpuHandleCrRead(Vcpu, GuestState, ExitQual); break;
    case CR_ACCESS_WRITE: Status = VcpuHandleCrWrite(Vcpu, GuestState, ExitQual); break;
    case CR_ACCESS_CTLS: Status = VcpuEmulateCLTS(Vcpu, ExitQual); break;
    case CR_ACCESS_LMSW: Status = VcpuEmulateLMSW(Vcpu, ExitQual); break;
    }

    return Status;
}

VMM_EVENT_STATUS
VcpuHandleVmxInstruction(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
/*++
Routine Description:
    Emulates execution of VMX related instructions by injecting a #UD exception
--*/
{
    VmxInjectEvent(EXCEPTION_UNDEFINED_OPCODE, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
    return VMM_EVENT_INTERRUPT;
}

VMM_EVENT_STATUS
VcpuHandleHypercall(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
/*++
Routine Description:
    This function handles the guest->host bridge known as hypercalls, which allows trusted applications
    to interact with the driver and control or use the functionality built into it
--*/
{
    // Check if we are measuring VM-exit latency
    if (GuestState->Rbx == 0x1FF2C88911424416 && Vcpu->TscInfo.VmExitLatency == 0)
    {
        LARGE_INTEGER PreTsc = {
            .LowPart = GuestState->Rax,
            .HighPart = GuestState->Rdx
        };

        GuestState->Rax = VcpuGetVmExitStoreValue(IA32_TIME_STAMP_COUNTER) - PreTsc.QuadPart;

        return VMM_EVENT_CONTINUE;
    }

    if (GuestState->Rbx == 0xF2C889114244161F && Vcpu->TscInfo.VmEntryLatency == 0)
    {
        // TODO: Set up MTF queue
        VcpuSetControl(Vcpu, VMX_CTL_MONITOR_TRAP_FLAG, TRUE);
    }

    PHYPERCALL_INFO Hypercall = (PHYPERCALL_INFO)&GuestState->Rax;
    return VmHandleHypercall(Vcpu, GuestState, Hypercall);
}

VMM_EVENT_STATUS
VcpuHandleRdtsc(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
{
    X86_CR4 Cr4 = {
        .Value = VmxRead(CONTROL_CR4_READ_SHADOW)
    };

    X86_SEGMENT_ACCESS_RIGHTS GuestCsAr = {
        .Value = VmxRead(GUEST_CS_ACCESS_RIGHTS)
    };

    if (Cr4.TimeStampDisable && GuestCsAr.Dpl != 0)
    {
        VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
        return VMM_EVENT_INTERRUPT;
    }

    LARGE_INTEGER Tsc = {
        .QuadPart = __rdtsc()
    };

    if (Vcpu->TscInfo.SpoofEnabled)
    {
        VcpuUpdateLastTscEventEntry(Vcpu, TSC_EVENT_RDTSC);

        PTSC_EVENT_ENTRY PrevEvent = &Vcpu->TscInfo.PrevEvent;
        Tsc.QuadPart = PrevEvent->Timestamp + PrevEvent->Latency; 
    }

    GuestState->Rdx = Tsc.HighPart;
    GuestState->Rax = Tsc.LowPart;

    return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VcpuHandleRdtscp(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
{
    X86_CR4 Cr4 = {
        .Value = VmxRead(CONTROL_CR4_READ_SHADOW)
    };

    X86_SEGMENT_ACCESS_RIGHTS GuestCsAr = {
        .Value = VmxRead(GUEST_CS_ACCESS_RIGHTS)
    };

    if (Cr4.TimeStampDisable && GuestCsAr.Dpl != 0)
    {
        VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
        return VMM_EVENT_INTERRUPT;
    }

    LARGE_INTEGER Tsc = {
        .QuadPart = __rdtscp(&GuestState->Rcx)
    };

    if (Vcpu->TscInfo.SpoofEnabled)
    {
        VcpuUpdateLastTscEventEntry(Vcpu, TSC_EVENT_RDTSCP);

        PTSC_EVENT_ENTRY PrevEvent = &Vcpu->TscInfo.PrevEvent;
        Tsc.QuadPart = PrevEvent->Timestamp + PrevEvent->Latency; 
    }

    GuestState->Rdx = Tsc.HighPart;
    GuestState->Rax = Tsc.LowPart;

    return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VcpuHandleTimerExpire(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
{
    ImpDebugPrint("[02%X] VMX preemption timer expired...\n", Vcpu->Id);

    // TODO: Disable VMX preemption timer and TSC spoofing
    if (Vcpu->TscInfo.SpoofEnabled)
    {
        Vcpu->TscInfo.PrevEvent.Valid = FALSE;

        VcpuSetControl(Vcpu, VMX_CTL_SAVE_VMX_PREEMPTION_VALUE, FALSE);
        VcpuSetControl(Vcpu, VMX_CTL_VMX_PREEMPTION_TIMER, FALSE);
        VcpuSetControl(Vcpu, VMX_CTL_RDTSC_EXITING, FALSE);

        VmxWrite(GUEST_VMX_PREEMPTION_TIMER_VALUE, 0);

        Vcpu->TscInfo.SpoofEnabled = FALSE;
    }

    return VMM_EVENT_CONTINUE;
}

BOOLEAN
VcpuValidateMsr(
    _In_ UINT64 Msr
)
/*++
Routine Description:
    Validates if an MSR is valid to read/write from
--*/
{
    // TODO: Finish this
    return FALSE;
}

VMM_EVENT_STATUS
VcpuHandleMsrWrite(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
{
    UINT64 Msr = GuestState->Rcx;

    if (!VcpuValidateMsr(Msr))
    {
        VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
        return VMM_EVENT_INTERRUPT;
    }

    LARGE_INTEGER MsrValue = {
        .HighPart = GuestState->Rdx, .LowPart = GuestState->Rax
    };

    __writemsr(Msr, MsrValue.QuadPart);

    return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VcpuHandleMsrRead(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
{
    UINT64 Msr = GuestState->Rcx;

    if (!VcpuValidateMsr(Msr))
    {
        VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
        return VMM_EVENT_INTERRUPT;
    }

    LARGE_INTEGER MsrValue = {
        .QuadPart = __readmsr(Msr)
    };

    GuestState->Rdx = MsrValue.HighPart;
    GuestState->Rax = MsrValue.LowPart;

    return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VcpuHandleXSETBV(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
{
    return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VcpuHandleExternalInterruptNmi(
    _Inout_ PVCPU Vcpu,
    _Inout_ PGUEST_STATE GuestState
)
{
    return VMM_EVENT_CONTINUE;
}
