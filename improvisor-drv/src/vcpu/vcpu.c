#include <improvisor.h>
#include <arch/memory.h>
#include <arch/cpuid.h>
#include <arch/flags.h>
#include <arch/msr.h>
#include <arch/cpu.h>
#include <arch/cr.h>
#include <vcpu/interrupts.h>
#include <vcpu/vmcall.h>
#include <vcpu/vmexit.h>
#include <vcpu/vcpu.h>
#include <detour.h>
#include <intrin.h>
#include <macro.h>
#include <vmm.h>

// Intel Virtualisation Notes:
//
// Handling exception virtualisation correctly:
// 
// 27.1 ARCHITECTURAL STATE BEFORE A VM EXIT:
// 
// - If an event causes a VM exit directly, it does not update architectural state as it would have if it had it not caused the VM exit :
// - If an event causes a VM exit indirectly, the event does update architectural state:
// 
// (An exception causes a VM exit directly if the bit corresponding to that exception is set in the exception bitmap, etc.)
// (An exception, NMI, external interrupt, or software interrupt causes a VM exit indirectly if it does not do so directly but delivery of the event causes a fault-like VM-exit)
//
// In the case that event delivery causes a fault-like VM-exit, the IDT-vectoring information should be checked.
// 
// For information where this should be checked, consult 27.2.4 Information for VM Exits During Event Delivery
// 
// 
// NMI Unblocking: (ONLY CONTROLLED BY INTERRUPTIBILITY STATE IF NMI EXITING IS OFF)
// NMI Unblocking occurs during execution of IRET, if a VM-exit due to a fault occurs, we need to manually clear this bit\
// in the guest interruptibility state
// FACT CHECK: If an VM-exit due to a fault while an NMI was being delivered, NMI unblocking is cleared
// 


//#define IMPV_USE_WINDOWS_CPU_STRUCTURES

// Amount of entries in the MTF event stack
#define MTF_EVENT_MAX_COUNT 16

#define CREATE_MSR_ENTRY(Msr) \
	{Msr, 0UL, 0ULL}

VMM_RDATA 
DECLSPEC_ALIGN(16)
static const VMX_MSR_ENTRY sVmExitMsrStore[] = {
	CREATE_MSR_ENTRY(IA32_TIME_STAMP_COUNTER)
};

#if 0 // Not used yet
VMM_RDATA 
DECLSPEC_ALIGN(16)
static const VMX_MSR_ENTRY sVmEntryMsrLoad[] = {
};
#endif

#undef CREATE_MSR_ENTRY

VMM_API
PVCPU
VcpuGetActiveVcpu(VOID)
/*++
Routine Description:
	Gets the current VCPU for this core
--*/
{
	return __current_vcpu();
}


