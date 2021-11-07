#include "improvisor.h"
#include "util/spinlock.h"
#include "util/hash.h"
#include "detour.h"
#include "vmcall.h"
#include "ldasm.h"
#include "ept.h"

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
    // TODO: We have to waste 1, in EhReserveHookRecords increment Count by one
    PEH_HOOK_REGISTRATION Hook = gHookRegistrationHead;
    if (Hook->Links.Flink == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Hook->Hash = Hash;
    Hook->State = EH_DETOUR_REGISTERED;
    Hook->TargetFunction = Target;
    Hook->CallbackFunction = Callback;

    gHookRegistrationHead = (PEH_HOOK_REGISTRATION)Hook->Links.Flink;

    return STATUS_SUCCESS;
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
    PEH_HOOK_REGISTRATION CurrHook = gHookRegistrationHead;
    while (CurrHook != NULL)
    {
        // Detour must be installed for it to have a target page MDL 
        if (CurrHook->Hash == Hash)
            return CurrHook;

        CurrHook = (PEH_HOOK_REGISTRATION)CurrHook->Links.Flink;
    }

    return NULL;
}

PMDL
EhFindTargetMdlByTargetPfn(
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
            if (PAGE_FRAME_NUMBER(CurrHook->Target) == PAGE_FRAME_NUMBER(TargetPage))
                return CurrHook->LockedTargetPage;
        }

        CurrHook = (PEH_HOOK_REGISTRATION)CurrHook->Links.Flink;
    }

    SpinUnlock(&gHookRegistrationLock);
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
    const UINT64 MINIMUM_OFFSET = 0x01 // 1 Instruction, 0xCC

    // TODO: Convert from psuedocode to real code
    Hook->Trampoline = AllocateNonEptBlock(PAGE_SIZE);
    if (Hook->Trampoline == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    UINT64 SizeCopied = 0;
    while (MINIMUM_OFFSET > SizeCopied)
    {
        ldasm_data Ld;
        SIZE_T Step = ldasm((PCHAR)Hook->TargetFunction + SizeCopied, &Ld, TRUE);
        if (Step == 0 || !ValidateLDasmResult(&Ld))
            return STATUS_INVALID_PARAMETER;

        RtlCopyMemory(Hook->Target, Hook->TargetFunction, Step);
   
        if (Ld.flags & F_RELATIVE)
        {
            Step = EhFixupRelativeInstruction();
            if (Step == 0)
                return STATUS_INVALID_PARAMETER;
        }


        SizeCopied += Step;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
EhInstallHook(
    _In_ PEH_HOOK_REGISTRATION Hook
)
/*++
Routine Description:
    This function installs the detour by setting up the trampoline and the execution page using EPT
--*/
{
    if (Hook->State == EH_DETOUR_INVALID)
        return STATUS_INVALID_PARAMETER;

    PMDL Mdl = EhFindMdlByTargetPage(Hook->TargetFunction);
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

    // Allocate and copy over the contents of the page containing Hook->TargetFunction to the shadow page
    Hook->ShadowPage = ImpAllocateContiguousMemory(PAGE_SIZE);
    if (Hook->ShadowPage == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (VmReadSystemMemory(PAGE_ALIGN(Hook->TargetFunction), Hook->ShadowPage, PAGE_SIZE) != HRESULT_SUCCESS)
        return STATUS_CRITICAL_FAILURE;

    if (!NT_SUCCESS(EhCreateTrampoline(Hook)))
        return STATUS_INSTRUCTION_MISALIGNMENT;
   
    // Write 0xCC and nops of size Hook->TrampolineSize - 1 to ShadowPage
    PUCHAR DetourShellcode = ImpAllocateNpPool() 
        

    // Enable the hook using HYPERCALL_EPT_REMAP_PAGES

    // NOTES:
    // This function is called in VMX-non root mode
    //
    // Hook->TargetFunction must be locked so that it doesn't get unmapped and mapped to a different GPA
    //
    // PEH_HOOK_REGISTRATION etc.. will be hidden because all VMM allocated data is hidden using EPT.
    // Add VMCALL to remap this stuff back into the EPT tables so it can be accessed briefly
    //
    // When reading from Hook->TargetFunction, use VPTE VMCALLs to read the contents of it so we don't taint
    // any PTEs
    //
    // Trampoline is created by copying out first instruction of Hook->TargetFunction and putting a JMP to 
    // the second instruction in Hook->TargetFunction
    // Care should be taken when relative instructions are copied out into the trampoline
    //
    // Hook->ExecutionPage be a copy of the page containing Hook->TargetFunction and Hook->TargetFunction's first
    // instruction will be replaced with 0xCC and remaining NOPs until the second instruction
    //

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
    PEH_HOOK_REGISTRATION CurrHook = gHookRegistrationHead;
    while (CurrHook != NULL)
    {
        if (CurrHook->LockedTargetPage == Hook->LockedTargetPage)
            return TRUE; 

        CurrHook = (PEH_HOOK_REGISTRATION)CurrHook->Links.Flink;
    }

    return FALSE;
}

VOID
EhDestroyDetour(
    _In_ PEH_HOOK_REGISTRATION Hook
)
/*++
Routine Description:
    This function frees all resources used by a detour
--*/
{
    SpinLock(&gHookRegistrationLock);

    if (Hook->State != EH_DETOUR_INSTALLED ||
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

    SpinUnlock(&gHookRegistrationLock);
}

NTSTATUS
EhInstallHooks(VOID)
/*
Routine Description:
    This function installs any registered detour that hasn't already been installed
*/
{
    PEH_HOOK_REGISTRATION CurrHook = gHookRegistrationHead;
    while (CurrHook != NULL)
    {
        if (CurrHook->State != EH_DETOUR_INSTALLED)
            EhInstallDetour(CurrHook);

        CurrHook = (PEH_HOOK_REGISTRATION)CurrHook->Links.Flink;
    }
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
    NTSTATUS Status = STATUS_SUCCESS;

    Status = EhReserveHookRecords(0x100);
    if (!NT_SUCCESS(Status))
        return Status;

    // TODO: Register NT hooks here

    EhRegisterHook(FNV1A_HASH("PsCallImageLoadCallbacks"));

    return Status;
}
