#include "improvisor.h"
#include "arch/interrupt.h"
#include "arch/cpuid.h"
#include "arch/msr.h"
#include "arch/cr.h"
#include "vmx.h"

VOID
__invvpid(
    _In_ UINT8 Vpid, 
    _In_ PVMX_INVVPID_DESCRIPTOR Desc
);

typedef union _VMX_CAPABILITY_MSR
{
    UINT64 Value;

	struct
	{
        UINT32 OnBits;
        UINT32 OffBits;
	};
} VMX_CAPABILITY_MSR, *PVMX_CAPABILITY_MSR;

PVMX_REGION
VmxAllocateRegion(VOID)
/*++
Routine Description:
    This function allocates a 1 KiB block of memory and initialises it as a VMX region. 
--*/
{
    PVMX_REGION VmxRegion = (PVMX_REGION)ImpAllocateHostContiguousMemory(sizeof(VMX_REGION));
    if (VmxRegion == NULL)
        return NULL;

    IA32_VMX_BASIC_MSR VmxCap = {
        .Value = __readmsr(IA32_VMX_BASIC)
    };

    VmxRegion->VmcsRevisionId = VmxCap.VmcsRevisionId; 

    return VmxRegion;
}

BOOLEAN
VmxCheckSupport(VOID)
/*++
Routine Description:
    Checks if VMX is supported by querying the VMX flag via CPUID
--*/
{
    return ArchCheckFeatureFlag(X86_FEATURE_VMX); 
}

BOOLEAN
VmxEnableVmxon(VOID)
/*++
Routine Description:
    Enables VMX inside of CR4 and enables/checks the VMXON outside SMX flag in 
    the feature control MSR
--*/
{
    X86_CR4 Cr4 = (X86_CR4){
        .Value = __readcr4()
    };

    Cr4.VmxEnable = TRUE;

    __writecr4(Cr4.Value);

    IA32_FEATURE_CONTROL_MSR FeatureControl = {
        .Value = __readmsr(IA32_FEATURE_CONTROL)
    };

    if (FeatureControl.Lock == 0)
    {
        FeatureControl.VmxonOutsideSmx = TRUE;
        FeatureControl.Lock = TRUE;

        __writemsr(IA32_FEATURE_CONTROL, FeatureControl.Value);

        return TRUE;
    }

    return FeatureControl.VmxonOutsideSmx == 1;
}

UINT64
VmxApplyCr0Restrictions(
    _In_ UINT64 Cr0
)
{
    Cr0 |= __readmsr(IA32_VMX_CR0_FIXED0);
    Cr0 &= __readmsr(IA32_VMX_CR0_FIXED1); 

    return Cr0;
}

UINT64
VmxApplyCr4Restrictions(
    _In_ UINT64 Cr4
)
{
    Cr4 |= __readmsr(IA32_VMX_CR4_FIXED0);
    Cr4 &= __readmsr(IA32_VMX_CR4_FIXED1);

    return Cr4;
}

VOID
VmxRestrictControlRegisters(VOID)
/*++
Routine Description:
    Applies restrictions on the CR0 and CR4 registers to prepare this CPU
    for VMX operation. See Intel SDM Vol.3 Chapter 23.8
--*/
{
    __writecr0(VmxApplyCr0Restrictions(__readcr0()));
    __writecr4(VmxApplyCr4Restrictions(__readcr4()));
}

BOOLEAN
VmxShouldPushErrorCode(
    _In_ VMX_ENTRY_INTERRUPT_INFO Interrupt
)
/*++
Routine Description:
    Checks if the current interrupt should push an error code onto the interrupt trap frame. See the Intel SDM
    Vol. 3 Chapter 26.2.1.3 where these checks are described
--*/
{
    if (Interrupt.Type != INTERRUPT_TYPE_HARDWARE_EXCEPTION)
        return FALSE;

    IA32_VMX_BASIC_MSR VmxCap = {
        .Value = __readmsr(IA32_VMX_BASIC)
    };

    if (VmxCap.NoErrCodeRequirement)
        return FALSE;

    X86_CR0 Cr0 = {
        .Value = VmxRead(GUEST_CR0)
    };

    if (!Cr0.ProtectedMode)
        return FALSE;

    if (Interrupt.Vector != EXCEPTION_DOUBLE_FAULT ||
        Interrupt.Vector != EXCEPTION_INVALID_TSS ||
        Interrupt.Vector != EXCEPTION_SEGMENT_NOT_PRESENT ||
        Interrupt.Vector != EXCEPTION_STACK_SEGMENT_FAULT ||
        Interrupt.Vector != EXCEPTION_GENERAL_PROTECTION_FAULT ||
        Interrupt.Vector != EXCEPTION_PAGE_FAULT ||
        Interrupt.Vector != EXCEPTION_ALIGNMENT_CHECK)
        return FALSE;

    return TRUE;
}