VSC_API
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
	// Setup the self reference member for getting the VCPU from FS
	Vcpu->Self = Vcpu;

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
	
	// VCPU stack is registered as hidden even though it is used after launch as VMM will not have hidden it yet
	Vcpu->Stack = (PVCPU_STACK)ImpAllocateHostNpPool(sizeof(VCPU_STACK));
	if (Vcpu->Stack == NULL)
	{
		ImpDebugPrint("Failed to allocate a host stack for VCPU #%d...\n", Id);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// Setup VCPU reference in the stack so the guest can return to execution after VM-launch
	Vcpu->Stack->Cache.Vcpu = Vcpu;

	// TODO: Allocate NMI and other interrupt stacks here, set Vcpu->Tss->Rsp0 to interrupt stack

	Vcpu->MtfStackHead = (PMTF_EVENT_ENTRY)ImpAllocateHostNpPool(sizeof(MTF_EVENT_ENTRY) * MTF_EVENT_MAX_COUNT);
	if (Vcpu->MtfStackHead == NULL)
	{
		ImpDebugPrint("Failed to allocate a MTF stack for VCPU #%d...\n", Id);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	for (SIZE_T i = 0; i < MTF_EVENT_MAX_COUNT; i++)
	{
		PMTF_EVENT_ENTRY CurrEvent = Vcpu->MtfStackHead + i;

		CurrEvent->Links.Flink = i < MTF_EVENT_MAX_COUNT - 1	? &(CurrEvent + 1)->Links : NULL;
		CurrEvent->Links.Blink = i > 0							? &(CurrEvent - 1)->Links : NULL;
	}

	// Setup VCPU host GDT and IDT
	if (!NT_SUCCESS(VcpuPrepareHostGDT(Vcpu)))
	{
		ImpDebugPrint("Failed to allocate GDT for VCPU #%d...\n", Id);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (!NT_SUCCESS(VcpuPrepareHostIDT(Vcpu)))
	{
		ImpDebugPrint("Failed to allocate IDT for VCPU #%d...\n", Id);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// TODO: Temp fix until we are using VMM created host CR3
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
#ifndef _DEBUG
	// Currently can't properly emulate CR writes without running under the custom page tables
	Vcpu->Cr0ShadowableBits = __readcr0();
	Vcpu->Cr4ShadowableBits = __readcr4();
#else
	Vcpu->Cr0ShadowableBits = ~(__readmsr(IA32_VMX_CR0_FIXED0) ^ __readmsr(IA32_VMX_CR0_FIXED1));
	Vcpu->Cr4ShadowableBits = ~(__readmsr(IA32_VMX_CR4_FIXED0) ^ __readmsr(IA32_VMX_CR4_FIXED1));
#endif

	// Make sure the VM enters in IA32e
	VcpuSetControl(Vcpu, VMX_CTL_HOST_ADDRESS_SPACE_SIZE, TRUE);
	VcpuSetControl(Vcpu, VMX_CTL_GUEST_ADDRESS_SPACE_SIZE, TRUE);

	VcpuSetControl(Vcpu, VMX_CTL_USE_MSR_BITMAPS, TRUE);
	VcpuSetControl(Vcpu, VMX_CTL_SECONDARY_CTLS_ACTIVE, TRUE);

	VcpuSetControl(Vcpu, VMX_CTL_ENABLE_RDTSCP, TRUE);
	VcpuSetControl(Vcpu, VMX_CTL_ENABLE_XSAVES_XRSTORS, TRUE);
	VcpuSetControl(Vcpu, VMX_CTL_ENABLE_INVPCID, TRUE);
	VcpuSetControl(Vcpu, VMX_CTL_ENABLE_EPT, TRUE);
	VcpuSetControl(Vcpu, VMX_CTL_ENABLE_VPID, TRUE);

	// Save GUEST_EFER on VM-exit
	VcpuSetControl(Vcpu, VMX_CTL_SAVE_EFER_ON_EXIT, TRUE);
	// Load HOST_EFER on VM-exit
	VcpuSetControl(Vcpu, VMX_CTL_LOAD_EFER_ON_EXIT, TRUE);

	// Temporarily disable CR3 exiting
	VcpuSetControl(Vcpu, VMX_CTL_CR3_LOAD_EXITING, FALSE);
	VcpuSetControl(Vcpu, VMX_CTL_CR3_STORE_EXITING, FALSE);

	VTscInitialise(&Vcpu->Tsc);

	return STATUS_SUCCESS;
}

VSC_API
VOID
VcpuCacheCpuFeatures(
	_Inout_ PVCPU Vcpu
)
/*++
Routine Description:
	Caches all relevant CPU feature information from CPUID flags and relevant MSR's
--*/
{
	return;
}

VOID
VcpuDestroy(
	_Inout_ PVCPU Vcpu
)
/*++
Routine Description:
	Frees all resources allocated for a VCPU, for this to be called VCPU and VMX initialisation had to have succeeded, therefore nothing is NULL
--*/
{
	// TODO: Finish this 
}

VSC_API
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

VSC_API
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

	EPT_POINTER EptPointer = {
		.MemoryType = EPT_MEMORY_WRITEBACK,
		.PageWalkLength = 3,
		.PML4PageFrameNumber = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(Vcpu->Vmm->Ept.Pml4))
	};

	VmxWrite(CONTROL_EPT_POINTER, EptPointer.Value);

	// Set the VPID identifier to the current processor number.
	VmxWrite(CONTROL_VIRTUAL_PROCESSOR_ID, 1);

	EXCEPTION_BITMAP ExceptionBitmap = {
		.BreakpointException = TRUE
	};

	VmxWrite(CONTROL_EXCEPTION_BITMAP, ExceptionBitmap.Value);

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

#ifndef IMPV_USE_WINDOWS_CPU_STRUCTURES
	VmxWrite(HOST_CR3, Vcpu->Vmm->Mm.Cr3.Value);
#else
	VmxWrite(HOST_CR3, Vcpu->SystemDirectoryBase);
#endif

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
	VmxWrite(GUEST_IDTR_BASE, Idtr.BaseAddress);

	VmxWrite(HOST_GDTR_BASE, Vcpu->Gdt);
	VmxWrite(HOST_IDTR_BASE, Vcpu->Idt);

#ifndef IMPV_USE_WINDOWS_CPU_STRUCTURES
	// Custom TSS is only used in RELEASE build
	VmxWrite(HOST_TR_BASE, Vcpu->Tss);
#endif

	// Store the current VCPU in the FS segment
	VmxWrite(HOST_FS_BASE, Vcpu);

#ifndef IMPV_USE_WINDOWS_CPU_STRUCTURES
	VmxWrite(HOST_GS_BASE, 0); 
#else
	VmxWrite(HOST_GS_BASE, __readmsr(IA32_GS_BASE));
#endif

#ifndef IMPV_USE_WINDOWS_CPU_STRUCTURES
	// Custom GDT, CS, and TR selectors are only used in RELEASE mode
	VmxWrite(HOST_CS_SELECTOR, gVmmCsSelector.Value);
	VmxWrite(HOST_TR_SELECTOR, gVmmTssSelector.Value);
#else
	X86_SEGMENT_SELECTOR Cs = {
		.Value = __readcs()
	};

	X86_SEGMENT_SELECTOR Tr = {
		.Value = __readtr()
	};

	VmxWrite(HOST_CS_SELECTOR, Cs.Value);
	VmxWrite(HOST_SS_SELECTOR, 0);
	VmxWrite(HOST_DS_SELECTOR, 0);
	VmxWrite(HOST_ES_SELECTOR, 0);
	VmxWrite(HOST_FS_SELECTOR, 0);
	VmxWrite(HOST_GS_SELECTOR, 0);
	VmxWrite(HOST_TR_SELECTOR, Tr.Value);
#endif

	X86_SEGMENT_SELECTOR Segment = {0};

	Segment.Value = __readcs();

	VmxWrite(GUEST_CS_SELECTOR, Segment.Value);
	VmxWrite(GUEST_CS_LIMIT, __segmentlimit(Segment.Value));
	VmxWrite(GUEST_CS_ACCESS_RIGHTS, SegmentAr(Segment));
	VmxWrite(GUEST_CS_BASE, SegmentBaseAddress(Segment));

	Segment.Value = __readss();

	VmxWrite(GUEST_SS_SELECTOR, Segment.Value);
	VmxWrite(GUEST_SS_LIMIT, __segmentlimit(Segment.Value));
	VmxWrite(GUEST_SS_ACCESS_RIGHTS, SegmentAr(Segment));
	VmxWrite(GUEST_SS_BASE, SegmentBaseAddress(Segment));

	Segment.Value = __readds();

	VmxWrite(GUEST_DS_SELECTOR, Segment.Value);
	VmxWrite(GUEST_DS_LIMIT, __segmentlimit(Segment.Value));
	VmxWrite(GUEST_DS_ACCESS_RIGHTS, SegmentAr(Segment));
	VmxWrite(GUEST_DS_BASE, SegmentBaseAddress(Segment));

	Segment.Value = __reades();

	VmxWrite(GUEST_ES_SELECTOR, Segment.Value);
	VmxWrite(GUEST_ES_LIMIT, __segmentlimit(Segment.Value));
	VmxWrite(GUEST_ES_ACCESS_RIGHTS, SegmentAr(Segment));
	VmxWrite(GUEST_ES_BASE, SegmentBaseAddress(Segment));

	Segment.Value = __readfs();

	VmxWrite(GUEST_FS_SELECTOR, Segment.Value);
	VmxWrite(GUEST_FS_LIMIT, __segmentlimit(Segment.Value));
	VmxWrite(GUEST_FS_ACCESS_RIGHTS, SegmentAr(Segment));
	VmxWrite(GUEST_FS_BASE, SegmentBaseAddress(Segment));

	Segment.Value = __readgs();

	VmxWrite(GUEST_GS_SELECTOR, Segment.Value);
	VmxWrite(GUEST_GS_LIMIT, __segmentlimit(Segment.Value));
	VmxWrite(GUEST_GS_ACCESS_RIGHTS, SegmentAr(Segment));
	VmxWrite(GUEST_GS_BASE, __readmsr(IA32_GS_BASE));

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

#ifdef IMPV_USE_WINDOWS_CPU_STRUCTURES
	VmxWrite(HOST_TR_BASE, SegmentBaseAddress(Segment));
#endif

	// Setup the HOST_EFER as the current EFER
	VmxWrite(HOST_EFER, __readmsr(IA32_EFER));

	// Setup MSR load & store regions
	VmxWrite(CONTROL_VMEXIT_MSR_STORE_COUNT, sizeof(sVmExitMsrStore) / sizeof(*sVmExitMsrStore));
	// VmxWrite(CONTROL_VMENTRY_MSR_LOAD_COUNT, sizeof(sVmEntryMsrLoad) / sizeof(*sVmEntryMsrLoad));

	VmxWrite(CONTROL_VMEXIT_MSR_STORE_ADDRESS, ImpGetPhysicalAddress(sVmExitMsrStore));
	// VmxWrite(CONTROL_VMENTRY_MSR_LOAD_ADDRESS, ImpGetPhysicalAddress(sVmEntryMsrLoad));

	VcpuCommitVmxState(Vcpu);
	
	VmxWrite(HOST_RSP, (UINT64)(Vcpu->Stack->Data + 0x6000 - 16));
	VmxWrite(GUEST_RSP, (UINT64)(Vcpu->Stack->Data + 0x6000 - 16));

	VmxWrite(HOST_RIP, (UINT64)__vmexit_entry);
	VmxWrite(GUEST_RIP, (UINT64)VcpuLaunch);

	Vcpu->Mode = VCPU_MODE_LAUNCHED;
	
	__vmx_vmlaunch();

	return STATUS_APP_INIT_FAILURE;
}

VSC_API
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
	// TODO: Fix issue with VMCALL happening during IPIs due to incorrect IRQL after returning from IPI function, might be needed so work it out.

	ULONG CpuId = KeGetCurrentProcessorNumber();
	PVCPU Vcpu = &Params->VmmContext->VcpuTable[CpuId];
	
	Vcpu->Vmm = Params->VmmContext;

	__cpu_save_state(&Vcpu->LaunchState);

	// Control flow is restored here upon successful virtualisation of the CPU
	if (Vcpu->Mode == VCPU_MODE_LAUNCHED)
	{
		// Check if this CPU core is currently virtualised.
		if (!__vcpu_is_virtualised())
		{
			ImpDebugPrint("VCPU #%d's mode indicates launched but CPUID check failed...\n", Vcpu->Id);
			return;
		}

		InterlockedIncrement(&Params->ActiveVcpuCount);
		ImpDebugPrint("VCPU #%d is now running...\n", Vcpu->Id);

		if (!NT_SUCCESS(VcpuPostSpawnInitialisation(Vcpu)))
		{
			// TODO: Panic shutdown here
			ImpDebugPrint("VCPU #%d failed post spawn initialisation...\n", Vcpu->Id);
			return;
		}

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

	// TODO: Modify `VcpuSpawn` to not use `__cpu_restore_state` and `__cpu_save_state`?
	// I want to use these for IRETQ to user/kernel after failure when calling VcpuShutdownVmx

	// TODO: Panic here, VcpuSpawn claimed it succeeded but control-flow execution wasn't restored where it was saved
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
	// Check if this CPU core is currently virtualised
	if (!__vcpu_is_virtualised())
		return;

	PVCPU Vcpu = NULL;
	// Attempt to shutdownt he VCPU
	VmShutdownVcpu(&Vcpu);
	// The result of the hypercall can't be used because changes made to the guest state are 
	// ignored if the VCPU panics, check if the VCPU pointer is present instead.
	if (Vcpu == NULL)
	{
		Params->Status = STATUS_ABANDONED;
		return;
	}

	ImpDebugPrint("VCPU #%i (%p) shutdown.\n", Vcpu->Id, Vcpu);

	// Free all VCPU related resources 
	// TODO: Only do this if the hypervisor wasn't shut down in a controlled manner
	VcpuDestroy(Vcpu);
	
	// The VMM context and the VCPU table are allocated as host memory, but after VmShutdownVcpu, all EPT will be disabled
	// and any host memory will be visible again
	Params->VmmContext = Vcpu->Vmm;
}

DECLSPEC_NORETURN
VMM_API
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
	// Cache the guest CR3, we will switch to it after VMX has been shutdown and CPU structures
	// have been restored
	UINT64 GuestCR3 = VmxRead(GUEST_CR3);
	// Switch to the system's address space for now while we restore CPU structures
	__writecr3(Vcpu->SystemDirectoryBase);

	__disable();

	__writecr0(VmxRead(GUEST_CR0));
	__writecr4(VmxRead(GUEST_CR4));
	__writedr(7, VmxRead(GUEST_DR7));

	__writemsr(IA32_DEBUGCTL, VmxRead(GUEST_DEBUGCTL));
	__writemsr(GUEST_SYSENTER_ESP, VmxRead(IA32_SYSENTER_ESP));
	__writemsr(GUEST_SYSENTER_EIP, VmxRead(IA32_SYSENTER_EIP));
	__writemsr(GUEST_SYSENTER_CS, VmxRead(IA32_SYSENTER_CS));

	// Restore the GDT and IDT
	X86_PSEUDO_DESCRIPTOR Gdtr = {
		.BaseAddress = VmxRead(GUEST_GDTR_BASE),
		.Limit = (UINT16)VmxRead(GUEST_GDTR_LIMIT)
	};

	__lgdt(&Gdtr);	

	X86_PSEUDO_DESCRIPTOR Idtr = {
		.BaseAddress = VmxRead(GUEST_IDTR_BASE),
		.Limit = (UINT16)VmxRead(GUEST_IDTR_LIMIT)
	};	

	__lidt(&Idtr);

	__writefs((VmxRead(GUEST_FS_ACCESS_RIGHTS) & (1 << 16)) ? 0 : (UINT16)VmxRead(GUEST_FS_SELECTOR));
	__writemsr(IA32_FS_BASE, VmxRead(GUEST_FS_BASE));
	__writegs((VmxRead(GUEST_GS_ACCESS_RIGHTS) & (1 << 16)) ? 0 : (UINT16)VmxRead(GUEST_GS_SELECTOR));
	__writemsr(IA32_GS_BASE, VmxRead(GUEST_GS_BASE));

	X86_SEGMENT_SELECTOR Selector = { 0 };
	Selector.Value = (UINT16)VmxRead(GUEST_DS_SELECTOR);
	__writeds(Selector.Value);
	Selector.Value = (UINT16)VmxRead(GUEST_ES_SELECTOR);
	__writees(Selector.Value);

	Selector.Value = (UINT16)VmxRead(GUEST_TR_SELECTOR);
	// Grab the segment selector for the TR segment and force it into a non-busy state
	PX86_SEGMENT_DESCRIPTOR TrSelector = LookupSegmentDescriptor(Selector);
	// The busy flag is determined through the `X86_SEGMENT_DESCRIPTOR::Type` field, it is always
	// either 0b1001 or 0b1011, indicating the non-busy and busy states respectively as bit 2 is used 
	// as the busy flag - have to do this in case we have exited from a 32-bit process
	TrSelector->Type = SEGMENT_TYPE_NATURAL_TSS_AVAILABLE;
	// Write the old TR selector
	__writetr(Selector.Value);

	Selector.Value = (UINT16)VmxRead(GUEST_LDTR_SELECTOR);
	__writeldt(Selector.Value);

	CpuState->Cs = (UINT16)VmxRead(GUEST_CS_SELECTOR);
	CpuState->Ss = (UINT16)VmxRead(GUEST_SS_SELECTOR);
	// Force interrupts to be enabled
	CpuState->RFlags = VmxRead(GUEST_RFLAGS) | RFLAGS_IF;
	CpuState->Rsp = VmxRead(GUEST_RSP);
	CpuState->Rip = VmxRead(GUEST_RIP);

	__vmx_off();

	// Finally, Switch back to the guest's CR3
	// TODO: Add KVAS support in case this is called due to the VCPU actually panicking 
	__writecr3(GuestCR3);

	__cpu_restore_state(CpuState);
}

