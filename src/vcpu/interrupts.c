#include <improvisor.h>
#include <vcpu/interrupts.h>
#include <vcpu/vcpu.h>

#define VCPU_IST_STACK_SIZE 0x2000

VMM_RDATA const X86_SEGMENT_SELECTOR gVmmCsSelector = {
	.Table = SEGMENT_SELECTOR_TABLE_GDT,
	.Index = 2,
	.Rpl = 0
};

VMM_RDATA const X86_SEGMENT_SELECTOR gVmmTssSelector = {
	.Table = SEGMENT_SELECTOR_TABLE_GDT,
	.Index = 8,
	.Rpl = 0
};

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

#if 0
	ImpDebugPrint("[%02d] GDTR Base: 0x%llX, Limit: 0x%llX\n", Vcpu->Id, Gdtr.BaseAddress, Gdtr.Limit);

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
#if 0
		// Start exiting on NMI interrupt windows
		VcpuSetControl(Vcpu, VMX_CTL_NMI_WINDOW_EXITING, TRUE);
		// Queue a new NMI
		InterlockedIncrement(&Vcpu->NumQueuedNMIs);
#else
		VmxInjectEvent(EXCEPTION_NMI, INTERRUPT_TYPE_NMI, 0);
#endif
	} break;
	default:
	{
#if 0
		// Stack walk like the fucking genius you are
		VCPU_CONTEXT Context = { 0 };
		VcpuTrapFrameToContext(TrapFrame, &Context);
#endif

		__panic();
	} break;
	}

}