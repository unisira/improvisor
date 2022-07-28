#include "arch/interrupt.h"
#include "arch/segment.h"
#include "arch/memory.h"
#include "arch/cpuid.h"
#include "arch/msr.h"
#include "arch/cr.h" 
#include "util/macro.h"
#include "util/spinlock.h"
#include "section.h"
#include "intrin.h"
#include "vmcall.h"
#include "detour.h"
#include "vcpu.h"
#include "vmm.h"

#include <intrin.h>

//#define IMPV_USE_WINDOWS_CPU_STRUCTURES

#define VCPU_IST_STACK_SIZE 0x2000
#define VCPU_NMI_IST_STACK 3
// The value of the VMX preemption timer used as a watchdog for TSC virtualisation
#define VTSC_WATCHDOG_QUANTUM 4000
// Amount of entries in the MTF event stack
#define MTF_EVENT_MAX_COUNT 16

EXTERN_C_START
#define VMM_INTR_GATE(Index)    \
	VOID                        \
	__vmm_intr_gate_##Index()   \

VMM_INTR_GATE(0);
VMM_INTR_GATE(1);
VMM_INTR_GATE(2);
VMM_INTR_GATE(3);
VMM_INTR_GATE(4);
VMM_INTR_GATE(5);
VMM_INTR_GATE(6);
VMM_INTR_GATE(7);
VMM_INTR_GATE(8);
VMM_INTR_GATE(9);
VMM_INTR_GATE(10);
VMM_INTR_GATE(11);
VMM_INTR_GATE(12);
VMM_INTR_GATE(13);
VMM_INTR_GATE(14);
VMM_INTR_GATE(15);
VMM_INTR_GATE(16);
VMM_INTR_GATE(17);
VMM_INTR_GATE(18);
VMM_INTR_GATE(19);
VMM_INTR_GATE(20);
VMM_INTR_GATE(21);
VMM_INTR_GATE(22);
VMM_INTR_GATE(23);
VMM_INTR_GATE(24);
VMM_INTR_GATE(25);
VMM_INTR_GATE(26);
VMM_INTR_GATE(27);
VMM_INTR_GATE(28);
VMM_INTR_GATE(29);
VMM_INTR_GATE(30);
VMM_INTR_GATE(31);
VMM_INTR_GATE(32);

VMM_INTR_GATE(unk);

#undef VMM_INTR_GATE
EXTERN_C_END

// TODO: Figure out how to make this allocated in 
EXTERN_C
VOID
__vmexit_entry(VOID);

EXTERN_C
PVCPU
__current_vcpu(VOID);

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
VMEXIT_HANDLER VcpuHandlePreemptionTimerExpire;
VMEXIT_HANDLER VcpuHandleExceptionNmi;
VMEXIT_HANDLER VcpuHandleExternalInterrupt;
VMEXIT_HANDLER VcpuHandleNmiWindow;
VMEXIT_HANDLER VcpuHandleInterruptWindow;
VMEXIT_HANDLER VcpuHandleEptViolation;
VMEXIT_HANDLER VcpuHandleEptMisconfig;
VMEXIT_HANDLER VcpuHandleMTFExit;
VMEXIT_HANDLER VcpuHandleWbinvd;
VMEXIT_HANDLER VcpuHandleXsetbv;
VMEXIT_HANDLER VcpuHandleInvlpg;
VMEXIT_HANDLER VcpuHandleInvd;

