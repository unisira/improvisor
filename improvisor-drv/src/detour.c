#include <improvisor.h>
#include <arch/memory.h>
#include <vcpu/vmcall.h>
#include <spinlock.h>
#include <detour.h>
#include <macro.h>
#include <ldasm.h>
#include <vmm.h>
#include <ll.h>

// PLAN:
//
// NOTES:
// All addresses should be 'encoded' such that they cannot be recognised as pointers within drivers
//
// EhInitialise
//      EhRegisterHooks
//  
// EhInstallHooks
//      - Install all hooks using EPT VMCALL
//
// Have a predefined list of hooks (set up in EhInitialise)
//
// For each hook in the hook registration list:
//      Allocate execution page for code, this page will be mapped to a dummy page which is empty using EPT
//      Copy contents of page into the allocated 'spoofed' page
//      Install the detour (0xCC) on the execution page
//
// For each hook in the hook registration list without EH_DETOUR_INSTALLED:
//      EPT VMCALL to remap physaddr of `Target` to physaddr of `ExecutionPage`

VMM_DATA LINKED_LIST_POOL sDetourPool;

NTSTATUS
EhInstallDetour(
	_In_ PEH_DETOUR_REGISTRATION Hook
);

NTSTATUS
EhReserveHookRecords(
	_In_ SIZE_T Count
)
{
	return LL_CREATE_POOL(&sDetourPool, EH_DETOUR_REGISTRATION, Count);
}

