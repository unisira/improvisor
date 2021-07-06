#include "improvisor.h"
#include "arch/cpuid.h"
#include "arch/msr.h"
#include "arch/cr.h"
#include "vmx.h"

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
    PVMX_REGION VmxRegion = (PVMX_REGION)ImpAllocateContiguousMemory(sizeof(VMX_REGION));
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

UINT64
VmxGetFixedBits(
    _In_ UINT64 VmxCapability
)
/*++
Routine Description:
	Calculates the bits that can be modified in a VMX control capability
--*/
{
    VMX_CAPABILITY_MSR Cap = {
        .Value = VmxCapability
    };

    return ~(Cap.OnBits ^ Cap.OffBits);
}

VOID
VmxToggleControl(
    _Inout_ PVMX_STATE Vmx,
    _In_ VMX_CONTROL Control
)
/*++
Routine Description:
	Toggles a VMX execution control
--*/
{
	UINT8 ControlField = (UINT8)Control & 0x03;

    PUINT64 TargetControls = &((PUINT64)&Vmx->Controls)[ControlField];
    UINT64 TargetCap = ((PUINT64)&Vmx->Cap)[ControlField];

    UINT8 ControlBit = (UINT8)((Control & 0xF8));

	if (VmxGetFixedBits(TargetCap) & ControlBit)
		return;
	
    *TargetControls |= !(*TargetControls & ControlBit);
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