VMM_RDATA static const VMEXIT_HANDLER* sExitHandlers[] = {
	VcpuHandleExceptionNmi,         // Exception or non-maskable interrupt (NMI)
	VcpuHandleExternalInterrupt, 	// External interrupt
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
	VcpuHandleInvd, 				// INVD
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
	VcpuHandleEptViolation, 		// EPT violation
	VcpuHandleEptMisconfig, 		// EPT misconfiguration
	VcpuHandleVmxInstruction, 		// INVEPT
	VcpuHandleRdtscp,     			// RDTSCP
	VcpuHandleTimerExpire,          // VMX-preemption timer expired
	VcpuHandleVmxInstruction, 		// INVVPID
	VcpuHandleWbinvd, 				// WBINVD or WBNOINVD
	VcpuHandleXsetbv, 			    // XSETBV
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

VMM_RDATA static const PVOID sExceptionRoutines[] = {
	__vmm_intr_gate_0,
	__vmm_intr_gate_1,
	__vmm_intr_gate_2,
	__vmm_intr_gate_3,
	__vmm_intr_gate_4,
	__vmm_intr_gate_5,
	__vmm_intr_gate_6,
	__vmm_intr_gate_7,
	__vmm_intr_gate_8,
	__vmm_intr_gate_9,
	__vmm_intr_gate_10,
	__vmm_intr_gate_11,
	__vmm_intr_gate_12,
	__vmm_intr_gate_13,
	__vmm_intr_gate_14,
	__vmm_intr_gate_15,
	__vmm_intr_gate_16,
	__vmm_intr_gate_17,
	__vmm_intr_gate_18,
	__vmm_intr_gate_19,
	__vmm_intr_gate_20,
	__vmm_intr_gate_21,
	__vmm_intr_gate_22,
	__vmm_intr_gate_23,
	__vmm_intr_gate_24,
	__vmm_intr_gate_25,
	__vmm_intr_gate_26,
	__vmm_intr_gate_27,
	__vmm_intr_gate_28,
	__vmm_intr_gate_29,
	__vmm_intr_gate_30,
	__vmm_intr_gate_31
};

#define CREATE_MSR_ENTRY(Msr) \
	{Msr, 0UL, 0ULL}

VMM_RDATA 
DECLSPEC_ALIGN(16)
static const VMX_MSR_ENTRY sVmExitMsrStore[] = {
	CREATE_MSR_ENTRY(IA32_TIME_STAMP_COUNTER)
};

/*
VMM_RDATA 
DECLSPEC_ALIGN(16)
static const VMX_MSR_ENTRY sVmEntryMsrLoad[] = {
};
*/

#undef CREATE_MSR_ENTRY

VMM_API
VOID
VcpuSetControl(
	_Inout_ PVCPU Vcpu,
	_In_ VMX_CONTROL Control,
	_In_ BOOLEAN State
);

VMM_API
BOOLEAN
VcpuIsControlSupported(
	_Inout_ PVCPU Vcpu,
	_In_ VMX_CONTROL Control
);

VMM_API
VOID
VcpuCommitVmxState(
	_Inout_ PVCPU Vcpu
);

VMM_API
VOID
VcpuToggleExitOnMsr(
	_Inout_ PVCPU Vcpu,
	_In_ UINT32 Msr,
	_In_ MSR_ACCESS Access
);

VMM_API
BOOLEAN
VcpuHandleExit(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
);

VOID
VcpuDestroy(
	_Inout_ PVCPU Vcpu
);

VSC_API
NTSTATUS
VcpuPostSpawnInitialisation(
	_Inout_ PVCPU Vcpu
);

VMM_API
UINT64
VcpuGetVmExitStoreValue(
	_In_ UINT32 Index
);

VMM_API
BOOLEAN
VcpuIs64Bit(
	_In_ PVCPU Vcpu
);

VMM_API
NTSTATUS
VcpuLoadPDPTRs(
	_In_ PVCPU Vcpu
);

NTSTATUS
VcpuPrepareHostGDT(
	_Inout_ PVCPU Vcpu
);


NTSTATUS
VcpuPrepareHostIDT(
	_Inout_ PVCPU Vcpu
);

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

	VTscInitialise(&Vcpu->TscInfo);

	return STATUS_SUCCESS;
}