VOID
VmxInjectEvent(
    _In_ UINT8 Vector,
    _In_ UINT8 Type,
    _In_ UINT16 ErrorCode
)
/*++
Routine Description:
    Injects a VMX event by filling out the VM-entry interrupt information VMCS component 
--*/
{
    VMX_ENTRY_INTERRUPT_INFO Interrupt = {0};

    Interrupt.Valid = TRUE;
    Interrupt.Vector = Vector;
    Interrupt.Type = Type;
    Interrupt.DeliverErrorCode = VmxShouldPushErrorCode(Interrupt);

    VmxWrite(CONTROL_VMENTRY_EXCEPTION_ERROR_CODE, ErrorCode);
    VmxWrite(CONTROL_VMENTRY_INTERRUPT_INFO, Interrupt.Value);

    if (Interrupt.Type == INTERRUPT_TYPE_SOFTWARE_EXCEPTION ||
        Interrupt.Type == INTERRUPT_TYPE_SOFTWARE_INTERRUPT ||
        Interrupt.Type == INTERRUPT_TYPE_PRIVILEGED_SOFTWARE_INTERRUPT)
        VmxWrite(CONTROL_VMENTRY_INSTRUCTION_LEN, VmxRead(VM_EXIT_INSTRUCTION_LEN));
}

UINT64
VmxRead(
    _In_ VMCS Component
)
/*++
Routine Description:
    Read the value of a VMCS component from the active VMCS
--*/
{
    UINT64 Value = 0;
    __vmx_vmread(Component, &Value);

    return Value;
}

VOID
VmxWrite(
    _In_ VMCS Component,
    _In_ UINT64 Value
)
/*++
Routine Description:
    Write a value to a VMCS component in the active VMCS
--*/
{
    __vmx_vmwrite(Component, Value);
}

VOID
VmxInvvpid(
    _In_ VMX_INVEPT_MODE InvMode,
    _In_ UINT16 Vpid
)
{
    VMX_INVVPID_DESCRIPTOR Desc = {
        .Vpid = Vpid
    };

    __invvpid(InvMode, &Desc);
}

UINT64
VmxGetFixedBits(
    _In_ UINT64 VmxCapability
)
/*++
Routine Description:
	Calculates the bits that can be modified in a VMX control capability
--*/
{
    const VMX_CAPABILITY_MSR Cap = {
        .Value = VmxCapability
    };

    return ~(Cap.OnBits ^ Cap.OffBits);
}

VOID
VmxSetControl(
    _Inout_ PVMX_STATE Vmx,
    _In_ VMX_CONTROL Control,
    _In_ BOOLEAN State
)
/*++
Routine Description:
	Toggles a VMX execution control
--*/
{
	UINT8 ControlField = (UINT8)Control & 0x07;

    PUINT64 TargetControls = NULL;
    UINT64 TargetCap = 0;
	
    switch (ControlField)
    {
    case VMX_PINBASED_CTLS:
        TargetControls = &Vmx->Controls.PinbasedCtls;
        TargetCap = Vmx->Cap.PinbasedCtls;
        break;

    case VMX_PRIM_PROCBASED_CTLS:
        TargetControls = &Vmx->Controls.PrimaryProcbasedCtls;
        TargetCap = Vmx->Cap.PrimaryProcbasedCtls;
        break;

    case VMX_SEC_PROCBASED_CTLS:
        TargetControls = &Vmx->Controls.SecondaryProcbasedCtls;
        TargetCap = Vmx->Cap.SecondaryProcbasedCtls;
        break;

    case VMX_EXIT_CTLS:
        TargetControls = &Vmx->Controls.VmExitCtls;
        TargetCap = Vmx->Cap.VmExitCtls;
        break;

    case VMX_ENTRY_CTLS:
        TargetControls = &Vmx->Controls.VmEntryCtls;
        TargetCap = Vmx->Cap.VmEntryCtls;
        break;
    }

    UINT8 ControlBit = (UINT8)((Control >> 3) & 0x1F);

	if (VmxGetFixedBits(TargetCap) & (1ULL << ControlBit))
		return;
	
    if (State)
        *TargetControls |= (1ULL << ControlBit);
    else
        *TargetControls &= ~(1ULL << ControlBit);
}