NTSTATUS
EhRegisterDetour(
	_In_ FNV1A Hash,
	_In_ PVOID Target,
	_In_ PVOID Callback
)
/*++
Routine Description:
	This function registers a hook in the list and records the target function and callback routines.
--*/
{
	PEH_DETOUR_REGISTRATION Detour = LlAllocate(&sDetourPool);
	// If `Detour` is null, there are no remaining free entries
	if (Detour == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	Detour->Hash = Hash;
	Detour->State = EH_DETOUR_REGISTERED;
	Detour->TargetFunction = Target;
	Detour->CallbackFunction = Callback;

	return EhInstallDetour(Detour);
}

VOID
EhUnregisterDetour(
	_In_ PEH_DETOUR_REGISTRATION Detour
)
/*++
Routine Description:
	This function unregisters and clears a hook entry, moving it to the head of the list, the entry must 
	already be 'uninitialised', meaning its MDL has been free'd if needs be etc.
--*/
{
	LlFree(&sDetourPool, &Detour->Links);
}

// Small context type for searching for detours based on their hash
typedef struct _EH_DETOUR_HASH_SEARCH
{
	FNV1A TargetHash;
} EH_DETOUR_HASH_SEARCH, *PEH_DETOUR_HASH_SEARCH;

BOOLEAN
EhpFindDetourByHashPredicate(
	_In_ PEH_DETOUR_REGISTRATION Reg,
	_In_ PEH_DETOUR_HASH_SEARCH Context
)
{
	return Reg->Hash == Context->TargetHash;
}

PEH_DETOUR_REGISTRATION
EhFindDetourByHash(
	_In_ FNV1A Hash
)
/*++
Routine Description:
	This function provides a simple way of searching for a hook registration by using a hashed string
--*/
{
	EH_DETOUR_HASH_SEARCH Context = {
		.TargetHash = Hash
	};

	return LlFind(&sDetourPool, EhpFindDetourByHashPredicate, &Context);
}

// Small context type for searching for detours based on their target function's PFN
typedef struct _EH_DETOUR_PFN_SEARCH
{
	PVOID TargetFunction;
} EH_DETOUR_PFN_SEARCH, *PEH_DETOUR_PFN_SEARCH;

BOOLEAN
EhpFindDetourByPfnPredicate(
	_In_ PEH_DETOUR_REGISTRATION Reg,
	_In_ PEH_DETOUR_PFN_SEARCH Context
)
{
	return PAGE_FRAME_NUMBER(Reg->TargetFunction) == PAGE_FRAME_NUMBER(Context->TargetFunction);
}

PMDL
EhFindMdlByTargetPfn(
	_In_ PVOID TargetFunction
)
/*++
Routine Description:
	This function looks for a hook which resides on the same PFN and returns that so it can be used 
--*/
{
	EH_DETOUR_PFN_SEARCH Context = {
		.TargetFunction = TargetFunction
	};

	return LlFind(&sDetourPool, EhpFindDetourByPfnPredicate, &Context);
}

BOOLEAN
EhFixupRelativeInstruction(
	_In_ PVOID InstructionAddr,
	_In_ PVOID DestInstruction,
	_In_ SIZE_T InstructionSz,
	_In_ ldasm_data* Ld
)
/*++
Routine Description:
	This function relocates relative instructions so their target is still valid
--*/
{
	SIZE_T DspOffset = Ld->disp_offset ? Ld->disp_offset : Ld->imm_offset;
	SIZE_T DspSize = Ld->disp_size ? Ld->disp_size : Ld->imm_size;

	// Copy the displacement from the instruction
	SIZE_T Disp = 0;
	if (VmReadSystemMemory(RVA_PTR(InstructionAddr, DspOffset), &Disp, DspSize) != HRESULT_SUCCESS)
		return FALSE;

	// Calculate the target address from the displacement
	UINT64 TargetAddr = (UINT64)InstructionAddr + InstructionSz + Disp;
	// Work out the new target
	UINT64 NewDisp = TargetAddr - (UINT64)DestInstruction + InstructionSz;

	if (VmWriteSystemMemory(RVA_PTR(DestInstruction, DspOffset), &NewDisp, DspSize) != HRESULT_SUCCESS)
		return FALSE;

	return TRUE;
}

NTSTATUS
EhCreateTrampoline(
	_In_ PEH_DETOUR_REGISTRATION Hook
)
/*++
Routine Description:
	This function creates a stub containing the first instruction of Hook->TargetFunction and a 0xCC instruction
	to the second instruction of TargetFunction, which can be called to call the original function of the hook
--*/
{
	static const UINT64 MINIMUM_OFFSET = 0x01; // 1 Instruction, 0xCC

	Hook->Trampoline = ImpAllocateNpPool(15 /* Longest possible X86 instruction */);
	if (Hook->Trampoline == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	UINT64 SizeCopied = 0;
	while (MINIMUM_OFFSET > SizeCopied)
	{
		ldasm_data Ld;
		SIZE_T Step = ldasm(RVA_PTR(Hook->TargetFunction, SizeCopied), &Ld, TRUE);
		if (Step == 0)
			return STATUS_INVALID_PARAMETER;

		if (VmWriteSystemMemory(RVA_PTR(Hook->TargetFunction, SizeCopied), RVA_PTR(Hook->Trampoline, SizeCopied), Step) != HRESULT_SUCCESS)
			return STATUS_INVALID_PARAMETER;
   
		if (Ld.flags & F_RELATIVE)
			if (!EhFixupRelativeInstruction(RVA_PTR(Hook->TargetFunction, SizeCopied), RVA_PTR(Hook->Trampoline, SizeCopied), Step, &Ld))
				return STATUS_INVALID_PARAMETER;

		Hook->PrologueSize += Step;

		SizeCopied += Step;
	}

	// TODO: 0xCC instruction to switch RIP to the rest of the original function

	return STATUS_SUCCESS;
}

NTSTATUS
EhInstallDetour(
	_In_ PEH_DETOUR_REGISTRATION Hook
)
/*++
Routine Description:
	This function installs the detour by setting up the trampoline and the execution page using EPT
--*/
{
	if (Hook->State == EH_DETOUR_INVALID)
		return STATUS_INVALID_PARAMETER;

	PMDL Mdl = EhFindMdlByTargetPfn(Hook->TargetFunction);
	if (!Mdl)
	{
		// No MDL for this page was found, allocate a new one for the page which TargetFunction resides on
		Mdl = IoAllocateMdl(PAGE_ALIGN(Hook->TargetFunction), PAGE_SIZE, FALSE, FALSE, NULL);
		if (!Mdl)
			return STATUS_INSUFFICIENT_RESOURCES;
		
		// NOTE: Should be in a SEH block but oh well
		// Lock the page TargetFunction resides on so it doesn't get mapped to a different GPA
		MmProbeAndLockPages(Mdl, KernelMode, IoReadAccess);
	}

	Hook->LockedTargetPage = Mdl;
	Hook->GuestPhysAddr = (UINT64)PAGE_ALIGN(ImpGetPhysicalAddress(Hook->TargetFunction));

	// Allocate and copy over the contents of the page containing Hook->TargetFunction to the shadow page
	Hook->ShadowPage = ImpAllocateHostContiguousMemory(PAGE_SIZE);
	if (Hook->ShadowPage == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	Hook->ShadowPhysAddr = ImpGetPhysicalAddress(Hook->ShadowPage);

	HYPERCALL_RESULT HResult = VmReadSystemMemory(PAGE_ALIGN(Hook->TargetFunction), Hook->ShadowPage, PAGE_SIZE);
	if (HResult != HRESULT_SUCCESS)
	{
		ImpDebugPrint("VmReadSystemMemory failed with %X...\n", HResult);
		return STATUS_INVALID_PARAMETER;
	}

	if (!NT_SUCCESS(EhCreateTrampoline(Hook)))
		return STATUS_INSTRUCTION_MISALIGNMENT;

	PUCHAR DetourShellcode = ImpAllocateNpPool(Hook->PrologueSize); 
		
	// Set the start of the function to INT 3's
	RtlFillMemory(DetourShellcode, Hook->PrologueSize, 0xCC);

	if (VmWriteSystemMemory(DetourShellcode, RVA_PTR(Hook->ShadowPage, PAGE_OFFSET(Hook->TargetFunction)), Hook->PrologueSize) != HRESULT_SUCCESS)
		return STATUS_INVALID_PARAMETER;

	ImpFreeAllocation(DetourShellcode);

	if (VmEptRemapPages(Hook->GuestPhysAddr, Hook->GuestPhysAddr, PAGE_SIZE, EPT_PAGE_RW) != HRESULT_SUCCESS)
		return STATUS_INVALID_PARAMETER;

	Hook->State = EH_DETOUR_INSTALLED;

	return STATUS_SUCCESS;
}

VOID
EhDisableDetour(
	_In_ PEH_DETOUR_REGISTRATION Hook
)
/*++
Routine Description:
	This function temporarily disables a detour
--*/
{
	if (Hook->State != EH_DETOUR_INSTALLED)
		return;

	Hook->State = EH_DETOUR_DISABLED;

	// Remap the GPA to RWX and make it point to the original function to avoid EPT violations for this hook
	if (VmEptRemapPages(Hook->GuestPhysAddr, Hook->GuestPhysAddr, PAGE_SIZE, EPT_PAGE_RWX) != HRESULT_SUCCESS)
		return;
}

VOID
EhEnableDetour(
	_In_ PEH_DETOUR_REGISTRATION Hook
)
/*++
Routine Description:
	This function enables a detour that is marked as disabled
--*/
{
	if (Hook->State != EH_DETOUR_DISABLED)
		return;

	Hook->State = EH_DETOUR_INSTALLED;

	// Remap the GPA to RW so X EPT violations occur
	if (VmEptRemapPages(Hook->GuestPhysAddr, Hook->GuestPhysAddr, PAGE_SIZE, EPT_PAGE_RW) != HRESULT_SUCCESS)
		return;
}

// Small context type for searching for detours based on their target function's MDL
typedef struct _EH_DETOUR_MDL_SEARCH
{
	PMDL TargetMDL;
} EH_DETOUR_MDL_SEARCH, *PEH_DETOUR_MDL_SEARCH;

BOOLEAN
EhpFindDetourByMdlPredicate(
	_In_ PEH_DETOUR_REGISTRATION Reg,
	_In_ PEH_DETOUR_MDL_SEARCH Context
)
{
	return Reg->LockedTargetPage == Context->TargetMDL;
}

BOOLEAN
EhIsTargetPageReferenced(
	_In_ PEH_DETOUR_REGISTRATION Hook
)
/*++
Routine Description:
	This function checks if Hook->LockedTargetPage is referenced in any of the registered hooks
--*/
{
	EH_DETOUR_MDL_SEARCH Context = {
		.TargetMDL = Hook->LockedTargetPage
	};

	return LlFind(&sDetourPool, EhpFindDetourByMdlPredicate, &Context) != NULL;
}

VOID
EhDestroyDetour(
	_In_ PEH_DETOUR_REGISTRATION Hook
)
/*++
Routine Description:
	This function frees all resources used by a detour and reverts its changes
--*/
{
	if (Hook->State != EH_DETOUR_INSTALLED &&
		Hook->State != EH_DETOUR_DISABLED)
		return;

	// Remap the original GPA with RWX so EPT violations no longer occur upon execution
	if (VmEptRemapPages(Hook->GuestPhysAddr, Hook->GuestPhysAddr, PAGE_SIZE, EPT_PAGE_RWX) != HRESULT_SUCCESS)
		return;

	// The hook is now permanently removed, set its state to invalid
	Hook->State = EH_DETOUR_INVALID;

	// Check if the target page MDL is referenced anywhere else, if not unlock and free it
	if (!EhIsTargetPageReferenced(Hook))
	{
		// Unlock the target function's page
		MmUnlockPages(Hook->LockedTargetPage);
		IoFreeMdl(Hook->LockedTargetPage);
	}

	// NOTE: Should we clear the rest of the members of `Hook` before freeing?
	LlFree(&sDetourPool, &Hook->Links);
}

NTSTATUS
EhInitialise(VOID)
/*++
Routine Description:
	This function allocates hook records so they can be registered and then later installed, disabled or removed and vice versa

	This function should allocate hook records, then register any hooks which can be registered at this point. Somewhere later, in
	VmmStartHypervisor, these hooks will be installed using EhInstallHook

	EhInstallHook will lock the target function in place, allocate and copy the target functions page as well as creating a trampoline stub
	which can be called to invoke the original behaviour of the function. It will then use HYPERCALL_EPT_MAP_PAGES to map the target functions
	page to the copied page which will have the breakpoint instruction written to it, which will allow us to gain control of the function
--*/
{
	// TODO: Data hooks (Redirect R to hidden page, allow WX to 'real' page)

	NTSTATUS Status = STATUS_SUCCESS;

	Status = EhReserveHookRecords(0x100);
	if (!NT_SUCCESS(Status))
		return Status;

	return Status;
}

VMM_API
BOOLEAN
EhHandleEptViolation(
	_In_ PVCPU Vcpu
)
{
	EPT_VIOLATION_EXIT_QUALIFICATION ExitQual = {
		.Value = VmxRead(VM_EXIT_QUALIFICATION)
	};

	UINT64 AttemptedPhysAddr = VmxRead(GUEST_PHYSICAL_ADDRESS);

	// TODO: Handle reads on same page as execution
	// When ExitQual.ReadAccessed:
	// Check if current page has execute only permissions, and check if the page containing GUEST_RIP is CurrHook->TargetFunction
	// If this happened, apply EPT_PAGE_READ and setup MTF event
	// 
	// Attmpted read happens on same page
	// 
	// When ExitQual.ExecuteAccessed:
	// Check if the current page has read only permissions, and check if the page being
	//
	//
	// RTM checks on hooks are pretty disastrous
	// They do the following:
	// - Choose a target function
	// - One of the following:
	//   - Write `retn` to the start of the function
	//   - Find a `retn` instruction on the same page, and read it
	// - Enter a RTM transaction
	// - Attempt to execute the `retn` instruction
	// - An EPT violation will be caught by the transaction, detecting EPT operation on this page
	// 
	// To fix this, all reads on executable pages should inject a pending MTF exit which will be delivered after 
	// execution of the read instruction. When handling this MTF exit, swap the execution of the affected page back to
	// execute only, this means when executing the instruction, it shouldn't cause an EPT violation

	if (LlIsEmpty(&sDetourPool))
		return FALSE;

	PLIST_ENTRY CurrLink = sDetourPool.Used.Flink;
	while (CurrLink != &sDetourPool.Used)
	{
		PEH_DETOUR_REGISTRATION CurrHook = CONTAINING_RECORD(CurrLink, EH_DETOUR_REGISTRATION, Links);
		// Check if the EPT violation was a result of accessing the locked physical address of the detour
		if (CurrHook->State == EH_DETOUR_INSTALLED && (UINT64)PAGE_ALIGN(AttemptedPhysAddr) == CurrHook->GuestPhysAddr)
		{
			if (ExitQual.ExecuteAccessed)
			{
				if (!NT_SUCCESS(
					EptMapMemoryRange(
						Vcpu->Vmm->Ept.Pml4,
						CurrHook->GuestPhysAddr,
						CurrHook->ShadowPhysAddr,
						PAGE_SIZE,
						EPT_PAGE_EXECUTE)
					))
				{
					ImpLog("EptMapMemoryRange failed mapping %llX->%llX with EPT_PAGE_EXECUTE\n", AttemptedPhysAddr, CurrHook->ShadowPhysAddr);
					// TODO: Panic here?
					return FALSE;
				}

				return TRUE;
			}
			else if (ExitQual.ReadAccessed || ExitQual.WriteAccessed)
			{
				EPT_PAGE_PERMISSIONS Perms = EPT_PAGE_RW;

#if 0 // I don't think this works currently, fuck knows why
				// EPT Violation occured for reading or writing while executing on the same page, inject MTF event to swap back to EPT_PAGE_EXECUTE 
				if (PAGE_FRAME_NUMBER(CurrHook->TargetFunction) == PAGE_FRAME_NUMBER(Vcpu->Vmx.GuestRip))
				{
					// Page permissions need to be RWX (reading or writing on same page as execution)
					Perms = EPT_PAGE_RWX;

					MTF_EVENT ResetEptEvent = {
						.Type = MTF_EVENT_RESET_EPT_PERMISSIONS,
						.GuestPhysAddr = AttemptedPhysAddr,
						.PhysAddr = CurrHook->ShadowPhysAddr,
						.Permissions = EPT_PAGE_EXECUTE
					};

					VcpuPushMTFEventEx(Vcpu, ResetEptEvent);
				}
#endif

				if (!NT_SUCCESS(
					EptMapMemoryRange(
						Vcpu->Vmm->Ept.Pml4,
						CurrHook->GuestPhysAddr,
						CurrHook->GuestPhysAddr,
						PAGE_SIZE,
						Perms)
				))
				{
					ImpLog("EptMapMemoryRange failed mapping %llX->%llX with Perms=%x\n", AttemptedPhysAddr, AttemptedPhysAddr, Perms);
					return FALSE;
				}

#ifdef _DEBUG
				ImpLog("[Detour #%08X] Swapped to RW page %llX\n", CurrHook->Hash, CurrHook->GuestPhysAddr);
#endif

				return TRUE;
			}
		}

		CurrLink = CurrLink->Flink;
	}

	return FALSE;
}

typedef struct _EH_DETOUR_BREAKPOINT_SEARCH
{
	UINT64 GuestRip;
} EH_DETOUR_BREAKPOINT_SEARCH, *PEH_DETOUR_BREAKPOINT_SEARCH;

VMM_API
BOOLEAN
EhpHandleBreakpointPredicate(
	_In_ PEH_DETOUR_REGISTRATION Reg,
	_In_ PEH_DETOUR_BREAKPOINT_SEARCH Context
)
{
	if (Context->GuestRip == (UINT64)Reg->TargetFunction)
	{
		VmxWrite(GUEST_RIP, (UINT64)Reg->TargetFunction);
		return TRUE;
	}
	// Handle INT 3 after the prologue instruction(s) in `EH_DETOUR_REGISTRATION::Trampoline`
	else if (Context->GuestRip == RVA(Reg->Trampoline, Reg->PrologueSize))
	{
		VmxWrite(GUEST_RIP, RVA(Reg->TargetFunction, Reg->PrologueSize));
		return TRUE;
	}

	return FALSE;
}

VMM_API
BOOLEAN
EhHandleBreakpoint(
	_In_ PVCPU Vcpu
)
/*++
Routine Description:
	This function checks if Hook->LockedTargetPage is referenced in any of the registered hooks
--*/
{
	if (LlIsEmpty(&sDetourPool))
		return FALSE;
	
	PLIST_ENTRY Link = sDetourPool.Used.Flink;
	while (Link != &sDetourPool.Used)
	{
		PEH_DETOUR_REGISTRATION CurrHook = CONTAINING_RECORD(Link, EH_DETOUR_REGISTRATION, Links);

		if (CurrHook->State != EH_DETOUR_INSTALLED)
			goto next;

		if (Vcpu->Vmx.GuestRip == (UINT64)CurrHook->TargetFunction)
		{
			VmxWrite(GUEST_RIP, (UINT64)CurrHook->CallbackFunction);
			return TRUE;
		}
		// Handle INT 3 after the prologue instruction(s) in `EH_HOOK_REGISTRATION::Trampoline`
		else if (Vcpu->Vmx.GuestRip == RVA(CurrHook->Trampoline, CurrHook->PrologueSize))
		{
			VmxWrite(GUEST_RIP, RVA(CurrHook->TargetFunction, CurrHook->PrologueSize));
			return TRUE;
		}

	next:
		Link = Link->Flink;
	}

	return FALSE;
}