VSC_API
NTSTATUS
VcpuPrepareHostGDT(
	_Inout_ PVCPU Vcpu
)
/*++
Routine Description:
	Sets up a GDT with CS and TSS selectors for the host
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	X86_PSEUDO_DESCRIPTOR Gdtr;
	__sgdt(&Gdtr);

#if 1
	ImpDebugPrint("[%02d] GDTR Base: 0x%llX, Limit: 0x%llX", Vcpu->Id, Gdtr.BaseAddress, Gdtr.Limit);

	for (SIZE_T i = 0; i < Gdtr.Limit / sizeof(X86_SEGMENT_DESCRIPTOR); i++)
	{
		PX86_SEGMENT_DESCRIPTOR Desc = &((PX86_SEGMENT_DESCRIPTOR)Gdtr.BaseAddress)[i];

		if (!Desc->Present)
			continue;

		const UINT64 Limit = ((UINT64)Desc->LimitHigh << 16) | (UINT64)Desc->LimitLow;

		UINT64 Address = (((UINT64)Desc->BaseHigh << 24) |
					((UINT64)Desc->BaseMiddle << 16) |
					((UINT64)Desc->BaseLow));

		if (Desc->System == 0)
		{
			PX86_SYSTEM_DESCRIPTOR SystemDesc = (PX86_SYSTEM_DESCRIPTOR)Desc;
			Address |= (UINT64)SystemDesc->BaseUpper << 32;
		}

		ImpDebugPrint("[%02d] Descriptor #%d:\n\tAddress: 0x%llX\n\tLimit: 0x%llX\n\tSystem: %d\n\tDPL: %d\n\tType: %d\n\tLong: %d\n\tDOS: %d\n\tGRAN: %d\n\tOS: %d\n",
			Vcpu->Id,
			i,
			Address,
			Limit,
			Desc->System,
			Desc->Dpl,
			Desc->Type,
			Desc->Long,
			Desc->DefaultOperationSize,
			Desc->Granularity,
			Desc->OsDefined);
	}
#endif

	PX86_SEGMENT_DESCRIPTOR Descriptors = ImpAllocateHostContiguousMemory(Gdtr.Limit + 1);
	if (Descriptors == NULL)
	{
		ImpDebugPrint("Failed to allocate GDT descriptors...\n");
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto panic;
	}

#ifndef IMPV_USE_WINDOWS_CPU_STRUCTURES
	PX86_SEGMENT_DESCRIPTOR Cs = &Descriptors[gVmmCsSelector.Index];

	Cs->Type = CODE_DATA_TYPE_EXECUTE_READ_ACCESSED;
	Cs->System = TRUE;
	Cs->Dpl = 0;
	Cs->Present = TRUE;
	Cs->Long = TRUE;
	Cs->DefaultOperationSize = 0;
	Cs->Granularity = 0;

	PX86_SYSTEM_DESCRIPTOR Tss = &Descriptors[gVmmTssSelector.Index];

	Tss->Type = SEGMENT_TYPE_NATURAL_TSS_BUSY;
	Tss->System = FALSE;
	Tss->Dpl = 0;
	Tss->Present = TRUE;
	Tss->Long = FALSE;
	Tss->DefaultOperationSize = 0;
	Tss->Granularity = 0;
	Tss->LimitLow = sizeof(X86_TASK_STATE_SEGMENT) - 1;
	Tss->LimitHigh = 0;

	Vcpu->Tss = ImpAllocateHostContiguousMemory(sizeof(X86_TASK_STATE_SEGMENT));
	if (Vcpu->Tss == NULL)
	{
		ImpDebugPrint("Failed to allocate TSS segment...\n");
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto panic;
	}

	PVOID InterruptStack = ImpAllocateHostContiguousMemory(VCPU_IST_STACK_SIZE);
	if (InterruptStack == NULL)
	{
		ImpDebugPrint("Failed to allocate interrupt stack...\n");
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto panic;
	}

	Vcpu->Tss->Ist1 = (UINT64)InterruptStack + VCPU_IST_STACK_SIZE - 0x10;

	const UINT64 Address = (UINT64)Vcpu->Tss;

	Tss->BaseLow = Address & 0xFFFF;
	Tss->BaseMiddle = (Address >> 16) & 0xFF;
	Tss->BaseHigh = (Address >> 24) & 0xFF;
	Tss->BaseUpper = (Address >> 32) & 0xFFFFFFFF;
#else
	RtlCopyMemory(Descriptors, Gdtr.BaseAddress, Gdtr.Limit + 1);
#endif

	Vcpu->Gdt = Descriptors;

panic:
	if (!NT_SUCCESS(Status))
	{
		if (Vcpu->Tss != NULL)
			ExFreePoolWithTag(Vcpu->Tss, POOL_TAG);
		if (Vcpu->Gdt != NULL)
			ExFreePoolWithTag(Vcpu->Gdt, POOL_TAG);
	}

	return Status;
}

VSC_API
VOID
VcpuInitialiseInterruptGate(
	PX86_INTERRUPT_TRAP_GATE Gates,
	UINT8 Vector,
	PVOID Handler,
	UINT8 IstEntry,
	UINT8 Dpl
)
{
	PX86_INTERRUPT_TRAP_GATE Gate = &Gates[Vector];

	Gate->SegmentSelector = gVmmCsSelector.Value;
	Gate->Type = SEGMENT_TYPE_NATURAL_INTERRUPT_GATE;
	Gate->Dpl = Dpl;
	Gate->InterruptStackTable = IstEntry;
	Gate->Present = TRUE;

	const UINT64 Address = (UINT64)Handler;
	Gate->OffsetLow = Address & 0xFFFF;
	Gate->OffsetMid = (Address >> 16) & 0xFFFF;
	Gate->OffsetHigh = (Address >> 32) & 0xFFFFFFFF;
}

EXTERN_C
VOID
__vmm_generic_intr_handler();

VSC_API
NTSTATUS
VcpuPrepareHostIDT(
	_Inout_ PVCPU Vcpu
)
/*++
Routine Description:
	Creates a host IDT pointing to VcpuHandleHostException
--*/
{
	X86_PSEUDO_DESCRIPTOR Idtr;
	__sidt(&Idtr);

#if 0
	ImpDebugPrint("[%02d] IDTR Base: 0x%llX, Limit: 0x%llX\n", Vcpu->Id, Idtr.BaseAddress, Idtr.Limit);

	for (SIZE_T i = 0; i < Idtr.Limit / sizeof(X86_INTERRUPT_TRAP_GATE); i++)
	{
		X86_INTERRUPT_TRAP_GATE Gate = ((PX86_INTERRUPT_TRAP_GATE)Idtr.BaseAddress)[i];

		if (!Gate.Present)
			continue;

		UINT64 Address = (((UINT64)Gate.OffsetHigh << 32) |
							((UINT64)Gate.OffsetMid << 16) |
							((UINT64)Gate.OffsetLow));

		ImpDebugPrint("[%02d] Gate #%d:\n\tAddress: 0x%llX\n\tSelector: %x\n\tDPL: %d\n\tType: %d\n\tIST: %d\n", 
			Vcpu->Id, 
			i, 
			Address,
			Gate.SegmentSelector,
			Gate.Dpl,
			Gate.Type,
			Gate.InterruptStackTable);
	}
#endif

	NTSTATUS Status = STATUS_SUCCESS;

	PX86_INTERRUPT_TRAP_GATE InterruptGates = ImpAllocateHostContiguousMemory(Idtr.Limit + 1);
	if (InterruptGates == NULL)
	{
		ImpDebugPrint("[%02d] Failed to allocate 256 interrupt gates for host IDT...\n", Vcpu->Id);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

#ifndef IMPV_USE_WINDOWS_CPU_STRUCTURES
	for (SIZE_T i = 0; i < 32; i++)
		VcpuInitialiseInterruptGate(InterruptGates, i, sExceptionRoutines[i], 1, 0);
#else
	RtlCopyMemory(InterruptGates, Idtr.BaseAddress, Idtr.Limit + 1);
#endif

	Vcpu->Idt = InterruptGates;

	// TODO: Enable interrupts during root mode and write a small debugger to walk the stack after an interrupt occurs
	// VcpuInitialiseInterruptGate(InterruptGates, 2, __vmm_intr_gate_2, 0, 0);

	return Status;
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
		.PML4PageFrameNumber = PAGE_FRAME_NUMBER(ImpGetPhysicalAddress(Vcpu->Vmm->EptInformation.SystemPml4))
	};

	VmxWrite(CONTROL_EPT_POINTER, EptPointer.Value);

	// Set the VPID identifier to the current processor number.
	VmxWrite(CONTROL_VIRTUAL_PROCESSOR_ID, 1);

	EXCEPTION_BITMAP ExceptionBitmap = {
		.Value = 0
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
	VmxWrite(HOST_CR3, Vcpu->Vmm->MmInformation.Cr3.Value);
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
	//VmxWrite(CONTROL_VMENTRY_MSR_LOAD_COUNT, sizeof(sVmEntryMsrLoad) / sizeof(*sVmEntryMsrLoad));

	VmxWrite(CONTROL_VMEXIT_MSR_STORE_ADDRESS, ImpGetPhysicalAddress(sVmExitMsrStore));
	//VmxWrite(CONTROL_VMENTRY_MSR_LOAD_ADDRESS, ImpGetPhysicalAddress(sVmEntryMsrLoad));

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

	//VTscEstimateVmExitLatency(&Vcpu->TscInfo);
	//VTscEstimateVmEntryLatency(&Vcpu->TscInfo);

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
VcpuIs64Bit(
	_In_ PVCPU Vcpu
)
{
	IA32_EFER_MSR Efer = {
		.Value = VmxRead(GUEST_EFER)
	};

	return Efer.LongModeEnable && Efer.LongModeActive;
}

VMM_API
VOID
VcpuTrapFrameToContext(
	_In_ PVCPU_TRAP_FRAME TrapFrame,
	_Out_ PVCPU_CONTEXT Context
)
/*++
Routine Description:
	Converts a VCPU trap frame into a succinct VPCU context state for stack walking
--*/
{
	Context->Rax = TrapFrame->Rax;
	Context->Rbx = TrapFrame->Rbx;
	Context->Rcx = TrapFrame->Rcx;
	Context->Rdx = TrapFrame->Rdx;
	Context->Rsi = TrapFrame->Rsi;
	Context->Rdi = TrapFrame->Rdi;
	Context->R8 = TrapFrame->R8;
	Context->R9 = TrapFrame->R9;
	Context->R10 = TrapFrame->R10;
	Context->R11 = TrapFrame->R11;
	Context->R12 = TrapFrame->R12;
	Context->R13 = TrapFrame->R13;
	Context->R14 = TrapFrame->R14;
	Context->R15 = TrapFrame->R15;
	Context->Rip = TrapFrame->Rip;
	Context->Rsp = TrapFrame->Rsp;
	Context->RFlags = TrapFrame->RFlags;
	Context->Xmm0 = TrapFrame->Xmm0;
	Context->Xmm1 = TrapFrame->Xmm1;
	Context->Xmm2 = TrapFrame->Xmm2;
	Context->Xmm3 = TrapFrame->Xmm3;
	Context->Xmm4 = TrapFrame->Xmm4;
	Context->Xmm5 = TrapFrame->Xmm5;
	Context->Xmm6 = TrapFrame->Xmm6;
	Context->Xmm7 = TrapFrame->Xmm7;
	Context->Xmm8 = TrapFrame->Xmm8;
	Context->Xmm9 = TrapFrame->Xmm9;
	Context->Xmm10 = TrapFrame->Xmm10;
	Context->Xmm11 = TrapFrame->Xmm11;
	Context->Xmm12 = TrapFrame->Xmm12;
	Context->Xmm13 = TrapFrame->Xmm13;
	Context->Xmm14 = TrapFrame->Xmm14;
	Context->Xmm15 = TrapFrame->Xmm15;
}

VMM_API
VOID
VcpuHandleHostException(
	_In_ PVCPU Vcpu,
	_In_ PVCPU_TRAP_FRAME TrapFrame
)
/*++
Routine Description:
	Handle host exceptions TODO: Stack walk and log error before aborting
--*/
{
	// TODO: Complete interrupts
	switch (TrapFrame->Vector)
	{
	case EXCEPTION_NON_MASKABLE_INTERRUPT:
	{
		// Start exiting on NMI interrupt windows
		VcpuSetControl(Vcpu, VMX_CTL_NMI_WINDOW_EXITING, TRUE);
		// Queue a new NMI
		InterlockedIncrement(&Vcpu->NumQueuedNMIs);
	} break;
	default:
	{
		// Stack walk like the fucking genius you are
		VCPU_CONTEXT Context = {0};
		VcpuTrapFrameToContext(TrapFrame, &Context);
	} break;
	}

}

VMM_API
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

VMM_API
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
	// TODO: Acknowledge interrupt on exit and check interrupt info in EPT violation handler?
	Vcpu->Mode = VCPU_MODE_HOST;
	Vcpu->Vmx.GuestRip = VmxRead(GUEST_RIP); 
	Vcpu->Vmx.ExitReason.Value = (UINT32)VmxRead(VM_EXIT_REASON);

	PVMX_EXIT_LOG_ENTRY ExitLog = &Vcpu->Vmx.ExitLog[Vcpu->Vmx.ExitCount++ % 256];
	ExitLog->Reason = Vcpu->Vmx.ExitReason;
	ExitLog->Rip = Vcpu->Vmx.GuestRip;
	ExitLog->ExitQualification = VmxRead(VM_EXIT_QUALIFICATION);

#if 0
	if (Vcpu->Vmx.ExitReason.BasicExitReason != 18 /* VMCALL */)
		ImpLog("[%02X-#%05d]: VM-exit ID: %i - RIP: %llX - EXIT QUAL: %llX\n",
			Vcpu->Id,
			Vcpu->Vmx.ExitCount,
			ExitLog->Reason.BasicExitReason,
			ExitLog->Rip,
			ExitLog->ExitQualification);
#endif

	VMM_EVENT_STATUS Status = sExitHandlers[Vcpu->Vmx.ExitReason.BasicExitReason](Vcpu, GuestState);

	if (Vcpu->Mode == VCPU_MODE_SHUTDOWN)
		Status = VMM_EVENT_ABORT;

	if (Status == VMM_EVENT_ABORT)
		return FALSE;

	GuestState->Rip = (UINT64)VcpuResume;

	VcpuCommitVmxState(Vcpu);

	if (Status == VMM_EVENT_CONTINUE)
		VmxAdvanceGuestRip();

	Vcpu->Mode = VCPU_MODE_GUEST;

	return TRUE;
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
	Vcpu->TscInfo.SpoofEnabled = TRUE;

	VcpuSetControl(Vcpu, VMX_CTL_SAVE_VMX_PREEMPTION_VALUE, TRUE);
	VcpuSetControl(Vcpu, VMX_CTL_VMX_PREEMPTION_TIMER, TRUE);
	VcpuSetControl(Vcpu, VMX_CTL_RDTSC_EXITING, TRUE);

	// Write the TSC watchdog quantum
	VmxWrite(GUEST_VMX_PREEMPTION_TIMER_VALUE, VTSC_WATCHDOG_QUANTUM + Vcpu->TscInfo.VmEntryLatency);
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
	case TSC_EVENT_CPUID: return Vcpu->TscInfo.CpuidLatency;
	case TSC_EVENT_RDTSC: return Vcpu->TscInfo.RdtscLatency;
	case TSC_EVENT_RDTSCP: return Vcpu->TscInfo.RdtscpLatency;
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

		ImpDebugPrint("[%02X] PrevEvent->Valid: TSC = %u + %u + %u\n", Vcpu->Id, PrevEvent->Timestamp, PrevEvent->Latency, ElapsedTime);
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

		ImpDebugPrint("[%02X] !PrevEvent->Valid: TSC = %u + %u\n", Vcpu->Id, Timestamp, PrevEvent->Latency);
	}
}