BOOLEAN
VmxIsControlSupported(
    _Inout_ PVMX_STATE Vmx,
    _In_ VMX_CONTROL Control
)
/*++
Routine Description:
    Checks VMX control capability MSRs to see if a VMX control is supported
--*/
{
	UINT8 ControlField = (UINT8)Control & 0x07;

    UINT64 ControlCap = 0;

    switch (ControlField)
    {
    case VMX_PINBASED_CTLS: ControlCap = Vmx->Cap.PinbasedCtls; break;
    case VMX_PRIM_PROCBASED_CTLS: ControlCap = Vmx->Cap.PrimaryProcbasedCtls; break;
    case VMX_SEC_PROCBASED_CTLS: ControlCap = Vmx->Cap.SecondaryProcbasedCtls; break;
    case VMX_EXIT_CTLS: ControlCap = Vmx->Cap.VmExitCtls; break;
    case VMX_ENTRY_CTLS: ControlCap = Vmx->Cap.VmEntryCtls; break;
    }

    UINT8 ControlBit = (UINT8)((Control >> 3) & 0x1F);

    return (VmxGetFixedBits(ControlCap) & (1ULL << ControlBit)) != 0;
}

VOID
VmxSetupVmxState(
    _Inout_ PVMX_STATE Vmx
)
/*++
Routine Description:
    Initialises the VMX state's controls and capability fields to default values for VMX operation
--*/
{
    const IA32_VMX_BASIC_MSR VmxCap = {
        .Value = __readmsr(IA32_VMX_BASIC)
    };

    VMX_CAPABILITY_MSR PinbasedCap = {
		.Value = VmxCap.TrueControls ? __readmsr(IA32_VMX_TRUE_PINBASED_CTLS) : __readmsr(IA32_VMX_PINBASED_CTLS)
    };

    VMX_CAPABILITY_MSR PrimProcbasedCap = {
        .Value = VmxCap.TrueControls ? __readmsr(IA32_VMX_TRUE_PROCBASED_CTLS) : __readmsr(IA32_VMX_PROCBASED_CTLS)
    };

    VMX_CAPABILITY_MSR SecProcbasedCap = {
        .Value = __readmsr(IA32_VMX_PROCBASED_CTLS2)
    };

    VMX_CAPABILITY_MSR VmEntryCap = {
        .Value = VmxCap.TrueControls ? __readmsr(IA32_VMX_TRUE_ENTRY_CTLS) : __readmsr(IA32_VMX_ENTRY_CTLS)
    };

    VMX_CAPABILITY_MSR VmExitCap = {
        .Value = VmxCap.TrueControls ? __readmsr(IA32_VMX_TRUE_EXIT_CTLS) : __readmsr(IA32_VMX_EXIT_CTLS)
    };

    *Vmx = (VMX_STATE){
        .Cap.PinbasedCtls = PinbasedCap.Value,
        .Cap.PrimaryProcbasedCtls = PrimProcbasedCap.Value,
        .Cap.SecondaryProcbasedCtls = SecProcbasedCap.Value,
        .Cap.VmEntryCtls = VmEntryCap.Value,
        .Cap.VmExitCtls = VmExitCap.Value,

        .Controls.PinbasedCtls = PinbasedCap.OnBits,
        .Controls.PrimaryProcbasedCtls = PrimProcbasedCap.OnBits,
        .Controls.SecondaryProcbasedCtls = SecProcbasedCap.OnBits,
        .Controls.VmEntryCtls = VmEntryCap.OnBits,
        .Controls.VmExitCtls = VmExitCap.OnBits,
    };
}

VOID
VmxAdvanceGuestRip(VOID)
/*++
Routine Description:
    Advances the guest's RIP to the next instruction
--*/
{
    VmxWrite(GUEST_RIP, VmxRead(GUEST_RIP) + VmxRead(VM_EXIT_INSTRUCTION_LEN));
}