VSC_API
NTSTATUS
VcpuPostSpawnInitialisation(
	_Inout_ PVCPU Vcpu
)
/*++
Routine Description:
	This function performs all post-VMLAUNCH initialisation that might be required, such as measuing VM-exit and VM-entry latencies
--*/
{
#if 0
	CHAR Log[512] = { 0 };

	HYPERCALL_RESULT Result = VmGetLogRecords(Log, 1);
	if (Result != HRESULT_SUCCESS)
		ImpDebugPrint("VmGetLogRecords returned %x...\n", Result);

	ImpDebugPrint("Log #1: %.\n", Log);
#endif

	//VTscEstimateVmExitLatency(&Vcpu->Tsc);
	//VTscEstimateVmEntryLatency(&Vcpu->Tsc);

	// TODO: Run VTSC tests and hook tests here

	// TODO: After post-spawn initialisation, Vcpu should be hidden from guest memory
	//       Call HYPERCALL_HIDE_HOST_RESOURCES

	return STATUS_SUCCESS;
}

VMM_API
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

VMM_API
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

VMM_API
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

VMM_API
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

VMM_API
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

VMM_API
BOOLEAN
VcpuIsGuest64Bit(
	_In_ PVCPU Vcpu
)
{
	IA32_EFER_MSR Efer = {
		.Value = VmxRead(GUEST_EFER)
	};

	return Efer.LongModeEnable && Efer.LongModeActive;
}

