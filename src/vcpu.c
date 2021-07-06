#include "arch/segment.h"
#include "arch/msr.h"
#include "vcpu.h"
#include "cpuid.h"
#include "vmm.h"

EXTERN_C
VOID
__vmexit_entry(VOID);

EXTERN_C
VOID
__sgdt(PX86_SYSTEM_DESCRIPTOR);

EXTERN_C
UINT64
__segmentar(X86_SEGMENT_SELECTOR);

EXTERN_C
UINT16
__readldt(VOID);

EXTERN_C
UINT16
__readtr(VOID);

EXTERN_C
UINT16
__readcs(VOID);

EXTERN_C
UINT16
__readss(VOID);

EXTERN_C
UINT16
__readds(VOID);

EXTERN_C
UINT16
__reades(VOID);

EXTERN_C
UINT16
__readfs(VOID);

EXTERN_C
UINT16
__readgs(VOID);

VMEXIT_HANDLER VcpuUnknownExitReason;
VMEXIT_HANDLER VcpuHandleCpuid;

static VMEXIT_HANDLER* sExitHandlers[] = {
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

UINT64
SegmentBaseAddress(
	_In_ X86_SEGMENT_SELECTOR Selector
);

UINT32
SegmentAr(
	_In_ X86_SEGMENT_SELECTOR Selector
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

	Vcpu->MsrBitmapPhysical = ImpGetPhysicalAddress(Vcpu->MsrBitmap);
	
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
	__cpu_restore_state(&VcpuGetStack()->Cache.Vcpu->LaunchState);
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

	// TODO: Define exception bitmap later in vmx.h
	VmxWrite(CONTROL_EXCEPTION_BITMAP, 0);

	// All page faults should cause VM-exits
	VmxWrite(CONTROL_PAGE_FAULT_ERROR_CODE_MASK, 0);
	VmxWrite(CONTROL_PAGE_FAULT_ERROR_CODE_MATCH, 0);

	VmxWrite(CONTROL_MSR_BITMAP_ADDRESS, Vcpu->MsrBitmapPhysical);

	// No nested virtualisation, set invalid VMCS link pointer
	VmxWrite(GUEST_VMCS_LINK_POINTER, ~0ULL);

	VmxWrite(CONTROL_CR0_READ_SHADOW, __readcr0());
	VmxWrite(CONTROL_CR0_GUEST_MASK, ~(__readmsr(IA32_VMX_CR0_FIXED0) ^ __readmsr(IA32_VMX_CR0_FIXED1)));
	VmxWrite(GUEST_CR0, __readcr0());
	VmxWrite(HOST_CR0, __readcr0());

	VmxWrite(CONTROL_CR4_READ_SHADOW, __readcr4());
	VmxWrite(CONTROL_CR4_GUEST_MASK, ~(__readmsr(IA32_VMX_CR4_FIXED0) ^ __readmsr(IA32_VMX_CR4_FIXED1)));
	VmxWrite(GUEST_CR4, __readcr4());
	VmxWrite(HOST_CR4, __readcr4());

	VmxWrite(GUEST_CR3, __readcr3());
	VmxWrite(HOST_CR3, Vcpu->Vmm->MmSupport->HostDirectoryPhysical);

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
	VmxWrite(GUEST_IDTR_LIMIT, Gdtr.Limit);

	VmxWrite(GUEST_GDTR_BASE, Gdtr.BaseAddress);
	VmxWrite(HOST_GDTR_BASE, Gdtr.BaseAddress);

	VmxWrite(GUEST_IDTR_BASE, Idtr.BaseAddress);
	VmxWrite(HOST_IDTR_BASE, Vcpu->Vmm->HostInterruptDescriptor.BaseAddress);

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
	VmxWrite(HOST_FS_SELECTOR, Segment.Value& HOST_SEGMENT_SELECTOR_MASK);

	Segment.Value = __readgs();

	VmxWrite(GUEST_GS_SELECTOR, Segment.Value);
	VmxWrite(GUEST_GS_LIMIT, __segmentlimit(Segment.Value));
	VmxWrite(GUEST_GS_ACCESS_RIGHTS, SegmentAr(Segment));
	VmxWrite(GUEST_GS_BASE, __readmsr(IA32_GS_BASE));
	VmxWrite(HOST_GS_BASE, __readmsr(IA32_GS_BASE));
	VmxWrite(HOST_GS_SELECTOR, Segment.Value& HOST_SEGMENT_SELECTOR_MASK);

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

	__cpu_save_state(&Vcpu->LaunchState);

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
		InterlockedExchange(&Params->FaultyCoreId, CpuId);

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
--*/
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

	VMM_EVENT_STATUS Status = sExitHandlers[Vcpu->Vmx.ExitReason.BasicExitReason](Vcpu, GuestState);

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
	X86_CPUID_ARGS CpuidArgs = {
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

PX86_SEGMENT_DESCRIPTOR
LookupSegmentDescriptor(
	_In_ X86_SEGMENT_SELECTOR Selector
)
/*++
Routine Description:
	Looks up a segment descriptor in the GDT using a provided selector
 */
{
	X86_PSEUDO_DESCRIPTOR Gdtr;
	__sgdt(&Gdtr);

	return &((PX86_SEGMENT_DESCRIPTOR)Gdtr.BaseAddress)[Selector.Index];
}

UINT64
SegmentBaseAddress(
	_In_ X86_SEGMENT_SELECTOR Selector
)
/*++
Routine Description:
	Calculates the base address of a segment using a segment selector
--*/
{
	UINT64 Address = 0;

	// The LDT is unuseable on Windows 10, and the first entry of the GDT isn't used
	if (Selector.Table != SEGMENT_SELECTOR_TABLE_GDT || Selector.Index == 0)
		return 0;

	PX86_SEGMENT_DESCRIPTOR Segment = LookupSegmentDescriptor(Selector);

	if (Segment == NULL)
	{
		ImpDebugPrint("Invalid segment selector '%i'...\n", Selector.Value);
		return 0;
	}

	Address = ((UINT64)Segment->BaseHigh << 24) | 
				((UINT64)Segment->BaseMiddle << 16) | 
				((UINT64)Segment->BaseLow & 0xFFFF);

	if (Segment->System == 0)
	{
		PX86_SYSTEM_DESCRIPTOR SystemSegment = (PX86_SYSTEM_DESCRIPTOR)Segment;
		Address |= (UINT64)SystemSegment->BaseUpper << 32;
	}
	
	return Address;
}

UINT32
SegmentAr(
	_In_ X86_SEGMENT_SELECTOR Selector
)
/*++
Routine Description:
	Gets the access rights of a segment
 */
{
	PX86_SEGMENT_DESCRIPTOR Segment = LookupSegmentDescriptor(Selector);

	if (Segment == NULL)
	{
		ImpDebugPrint("Invalid segment selector '%i'...\n", Selector.Value);
		return 0;
	}
	
	X86_SEGMENT_ACCESS_RIGHTS SegmentAccessRights = {
		.Value = (UINT32)(__segmentar(Selector) >> 8)
	};

	SegmentAccessRights.Unusable = !Segment->Present;
	SegmentAccessRights.Reserved1 = 0;
	SegmentAccessRights.Reserved2 = 0;
	
	return SegmentAccessRights.Value;
}