VMM_API
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
	case FEATURE_LEAF(X86_FEATURE_HYPERVISOR):
		CpuidArgs.Data[FEATURE_REG(X86_FEATURE_HYPERVISOR)] &= ~FEATURE_MASK(X86_FEATURE_HYPERVISOR);
		break;
	}

#if 0
	ImpDebugPrint("[0x%02X] CPUID %XH.%X request\n\t[%08X, %08X, %08X, %08X]\n", Vcpu->Id, GuestState->Rax, GuestState->Rcx,
		CpuidArgs.Eax, CpuidArgs.Ebx, CpuidArgs.Ecx, CpuidArgs.Edx);
#endif

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

VMM_API
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

VMM_API
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
	VMM_RDATA static const UINT64 sRegIdToGuestStateOffs[] = {
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

#define PDPTE_RESERVED_BITS \
	(XBITRANGE(1, 2) | XBITRANGE(5, 8) | XBITRANGE(52, 63))

VMM_API
NTSTATUS
VcpuLoadPDPTRs(
	_In_ PVCPU Vcpu
)
/*++
Routine Description:
	Loads PDPTR's from CR3 into the VMCS region
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	X86_CR3 Cr3 = {
		.Value = VmxRead(GUEST_CR3)
	};

	MM_PTE Pdptr[4] = {0, 0, 0, 0};
	Status = MmReadGuestPhys(PAGE_ADDRESS(Cr3.PageDirectoryBase), sizeof(Pdptr), Pdptr);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("[%02X] Failed to map CR3 PDPTR '%llX'... (%x)\n", Cr3.PageDirectoryBase, Status);
		return Status;
	}

	// Validate PDPTEs
	for (SIZE_T i = 0; i < sizeof(Pdptr) / sizeof(*Pdptr); i++)
	{
		MM_PTE Pdpte = Pdptr[i];

		if (Pdpte.Present && Pdpte.Value & PDPTE_RESERVED_BITS)
			return STATUS_INVALID_PARAMETER;
	}

	VmxWrite(GUEST_PDPTE_0, Pdptr[0].Value);
	VmxWrite(GUEST_PDPTE_1, Pdptr[1].Value);
	VmxWrite(GUEST_PDPTE_2, Pdptr[2].Value);
	VmxWrite(GUEST_PDPTE_3, Pdptr[3].Value);

	return STATUS_SUCCESS;
}

VMM_API
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
	case 8: ControlRegister = GuestState->Cr8; break; 
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

VMM_API
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

// CR0 bits that require PDPTRs to be loaded again if changed
// See Intel SDM Vol. 3 Ch 4.4.1 PDPTE Registers
#define CR0_PDPTR_CHANGE_BITS \
	((UINT64)((1ULL << 29) | (1ULL << 30) | (1ULL << 31)))

VMM_API
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

	if (!NewCr.CacheDisable && NewCr.NotWriteThrough)
	{
		VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
		return VMM_EVENT_INTERRUPT;
	}

	// VM-entry requires CR0.PG and CR0.PE to be enabled without unrestricted guest
	if (PagingDisabled || ProtectedModeDisabled)
	{
		if (Vcpu->Vmm->UseUnrestrictedGuests &&
			VcpuIsControlSupported(Vcpu, VMX_CTL_UNRESTRICTED_GUEST))
		{
			// TODO: Create identity map CR3
			VcpuSetControl(Vcpu, VMX_CTL_UNRESTRICTED_GUEST, TRUE);

			Vcpu->IsUnrestrictedGuest = TRUE;
		}
		else
		{
			// TODO: Shutdown the VMM and report to the client what happened.
			return VMM_EVENT_ABORT;
		}
	}

	// Disable unrestricted guest if paging is enabled again
	if (!PagingDisabled && !ProtectedModeDisabled)
	{
		VcpuSetControl(Vcpu, VMX_CTL_UNRESTRICTED_GUEST, FALSE);
		Vcpu->IsUnrestrictedGuest = FALSE;
	}

	// Handle invalid states of CR0.PE and CR0.PG when unrestricted guest is on
	if (Vcpu->IsUnrestrictedGuest)
	{
		// Remove CR0.PG and CR0.PE from the shadowable bits bitmask, as they can now be modified
		ShadowableBits &= ~CR0_PE_PG_BITMASK;

		if (NewCr.Paging && !NewCr.ProtectedMode)
		{
			VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
			return VMM_EVENT_INTERRUPT;
		}

		X86_CR4 Cr4 = {
			.Value = VmxRead(GUEST_CR4)
		};

		if (PagingDisabled && Cr4.PCIDEnable)
		{
			VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
			return VMM_EVENT_INTERRUPT;
		}

		// If CR0.PG is changed to a 0, all TLBs are invalidated
		if (DifferentBits.Paging && !NewCr.Paging)
			VmxInvvpid(INV_SINGLE_CONTEXT, 1);
	}

	// If CR0.PG has been changed, update IA32_EFER.LMA
	if (Vcpu->IsUnrestrictedGuest && DifferentBits.Paging)
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

		X86_SEGMENT_ACCESS_RIGHTS CsAr = {
			.Value = VmxRead(GUEST_CS_ACCESS_RIGHTS)
		};

		if (NewCr.Paging && Efer.LongModeEnable && 
			(!Cr4.PhysicalAddressExtension || CsAr.Long))
		{
			VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
			return VMM_EVENT_INTERRUPT;
		}

		// If CR0.PG has been enabled, we must check if we are in PAE paging
		if (NewCr.Paging && !Efer.LongModeEnable && Cr4.PhysicalAddressExtension && DifferentBits.Value & CR0_PDPTR_CHANGE_BITS)
		{
			if (!NT_SUCCESS(VcpuLoadPDPTRs(Vcpu)))
			{
				VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
				return VMM_EVENT_INTERRUPT;
			}
		}
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

// CR4 bits that require PDPTRs to be loaded again if changed
// See Intel SDM Vol. 3 Ch 4.4.1 PDPTE Registers
#define CR4_PDPTR_CHANGE_BITS \
	((UINT64)((1ULL << 4) | (1ULL << 5) | (1ULL << 7) | (1ULL << 20)))

VMM_API
VMM_EVENT_STATUS
VcpuHandleCr4Write(
	_Inout_ PVCPU Vcpu,
	_In_ UINT64 NewValue
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

	X86_CR0 Cr0 = {
		.Value = VmxRead(GUEST_CR0)
	};

	if (Cr0.Paging && Efer.LongModeActive && NewCr.PhysicalAddressExtension && DifferentBits.Value & CR4_PDPTR_CHANGE_BITS)
	{
		if (!NT_SUCCESS(VcpuLoadPDPTRs(Vcpu)))
		{
			VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
			return VMM_EVENT_INTERRUPT;
		}
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

	return Status;
}

VMM_API
VMM_EVENT_STATUS
VcpuHandleCr3Write(
	_Inout_ PVCPU Vcpu,
	_In_ UINT64 NewValue
)
{
	// TODO: Properly implement this
	VmxWrite(GUEST_CR3, NewValue);

	VmxInvvpid(INV_SINGLE_CONTEXT_RETAIN_GLOBALS, VmxRead(CONTROL_VIRTUAL_PROCESSOR_ID));

	return VMM_EVENT_CONTINUE;
}

VMM_API
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
	const PUINT64 TargetReg = LookupTargetReg(GuestState, ExitQual.RegisterId);

	UINT64 NewValue = 0;
	if (ExitQual.ControlRegisterId == 4 /* RSP */)
		NewValue = VmxRead(GUEST_RSP);
	else
		NewValue = *TargetReg;

	switch (ExitQual.ControlRegisterId)
	{
	case 0: return VcpuHandleCr0Write(Vcpu, NewValue); break;
	case 3: return VcpuHandleCr3Write(Vcpu, NewValue); break;
	case 4: return VcpuHandleCr4Write(Vcpu, NewValue); break;
	case 8:
		{
			// TODO: Validate CR8 value
			__writecr8(NewValue);
		} break;
	}

	return VMM_EVENT_CONTINUE;
}