VMM_API
UCHAR
VcpuGetGuestCPL(
	_In_ PVCPU Vcpu
)
{
	X86_SEGMENT_ACCESS_RIGHTS CsAr = {
		.Value = VmxRead(GUEST_CS_ACCESS_RIGHTS)
	};

	return CsAr.Dpl;
}

VMM_API
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

	__debugbreak();
	ImpDebugPrint("Unknown VM-exit reason (%d) on VCPU #%d...\n", Vcpu->Vmx.ExitReason.BasicExitReason, Vcpu->Id);
	
	return VMM_EVENT_ABORT;
}

VMM_API
VOID
VcpuEnableTscSpoofing(
	_Inout_ PVCPU Vcpu
)
/*++
Routine Description:
	This function enables TSC spoofing for the next while
--*/
{
	Vcpu->Tsc.SpoofEnabled = TRUE;

	VcpuSetControl(Vcpu, VMX_CTL_SAVE_VMX_PREEMPTION_VALUE, TRUE);
	VcpuSetControl(Vcpu, VMX_CTL_VMX_PREEMPTION_TIMER, TRUE);
	VcpuSetControl(Vcpu, VMX_CTL_RDTSC_EXITING, TRUE);

	// Write the TSC watchdog quantum
	VmxWrite(GUEST_VMX_PREEMPTION_TIMER_VALUE, VTSC_WATCHDOG_QUANTUM + Vcpu->Tsc.VmEntryLatency);
}

