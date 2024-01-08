#include <improvisor.h>
#include <arch/msr.h>
#include <arch/cpuid.h>
#include <arch/memory.h>
#include <vcpu/tsc.h>
#include <vcpu/vcpu.h>
#include <vcpu/vmexit.h>
#include <vcpu/vmcall.h>
#include <pdb/pdb.h>
#include <mm/mm.h>
#include <mm/vpte.h>
#include <detour.h>
#include <macro.h>
#include <ept.h>
#include <vmm.h>
#include <win.h>

VMM_API
BOOLEAN
VcpuHandleExit(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
);

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

VMM_RDATA static VMEXIT_HANDLER* sExitHandlers[] = {
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

	// Dont log VM-exit's for VMCALLs, as this would clutter the logs with things that don't matter
	if (Vcpu->Vmx.ExitReason.BasicExitReason != EXIT_REASON_VMCALL)
	{
		PVMX_EXIT_LOG_ENTRY ExitLog = &Vcpu->Vmx.ExitLog[Vcpu->Vmx.ExitCount++ % 256];
		ExitLog->Reason = Vcpu->Vmx.ExitReason;
		ExitLog->Rip = Vcpu->Vmx.GuestRip;
		ExitLog->ExitQualification = VmxRead(VM_EXIT_QUALIFICATION);

#if 0
		ImpLog("[%02X-#%03d]: VM-exit - Type: %i - RIP: %llX - EXIT QUAL: %llX\n",
			Vcpu->Id,
			Vcpu->Vmx.ExitCount,
			ExitLog->Reason.BasicExitReason,
			ExitLog->Rip,
			ExitLog->ExitQualification);
#endif
	}

	VMM_EVENT_STATUS Status = sExitHandlers[Vcpu->Vmx.ExitReason.BasicExitReason](Vcpu, GuestState);

	// If this VCPU was signalled to shutdown, return FALSE to make VcpuShutdownVmx
	if (Vcpu->Mode == VCPU_MODE_SHUTDOWN)
	{
		// Check if this VCPU was signalled through HYPERCALL_SHUTDOWN_VCPU and handle it accordingly
		if (Vcpu->Vmx.ExitReason.BasicExitReason == EXIT_REASON_VMCALL)
		{
			HYPERCALL_INFO AttemptedHypercall = {
				.Value = GuestState->Rax
			};

			if (AttemptedHypercall.Id == HYPERCALL_SHUTDOWN_VCPU)
			{
				// Increment guest rip to skip the VMCALL instruction. It must not be re-attempted once VMX operation has shut down
				VmxAdvanceGuestRip();
			}
		}

		return FALSE;
	}

	GuestState->Rip = (UINT64)VcpuResume;

	// Recover blocking by NMI based on if the VM-exit signalled that they 
	// were unblocked when they shouldn't have been
	VcpuRecoverNMIBlocking(Vcpu);
	// Update VMX state in the current VMCS
	VcpuCommitVmxState(Vcpu);

	if (Status == VMM_EVENT_CONTINUE)
		VmxAdvanceGuestRip();

	Vcpu->Mode = VCPU_MODE_GUEST;

	return TRUE;
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
	// Handle checks for CPU virtualisation, see vcpu.asm
	// TODO: Do this in a better way in __vmexit_entry
	if (GuestState->Rdx == 0x441C88F24291161F)
	{
		GuestState->Rsi = TRUE;
		// Continue, not a real CPUID call (could cause issues?).
		return VMM_EVENT_CONTINUE;
	}

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
	ImpLog("[0x%02X] CPUID %XH.%X request\n\t[%08X, %08X, %08X, %08X]\n", Vcpu->Id, GuestState->Rax, GuestState->Rcx,
		CpuidArgs.Eax, CpuidArgs.Ebx, CpuidArgs.Ecx, CpuidArgs.Edx);
#endif

	GuestState->Rax = CpuidArgs.Eax;
	GuestState->Rbx = CpuidArgs.Ebx;
	GuestState->Rcx = CpuidArgs.Ecx;
	GuestState->Rdx = CpuidArgs.Edx;

	if (Vcpu->Vmm->UseTscSpoofing)
	{
		if (!Vcpu->Tsc.SpoofEnabled)
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

	ImpLog("[%02X] VM-entry failed due to invalid guest state...\n", Vcpu->Id);

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

	MM_PTE Pdptrs[4] = { 0, 0, 0, 0 };
	Status = MmReadGuestPhys(PAGE_ADDRESS(Cr3.PageDirectoryBase), sizeof(Pdptrs), Pdptrs);
	if (!NT_SUCCESS(Status))
	{
		ImpLog("[%02X] Failed to map CR3 PDPTR '%llX'... (%x)\n", Cr3.PageDirectoryBase, Status);
		return Status;
	}

	// Validate PDPTEs
	for (SIZE_T i = 0; i < sizeof(Pdptrs) / sizeof(*Pdptrs); i++)
	{
		MM_PTE Pdpte = Pdptrs[i];

		if (Pdpte.Present && Pdpte.Value & PDPTE_RESERVED_BITS)
			return STATUS_INVALID_PARAMETER;
	}

	VmxWrite(GUEST_PDPTE_0, Pdptrs[0].Value);
	VmxWrite(GUEST_PDPTE_1, Pdptrs[1].Value);
	VmxWrite(GUEST_PDPTE_2, Pdptrs[2].Value);
	VmxWrite(GUEST_PDPTE_3, Pdptrs[3].Value);

	return STATUS_SUCCESS;
}

VMM_API
VMM_EVENT_STATUS
VcpuHandleCrRead(
	PVCPU Vcpu,
	PGUEST_STATE GuestState,
	VMX_MOV_CR_EXIT_QUALIFICATION ExitQual
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

#if 0 // Move this into vmexit.asm and so it ASAP, or when launching the VMM 
	// Check if we are measuring VM-exit latency
	if (GuestState->Rbx == 0x1FF2C88911424416 && Vcpu->Tsc.VmExitLatency == 0)
	{
		LARGE_INTEGER PreTsc = {
			.LowPart = GuestState->Rax,
			.HighPart = GuestState->Rdx
		};

		GuestState->Rax = VcpuGetVmExitStoreValue(IA32_TIME_STAMP_COUNTER) - PreTsc.QuadPart;

		return VMM_EVENT_CONTINUE;
	}

	if (GuestState->Rbx == 0xF2C889114244161F && Vcpu->Tsc.VmEntryLatency == 0)
	{
		VcpuSetControl(Vcpu, VMX_CTL_VMX_PREEMPTION_TIMER, TRUE);

		VmxWrite(GUEST_VMX_PREEMPTION_TIMER_VALUE, VTSC_WATCHDOG_QUANTUM);

		VcpuPushPendingMTFEvent(Vcpu, MTF_EVENT_MEASURE_VMENTRY);

		return VMM_EVENT_CONTINUE;
	}
#endif

	PHYPERCALL_INFO Hypercall = (PHYPERCALL_INFO)&GuestState->Rax;

	Status = VmHandleHypercall(Vcpu, GuestState, Hypercall);

	// Store the result of this hypercall
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

	if (Vcpu->Tsc.SpoofEnabled)
	{
		VcpuUpdateLastTscEventEntry(Vcpu, TSC_EVENT_RDTSC);

		PTSC_EVENT_ENTRY PrevEvent = &Vcpu->Tsc.PrevEvent;
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

	if (Vcpu->Tsc.SpoofEnabled)
	{
		VcpuUpdateLastTscEventEntry(Vcpu, TSC_EVENT_RDTSCP);

		// Set the TSC to the previous event's timestamp and its estimated latency
		Tsc.QuadPart = Vcpu->Tsc.PrevEvent.Timestamp + Vcpu->Tsc.PrevEvent.Latency;
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
	if (Vcpu->Tsc.SpoofEnabled)
	{
		Vcpu->Tsc.PrevEvent.Valid = FALSE;

		VcpuSetControl(Vcpu, VMX_CTL_SAVE_VMX_PREEMPTION_VALUE, FALSE);
		VcpuSetControl(Vcpu, VMX_CTL_VMX_PREEMPTION_TIMER, FALSE);
		VcpuSetControl(Vcpu, VMX_CTL_RDTSC_EXITING, FALSE);

		VmxWrite(GUEST_VMX_PREEMPTION_TIMER_VALUE, 0);

		Vcpu->Tsc.SpoofEnabled = FALSE;
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
VOID
VcpuRecoverNMIBlocking(
	_In_ PVCPU Vcpu
)
/*++
Routine Descriptions:
	This function handles times where NMI's were unblocked due to execution of IRET, but it faulted in some way.
	In this case, NMI's will appear to be unblocked and should be set to blocked again
--*/
{
	VMX_IDT_VECTORING_INFO VecInfo = {
		.Value = VmxRead(IDT_VECTORING_INFO)
	};

	// NMI unblocking due to IRET execution faulted, re-set NMI blocking in the guest interruptibility state
	//
	// NMI unblocking due to IRET is undefined when:
	// - The Valid bit of the IDT vectoring information field in the active VMCS is 1
	// - The Vector field of the VM-exit interrupt information field in the active VMCS indicates a #DF
	if (VecInfo.Valid == 0)
	{
		// NOTE: There are some more VM-exits that have this, but I don't have any of them enabled.
		if (Vcpu->Vmx.ExitReason.BasicExitReason == EXIT_REASON_EXCEPTION_NMI)
		{
			// For VM exits due to faults, NMI unblocking due to IRET is saved in bit 12 of the 
			// VM-exit interruption-information field(Section 27.2.2).

			VMX_EXIT_INTERRUPT_INFO IntrInfo = {
				.Value = VmxRead(VM_EXIT_INTERRUPT_INFO)
			};

			if (IntrInfo.Type == INTERRUPT_TYPE_HARDWARE_EXCEPTION && IntrInfo.Vector == EXCEPTION_DF)
				return;

			VMX_GUEST_INTERRUPTIBILITY_STATE InterruptibilityState = {
				.Value = VmxRead(GUEST_INTERRUPTIBILITY_STATE)
			};

			InterruptibilityState.BlockingByNMI = !IntrInfo.NmiUnblocking;

			VmxWrite(GUEST_INTERRUPTIBILITY_STATE, InterruptibilityState.Value);
		}
		else if (Vcpu->Vmx.ExitReason.BasicExitReason == EXIT_REASON_EPT_VIOLATION)
		{
			// For VM exits due to EPT violations, page-modification log-full events, and SPP-related events, 
			// NMI unblocking due to IRET is saved in bit 12 of the exit qualification (Section 27.2.1).

			EPT_VIOLATION_EXIT_QUALIFICATION ExitQual = {
				.Value = VmxRead(VM_EXIT_QUALIFICATION)
			};

			VMX_GUEST_INTERRUPTIBILITY_STATE InterruptibilityState = {
				.Value = VmxRead(GUEST_INTERRUPTIBILITY_STATE)
			};

			InterruptibilityState.BlockingByNMI = !ExitQual.NmiUnblocking;

			VmxWrite(GUEST_INTERRUPTIBILITY_STATE, InterruptibilityState.Value);
		}
	}
}

VMM_API
VMM_EVENT_STATUS
VcpuHandleVectoredExceptions(
	_In_ PVCPU Vcpu
)
/*++
Routine Description:
	This function handles IDT-vectoring information and ensures that any events that caused VM-exits are delivered properly.
	For reference to where this function should be called in the future, check '27.2.4 Information for VM Exits During Event Delivery'
--*/
{
	VMX_IDT_VECTORING_INFO VecInfo = {
		.Value = VmxRead(IDT_VECTORING_INFO)
	};

	// No vectored exception waiting to be delivered, return
	if (VecInfo.Valid == FALSE)
		return VMM_EVENT_CONTINUE;

	if (Vcpu->Vmx.ExitReason.BasicExitReason == EXIT_REASON_EXCEPTION_NMI)
	{
		VMX_EXIT_INTERRUPT_INFO IntrInfo = {
			.Value = VmxRead(VM_EXIT_INTERRUPT_INFO)
		};

		// TODO: Handle #DF's
	}

	VmxInjectEvent(VecInfo.Vector, VecInfo.Type, VecInfo.ErrorCodeValid ? VmxRead(IDT_VECTORING_ERROR_CODE) : 0);

	return VMM_EVENT_INTERRUPT;
}

VMM_API
VMM_EVENT_STATUS
VcpuHandleExceptionNmi(
	_Inout_ PVCPU Vcpu,
	_Inout_ PGUEST_STATE GuestState
)
{
	// TODO: Update architecture state on #DB:
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
	// Check for IDT-vectoring information here, and handle double faults properly.
	// Execution of NMIs usually unblocks NMI blocking, but with NMI exiting on, it does not, we must

	VMX_EXIT_INTERRUPT_INFO IntrInfo = {
		.Value = VmxRead(VM_EXIT_INTERRUPT_INFO)
	};

	// Handle vectored exceptions and possible double faults
	// If this function successfully injects its own event and not the
	// event from VM_EXIT_INTERRUPT_INFO, return now and signal and interrupt
	if (VcpuHandleVectoredExceptions(Vcpu) == VMM_EVENT_INTERRUPT)
		return VMM_EVENT_INTERRUPT;

	switch (IntrInfo.Vector)
	{
	case EXCEPTION_BREAKPOINT:
	{
		if (EhHandleBreakpoint(Vcpu))
			return VMM_EVENT_RETRY;
		else
			VmxInjectEvent(IntrInfo.Vector, IntrInfo.Type, VmxRead(VM_EXIT_INTERRUPT_ERROR_CODE));
	} break;
#if 0 // NOTE: I don't use NMI exiting at the minute
	case EXCEPTION_NON_MASKABLE_INTERRUPT:
	{
		// TODO: NMI executed, NMI blocking is in effect
		Vcpu->NMIsBlocked = TRUE;

		// TODO: This should never happen (unless i start instrumenting NMI's for something and end up injecting an event because of it?)
		if (VmxIsAnyEventPending())
		{
			// Add one pending NMI
			InterlockedIncrement(&Vcpu->NumQueuedNMIs);
			// Start exiting on NMI windows
			VcpuSetControl(Vcpu, VMX_CTL_NMI_WINDOW_EXITING, TRUE);
			// Nothing else to do here, continue
			return VMM_EVENT_CONTINUE;
		}
		else
		{
			// TODO: CHECK BLOCKING BY NMI IN GUEST INTERRUPTIBILITY
			// Inject this NMI now, no need to make it pending
			VmxInjectEvent(EXCEPTION_NON_MASKABLE_INTERRUPT, INTERRUPT_TYPE_NMI, 0);
		}
	} break;
#endif
	default:
	{
		VmxInjectEvent(IntrInfo.Vector, IntrInfo.Type, VmxRead(VM_EXIT_INTERRUPT_ERROR_CODE));
	} break;
	}

	return VMM_EVENT_INTERRUPT;
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
	// TODO: Check IDT-vectoring information to see if this EPT violation was caused by event delivery

	EPT_VIOLATION_EXIT_QUALIFICATION ExitQual = {
		.Value = VmxRead(VM_EXIT_QUALIFICATION)
	};

	if (EhHandleEptViolation(Vcpu))
		return VMM_EVENT_RETRY;

	// Handle vectored exceptions and possible double faults
	VcpuHandleVectoredExceptions(Vcpu);

	UINT64 AttemptedAddress = VmxRead(GUEST_PHYSICAL_ADDRESS);

	if (!NT_SUCCESS(
		EptMapMemoryRange(
			Vcpu->Vmm->Ept.Pml4,
			AttemptedAddress,
			AttemptedAddress,
			PAGE_SIZE,
			EPT_PAGE_RWX)
	))
	{
		ImpLog("[%02X] Failed to map %llX -> %llX...\n", Vcpu->Id, AttemptedAddress, AttemptedAddress);
		return VMM_EVENT_ABORT;
	}

	ImpLog("[%02X-#%03d] %llX: Mapped %llX -> %llX...\n", Vcpu->Id, Vcpu->Vmx.ExitCount, Vcpu->Vmx.GuestRip, AttemptedAddress, AttemptedAddress);

	// NOTE: Don't think this is necessary
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
	ImpLog("[%02X] EPT misconfig, aborting...\n", Vcpu->Id);

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
			GuestState->Rax = VTSC_WATCHDOG_QUANTUM - VmxRead(GUEST_VMX_PREEMPTION_TIMER_VALUE) - Vcpu->Tsc.VmExitLatency;
		} break;
		case MTF_EVENT_RESET_EPT_PERMISSIONS:
		{
			// NOTE: This doesn't check user-execution
			if ((Event.Permissions & EPT_PAGE_RWX) != 0)
			{
				ImpLog("[%02X] Invalid MTF_EVENT_RESET_EPT_PERMISSIONS permissions (%x)...\n", Event.Permissions);
				return VMM_EVENT_CONTINUE;
			}

			if (!NT_SUCCESS(
				EptMapMemoryRange(
					Vcpu->Vmm->Ept.Pml4,
					Event.GuestPhysAddr,
					Event.PhysAddr,
					PAGE_SIZE,
					Event.Permissions)
			))
			{
				ImpLog("[%02X] Failed to remap page permissions for %llx->%llx...\n", Vcpu->Id, Event.GuestPhysAddr, Event.PhysAddr);
				return VMM_EVENT_ABORT;
			}

			EptInvalidateCache();
		}
		default:
		{
			ImpLog("[%02X] Unknown MTF event (%x)...\n", Vcpu->Id, Event);
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
	return -1;
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