VMM_API
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

VMM_API
VMM_EVENT_STATUS
VcpuEmulateLMSW(
	_Inout_ PVCPU Vcpu,
	_In_ VMX_MOV_CR_EXIT_QUALIFICATION ExitQual
)
{
	// TODO: Finish this
	return VMM_EVENT_CONTINUE;
}

VMM_API
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

VMM_API
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

VMM_API
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
	VMM_EVENT_STATUS Status = VMM_EVENT_CONTINUE;

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
		VcpuSetControl(Vcpu, VMX_CTL_VMX_PREEMPTION_TIMER, TRUE);

		VmxWrite(GUEST_VMX_PREEMPTION_TIMER_VALUE, VTSC_WATCHDOG_QUANTUM);

		VcpuPushPendingMTFEvent(Vcpu, MTF_EVENT_MEASURE_VMENTRY);

		return VMM_EVENT_CONTINUE;
	}

	PHYPERCALL_INFO Hypercall = (PHYPERCALL_INFO)&GuestState->Rax;

	Status = VmHandleHypercall(Vcpu, GuestState, Hypercall);

	Vcpu->LastHypercallResult = Hypercall->Result;

	return Status;
}

VMM_API
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

VMM_API
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

VMM_API
VMM_EVENT_STATUS
VcpuHandleTimerExpire(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	ImpDebugPrint("[%02X] VMX preemption timer expired...\n", Vcpu->Id);

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

VMM_API
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

VMM_API
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

VMM_API
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

VMM_API
VMM_EVENT_STATUS
VcpuHandleExceptionNmi(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	return VMM_EVENT_ABORT;
}

VMM_API
VMM_EVENT_STATUS
VcpuHandleExternalInterrupt(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	return VMM_EVENT_ABORT;
}

VMM_API
VMM_EVENT_STATUS
VcpuHandleNmiWindow(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	// Disable NMI window exiting if no pending NMI interrupts are left
	if (InterlockedDecrement(&Vcpu->NumQueuedNMIs) == 0)
		VcpuSetControl(Vcpu, VMX_CTL_NMI_WINDOW_EXITING, FALSE);

	VmxInjectEvent(EXCEPTION_NMI, INTERRUPT_TYPE_NMI, 0);

	return VMM_EVENT_CONTINUE;
}

VMM_API
VMM_EVENT_STATUS 
VcpuHandleInterruptWindow(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	return VMM_EVENT_CONTINUE;
}

VMM_API
VMM_EVENT_STATUS
VcpuHandleEptViolation(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	EPT_VIOLATION_EXIT_QUALIFICATION ExitQual = {
		.Value = VmxRead(VM_EXIT_QUALIFICATION)
	};

	//if (EhHandleEptViolation(Vcpu))
	//    return VMM_EVENT_CONTINUE;

	UINT64 AttemptedAddress = VmxRead(GUEST_PHYSICAL_ADDRESS);

	if (!NT_SUCCESS(
		EptMapMemoryRange(
			Vcpu->Vmm->EptInformation.SystemPml4,
			AttemptedAddress,
			AttemptedAddress,
			PAGE_SIZE,
			EPT_PAGE_RWX)
		))
	{
		ImpLog("[%02X] Failed to map %llX -> %llX...\n", Vcpu->Id, AttemptedAddress, AttemptedAddress);
		return VMM_EVENT_ABORT;
	}

#if 0
	ImpLog("[%02X-#%03d] %llX: Mapped %llX -> %llX...\n", Vcpu->Id, Vcpu->Vmx.ExitCount, Vcpu->Vmx.GuestRip, AttemptedAddress, AttemptedAddress);
#endif

	// TODO: Don't think this is necessary
	//       EPT violations for address X automatically invalidate all EPT cache entries for address X
	EptInvalidateCache();

	return VMM_EVENT_RETRY;
}

VMM_API
VMM_EVENT_STATUS
VcpuHandleEptMisconfig(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	// TODO: Panic
	ImpDebugPrint("[%02X] EPT MISCONFIG...\n", Vcpu->Id);
	__debugbreak();

	return VMM_EVENT_ABORT;
}

VMM_API
VOID
VcpuPushMTFEventEx(
	_In_ PVCPU Vcpu,
	_In_ MTF_EVENT Event
)
/*++
Routine Description:
	Registers `Event` in the MTF event stack
--*/
{
	// TODO: Propagate changes using Valid to check if its right
	PMTF_EVENT_ENTRY MtfEntry = Vcpu->MtfStackHead;

	// Make sure MtfEntry isn't valid, this can occur after VcpuPopMTFEvent
	while (MtfEntry->Valid)
	{
		if (MtfEntry->Links.Flink == NULL)
			return;

		MtfEntry = (PMTF_EVENT_ENTRY)MtfEntry->Links.Flink;
	}

	// If this MTF event entry is the first (MTF_EVENT_ENTRY::Links::Blink == NULL), enable MTF exiting
	if (MtfEntry->Links.Blink == NULL && !VmxIsEventPending(INTERRUPT_PENDING_MTF, INTERRUPT_TYPE_OTHER_EVENT))
		VcpuSetControl(Vcpu, VMX_CTL_MONITOR_TRAP_FLAG, TRUE);

	MtfEntry->Event = Event;
	MtfEntry->Valid = TRUE;

	Vcpu->MtfStackHead = (PMTF_EVENT_ENTRY)MtfEntry->Links.Flink;

	return;
}

VMM_API
VOID
VcpuPushMTFEvent(
	_In_ PVCPU Vcpu,
	_In_ MTF_EVENT_TYPE Event
)
/*++
Routine Description:
	Registers `Event` in the MTF event stack with no futher information/parameters
--*/
{
	MTF_EVENT EventEx = {
		.Type = Event
	};

	VcpuPushMTFEventEx(Vcpu, EventEx);
}

VMM_API
VOID
VcpuPushPendingMTFEvent(
	_In_ PVCPU Vcpu,
	_In_ MTF_EVENT_TYPE Event
)
{
	// Inject pending MTF interrupt
	VmxInjectEvent(INTERRUPT_PENDING_MTF, INTERRUPT_TYPE_OTHER_EVENT, 0);
	// Push MTF event
	VcpuPushMTFEvent(Vcpu, Event);
}


VMM_API
BOOLEAN
VcpuPopMTFEvent(
	_In_ PVCPU Vcpu,
	_Out_ PMTF_EVENT Event
)
/*++
Routine Description:
	Pops the top MTF event from the bottom or somethign think abotu this later
--*/
{
	PMTF_EVENT_ENTRY MtfEntry = Vcpu->MtfStackHead;

	if (MtfEntry->Valid)
		MtfEntry->Valid = FALSE;
	else        
		return FALSE;

	*Event = MtfEntry->Event;

	if (MtfEntry->Links.Blink != NULL)
		Vcpu->MtfStackHead = (PMTF_EVENT_ENTRY)MtfEntry->Links.Blink;

	return TRUE;
}

VMM_API
VMM_EVENT_STATUS 
VcpuHandleMTFExit(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	MTF_EVENT Event;
	if (VcpuPopMTFEvent(Vcpu, &Event))
	{
		switch (Event.Type)
		{
		case MTF_EVENT_MEASURE_VMENTRY:
		{
			GuestState->Rax = VTSC_WATCHDOG_QUANTUM - VmxRead(GUEST_VMX_PREEMPTION_TIMER_VALUE) - Vcpu->TscInfo.VmExitLatency;
		} break;
		case MTF_EVENT_RESET_EPT_PERMISSIONS:
		{
			if ((Event.Permissions & EPT_PAGE_RWX) != 0)
			{
				ImpDebugPrint("[%02X] Invalid MTF_EVENT_RESET_EPT_PERMISSIONS permissions (%x)...\n", Event.Permissions);
				return VMM_EVENT_CONTINUE;
			}

			if (!NT_SUCCESS(
				EptMapMemoryRange(
					Vcpu->Vmm->EptInformation.SystemPml4,
					Event.GuestPhysAddr,
					Event.PhysAddr,
					PAGE_SIZE,
					Event.Permissions)
				))
			{
				ImpDebugPrint("[%02X] Failed to remap page permissions for %llx->%llx...\n", Event.GuestPhysAddr, Event.PhysAddr);
				return VMM_EVENT_ABORT;
			}

			EptInvalidateCache();
		}
		default: 
		{   
			ImpDebugPrint("[%02X] Unknown MTF event (%x)...\n", Vcpu->Id, Event);
		} break;
		}
	}
	else
	{
		// Disable MTF exiting - no events left in queue
		VcpuSetControl(Vcpu, VMX_CTL_MONITOR_TRAP_FLAG, FALSE);
	}

	return VMM_EVENT_CONTINUE;
}

VMM_API
VMM_EVENT_STATUS 
VcpuHandleWbinvd(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	// TODO: Look into proper way to emulate this
	__wbinvd();

	return VMM_EVENT_CONTINUE;
}

VMM_API
UINT64
GetXCR0SupportedMask(VOID)
{
	// TODO: Complete
}

VMM_API
VMM_EVENT_STATUS 
VcpuHandleXsetbv(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	X86_CR4 Cr4 = {
		.Value = VmxRead(CONTROL_CR4_READ_SHADOW)
	};

	if (Cr4.XSaveEnable)
	{
		VmxInjectEvent(EXCEPTION_UNDEFINED_OPCODE, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
		return VMM_EVENT_INTERRUPT;
	}

	const UINT64 XcrIdentifier = GuestState->Rcx;

	// Only XCR0 is supported at the minute, any other value of ECX is reserved. 
	if (XcrIdentifier != 0)
	{
		VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
		return VMM_EVENT_INTERRUPT;
	}

	X86_XCR0 Xcr0 = {
		.Value = GuestState->Rdx << 32 | GuestState->Rax
	};

	// Check if SSE state is being cleared while trying to enable AVX state, AVX state relies upon both. 
	if (!Xcr0.SseState && Xcr0.AvxState)
	{
		VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
		return VMM_EVENT_INTERRUPT;
	}

	// Check if trying to clear AVX state, and trying to set OPMASK or ZMM states
	if (!Xcr0.AvxState && (Xcr0.OpMaskState || Xcr0.ZmmHi256State || Xcr0.Hi16ZmmState))
	{
		VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
		return VMM_EVENT_INTERRUPT;
	}

	// Check reserved bits.
	if (Xcr0.Value & XCR_RESERVED_BITMASK || Xcr0.Value & ~GetXCR0SupportedMask())
	{
		VmxInjectEvent(EXCEPTION_GENERAL_PROTECTION_FAULT, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
		return VMM_EVENT_INTERRUPT;
	}

	return VMM_EVENT_CONTINUE;
}

VMM_API
VMM_EVENT_STATUS 
VcpuHandleInvlpg(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	return VMM_EVENT_CONTINUE;
}

VMM_API
VMM_EVENT_STATUS
VcpuHandleInvd(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	return VMM_EVENT_CONTINUE;
}