VMM_API
UINT64
VcpuGetTscEventLatency(
	_In_ PVCPU Vcpu,
	_In_ TSC_EVENT_TYPE Type
)
{
	switch (Type) 
	{
	case TSC_EVENT_CPUID: return Vcpu->Tsc.CpuidLatency;
	case TSC_EVENT_RDTSC: return Vcpu->Tsc.RdtscLatency;
	case TSC_EVENT_RDTSCP: return Vcpu->Tsc.RdtscpLatency;
	default: return 0;
	}
}

VMM_API
VOID
VcpuUpdateLastTscEventEntry(
	_Inout_ PVCPU Vcpu,
	_In_ TSC_EVENT_TYPE Type
)
{
	// TODO: Also spoof TSC values during EPT violations, some anti-cheats/malware monitor those 
	//       Essentially everything that exclusively causes a VM-exit should have this emulated
	
	// Try find a previous event to base our value off
	PTSC_EVENT_ENTRY PrevEvent = &Vcpu->Tsc.PrevEvent; 
	if (PrevEvent->Valid)
	{
		// Calculate the time since the last event during this thread
		UINT64 ElapsedTime = VTSC_WATCHDOG_QUANTUM - VmxRead(GUEST_VMX_PREEMPTION_TIMER_VALUE);

		// Base the new timestamp off the last TSC event for this thread
		UINT64 Timestamp = PrevEvent->Timestamp + PrevEvent->Latency + ElapsedTime;

		PrevEvent->Type = Type;
		PrevEvent->Timestamp = Timestamp;
		PrevEvent->Latency = VcpuGetTscEventLatency(Vcpu, Type);

		ImpLog("[%02X] PrevEvent->Valid: TSC = %u + %u + %u\n", Vcpu->Id, PrevEvent->Timestamp, PrevEvent->Latency, ElapsedTime);
	} 
	else
	{
		// No preceeding records, approximate the value of the TSC before exiting using the stored MSR
		UINT64 Timestamp = 
			VcpuGetVmExitStoreValue(IA32_TIME_STAMP_COUNTER) - Vcpu->Tsc.VmExitLatency;

		PrevEvent->Valid = TRUE;
		PrevEvent->Type = Type;
		PrevEvent->Timestamp = Timestamp;
		PrevEvent->Latency = VcpuGetTscEventLatency(Vcpu, Type);

		ImpLog("[%02X] !PrevEvent->Valid: TSC = %u + %u\n", Vcpu->Id, Timestamp, PrevEvent->Latency);
	}
}
