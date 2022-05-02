#include "improvisor.h"
#include "arch/memory.h"
#include "util/spinlock.h"
#include "util/macro.h"
#include "util/hash.h"
#include "section.h"
#include "detour.h"
#include "vmcall.h"
#include "ldasm.h"
#include "ept.h"
#include "vmm.h"

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

static PEH_HOOK_REGISTRATION sHookRegistrationListRaw = NULL;

// The head of the hook registration list
PEH_HOOK_REGISTRATION gHookRegistrationHead = NULL;

// The lock for the hook registration list
SPINLOCK gHookRegistrationLock;

NTSTATUS
EhReserveHookRecords(
	_In_ SIZE_T Count
)
{
	// TODO: write list.c wrappers for these functions
	sHookRegistrationListRaw = (PEH_HOOK_REGISTRATION)ImpAllocateNpPool(sizeof(EH_HOOK_REGISTRATION) * Count);
	if (sHookRegistrationListRaw == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	gHookRegistrationHead = sHookRegistrationListRaw;

	for (SIZE_T i = 0; i < Count; i++)
	{
		PEH_HOOK_REGISTRATION CurrHook = sHookRegistrationListRaw + i;

		CurrHook->Links.Flink = i < Count   ? &(CurrHook + 1)->Links : NULL;
		CurrHook->Links.Blink = i > 0       ? &(CurrHook - 1)->Links : NULL;
	}

	return STATUS_SUCCESS;
}

NTSTATUS
EhRegisterHook(
	_In_ FNV1A Hash,
	_In_ PVOID Target,
	_In_ PVOID Callback
)
/*++
Routine Description:
	This function registers a hook in the list and records the target function and callback routines.
--*/
{
	SpinLock(&gHookRegistrationLock);

	// TODO: We have to waste 1, in EhReserveHookRecords increment Count by one
	PEH_HOOK_REGISTRATION Hook = gHookRegistrationHead;
	if (Hook->Links.Flink == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	Hook->Hash = Hash;
	Hook->State = EH_DETOUR_REGISTERED;
	Hook->TargetFunction = Target;
	Hook->CallbackFunction = Callback;

	gHookRegistrationHead = (PEH_HOOK_REGISTRATION)Hook->Links.Flink;

	SpinUnlock(&gHookRegistrationLock);

	return STATUS_SUCCESS;
}

VOID
EhUnregisterHook(
	_In_ PEH_HOOK_REGISTRATION Hook
)
/*++
Routine Description:
	This function unregisters and clears a hook entry, moving it to the head of the list, the entry must 
	already be 'uninitialised', meaning its MDL has been free'd if needs be etc.
--*/
{
	SpinLock(&gHookRegistrationLock); 

	PEH_HOOK_REGISTRATION HeadEntry = gHookRegistrationHead;

	Hook->Hash = 0;
	Hook->LockedTargetPage = 0;

	// TODO: Free from list and free resources if needs be

	SpinUnlock(&gHookRegistrationLock);

	return;
}

PEH_HOOK_REGISTRATION
EhFindHookByHash(
	_In_ FNV1A Hash
)
/*++
Routine Description:
	This function provides a simple way of searching for a hook registration by using a hashed string
--*/
{
	SpinLock(&gHookRegistrationLock);

	PEH_HOOK_REGISTRATION CurrHook = gHookRegistrationHead;
	while (CurrHook != NULL)
	{
		// Detour must be installed for it to have a target page MDL 
		if (CurrHook->Hash == Hash)
			return CurrHook;

		CurrHook = (PEH_HOOK_REGISTRATION)CurrHook->Links.Flink;
	}

	SpinUnlock(&gHookRegistrationLock);

	return NULL;
}

PMDL
EhFindMdlByTargetPfn(
	_In_ PVOID TargetPage
)
/*++
Routine Description:
	This function looks for a hook which resides on the same PFN and returns that so it can be used 
--*/
{
	SpinLock(&gHookRegistrationLock);

	PEH_HOOK_REGISTRATION CurrHook = gHookRegistrationHead;
	while (CurrHook != NULL)
	{
		// Detour must be installed for it to have a target page MDL 
		if (CurrHook->State == EH_DETOUR_INSTALLED)
		{
			if (PAGE_FRAME_NUMBER(CurrHook->TargetFunction) == PAGE_FRAME_NUMBER(TargetPage))
				return CurrHook->LockedTargetPage;
		}

		CurrHook = (PEH_HOOK_REGISTRATION)CurrHook->Links.Flink;
	}

	SpinUnlock(&gHookRegistrationLock);

	return NULL;
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
	_In_ PEH_HOOK_REGISTRATION Hook
)
/*++
Routine Description:
	This function creates a stub containing the first instruction of Hook->TargetFunction and a JMP instruction
	to the second instruction of TargetFunction, which can be called to call the original function of the hook
--*/
{
	static const UINT64 MINIMUM_OFFSET = 0x01; // 1 Instruction, 0xCC

	Hook->Trampoline = ImpAllocateNpPool(PAGE_SIZE);
	if (Hook->Trampoline == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	UINT64 SizeCopied = 0;
	while (MINIMUM_OFFSET > SizeCopied)
	{
		ldasm_data Ld;
		SIZE_T Step = ldasm(RVA_PTR(Hook->TargetFunction, SizeCopied), &Ld, TRUE);
		if (Step == 0)
			return STATUS_INVALID_PARAMETER;

		if (VmWriteSystemMemory(RVA_PTR(Hook->Trampoline, SizeCopied), RVA_PTR(Hook->TargetFunction, SizeCopied), Step) != HRESULT_SUCCESS)
			return STATUS_INVALID_PARAMETER;
   
		if (Ld.flags & F_RELATIVE)
			if (!EhFixupRelativeInstruction(RVA_PTR(Hook->TargetFunction, SizeCopied), RVA_PTR(Hook->Trampoline, SizeCopied), Step, &Ld))
				return STATUS_INVALID_PARAMETER;

		Hook->PrologueSize += Step;

		SizeCopied += Step;
	}

	return STATUS_SUCCESS;
}

NTSTATUS
EhInstallDetour(
	_In_ PEH_HOOK_REGISTRATION Hook
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
	Hook->GuestPhysAddr = ImpGetPhysicalAddress(PAGE_ALIGN(Hook->TargetFunction));

	// Allocate and copy over the contents of the page containing Hook->TargetFunction to the shadow page
	Hook->ShadowPage = ImpAllocateContiguousMemory(PAGE_SIZE);
	if (Hook->ShadowPage == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	Hook->ShadowPhysAddr = ImpGetPhysicalAddress(Hook->ShadowPage);

	if (VmReadSystemMemory(PAGE_ALIGN(Hook->TargetFunction), Hook->ShadowPage, PAGE_SIZE) != HRESULT_SUCCESS)
		return STATUS_FATAL_APP_EXIT;

	if (!NT_SUCCESS(EhCreateTrampoline(Hook)))
		return STATUS_INSTRUCTION_MISALIGNMENT;
   
	PUCHAR DetourShellcode = ImpAllocateNpPool(Hook->PrologueSize); 
		
	for (SIZE_T i = 0; i < Hook->PrologueSize; i++)
		*(DetourShellcode + i) = 0xCC /* NOP */;

	if (VmWriteSystemMemory(
			RVA_PTR(Hook->ShadowPage, PAGE_OFFSET(Hook->TargetFunction)), 
			DetourShellcode, 
			Hook->PrologueSize) != HRESULT_SUCCESS)
		return STATUS_FATAL_APP_EXIT;

	if (VmEptRemapPages(Hook->GuestPhysAddr, Hook->GuestPhysAddr, PAGE_SIZE, EPT_PAGE_RW) != HRESULT_SUCCESS)
		return STATUS_FATAL_APP_EXIT;

	return STATUS_SUCCESS;
}

VOID
EhDisableDetour(
	_In_ PEH_HOOK_REGISTRATION Hook
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
		return STATUS_FATAL_APP_EXIT;
}

VOID
EhEnableDetour(
	_In_ PEH_HOOK_REGISTRATION Hook
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
		return STATUS_FATAL_APP_EXIT;
}

BOOLEAN
EhIsTargetPageReferenced(
	_In_ PEH_HOOK_REGISTRATION Hook
)
/*++
Routine Description:
	This function checks if Hook->LockedTargetPage is referenced in any of the registered hooks
--*/
{
	SpinLock(&gHookRegistrationLock);

	PEH_HOOK_REGISTRATION CurrHook = gHookRegistrationHead;
	while (CurrHook != NULL)
	{
		if (CurrHook->LockedTargetPage == Hook->LockedTargetPage)
			return TRUE; 

		CurrHook = (PEH_HOOK_REGISTRATION)CurrHook->Links.Flink;
	}

	SpinUnlock(&gHookRegistrationLock);

	return FALSE;
}

VOID
EhDestroyDetour(
	_In_ PEH_HOOK_REGISTRATION Hook
)
/*++
Routine Description:
	This function frees all resources used by a detour and reverts its changes
--*/
{
	SpinLock(&gHookRegistrationLock);

	if (Hook->State != EH_DETOUR_INSTALLED &&
		Hook->State != EH_DETOUR_DISABLED)
		return;

	// TODO: Remap EPT to original page and free execution page and trampoline

	// Check if the target page MDL is referenced anywhere else, if not unlock and free it
	if (!EhIsTargetPageReferenced(Hook))
	{
		// Unlock the target function's page
		MmUnlockPages(Hook->LockedTargetPage);
		IoFreeMdl(Hook->LockedTargetPage);

		Hook->LockedTargetPage = NULL;
	}

	// TODO: Free PEH_HOOK_REGISTRATION

	SpinUnlock(&gHookRegistrationLock);
}

NTSTATUS
EhInstallHooks(VOID)
/*
Routine Description:
	This function installs any registered detour that hasn't already been installed
*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	PEH_HOOK_REGISTRATION CurrHook = gHookRegistrationHead;
	while (CurrHook != NULL)
	{
		if (CurrHook->State != EH_DETOUR_INSTALLED)
		{
			Status = EhInstallDetour(CurrHook);
			if (!NT_SUCCESS(Status))
			{
				ImpLog("Failed to install hook #%llX...\n", CurrHook->Hash);
				return Status;
			}
		}

		CurrHook = (PEH_HOOK_REGISTRATION)CurrHook->Links.Flink;
	}

	return Status;
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

	PEH_HOOK_REGISTRATION CurrHook = gHookRegistrationHead;
	while (CurrHook != NULL)
	{
		// Check if the EPT violation was a result of accessing the locked physical address of the detour
		if (CurrHook->State == EH_DETOUR_INSTALLED && PAGE_FRAME_NUMBER(AttemptedPhysAddr) == PAGE_FRAME_NUMBER(CurrHook->GuestPhysAddr))
		{
			if (ExitQual.ExecuteAccessed)
			{
				if (!NT_SUCCESS(
					EptMapMemoryRange(
						Vcpu->Vmm->EptInformation.SystemPml4,
						AttemptedPhysAddr,
						CurrHook->ShadowPhysAddr,
						PAGE_SIZE,
						EPT_PAGE_EXECUTE)
					))
				{
					// TODO: Panic here?
					return FALSE;
				}

				return TRUE;
			}
			else if (ExitQual.ReadAccessed || ExitQual.WriteAccessed)
			{
				if (!NT_SUCCESS(
					EptMapMemoryRange(
						Vcpu->Vmm->EptInformation.SystemPml4,
						AttemptedPhysAddr,
						AttemptedPhysAddr,
						PAGE_SIZE,
						EPT_PAGE_RW)
					))
				{
					return FALSE;
				}

				// EPT Violation occured for reading or writing while executing on the same page, inject MTF event to swap back to EPT_PAGE_EXECUTE 
				if (PAGE_FRAME_NUMBER(CurrHook->TargetFunction) == PAGE_FRAME_NUMBER(Vcpu->Vmx.GuestRip))
				{
					MTF_EVENT ResetEptEvent = {
						.Type = MTF_EVENT_RESET_EPT_PERMISSIONS,
						.GuestPhysAddr = AttemptedPhysAddr,
						.PhysAddr = CurrHook->ShadowPhysAddr,
						.Permissions = EPT_PAGE_EXECUTE
					};

					VcpuPushMTFEventEx(Vcpu, ResetEptEvent);
				}

				return TRUE;
			}
		}

		CurrHook = (PEH_HOOK_REGISTRATION)CurrHook->Links.Flink;
	}

	return FALSE;
}

VMM_API
BOOLEAN
EhHandleBreakpoint(
	_In_ PVCPU Vcpu
)
{
	PEH_HOOK_REGISTRATION CurrHook = gHookRegistrationHead;
	while (CurrHook != NULL)
	{
		if (CurrHook->State == EH_DETOUR_INSTALLED && Vcpu->Vmx.GuestRip == (UINT64)CurrHook->TargetFunction)
		{
			VmxWrite(GUEST_RIP, (UINT64)CurrHook->CallbackFunction);
			return TRUE;
		}

		CurrHook = (PEH_HOOK_REGISTRATION)CurrHook->Links.Flink;
	}

	return FALSE;
}
