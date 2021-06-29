#include "improvisor.h"
#include "arch/msr.h"
#include "vmx.h"

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

    IA32_VMX_BASIC_MSR VmxCap = (IA32_VMX_BASIC_MSR){
        .Value = __rdmsr(IA32_VMX_BASIC)
    };

    VmxRegion->VmcsRevisionId = VmxCap.VmcsRevisionId; 

    return VmxRegion;
}

BOOL
VmxCheckSupport(VOID)
/*++
Routine Description:
    Checks if VMX is supported by querying the VMX flag via CPUID
--*/
{
    return ArchCheckFeatureFlag(X86_FEATURE_VMX); 
}

BOOL
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

    IA32_FEATURE_CONTROL_MSR FeatureControl = (IA32_FEATURE_CONTROL_MSR){
        .Value = __readmsr(IA32_FEATURE_CONTROL)
    };

    if (FeatureControl.Lock == 0)
    {
        FeatureControl.VmxonOutsideSmx = TRUE;
        FeatureControl.Lock = TRUE;

        __wrmsr(IA32_FEATURE_CONTROL, FeatureControl.Value);

        return TRUE;
    }

    return FeatureControl.VmxonOutisdeSmx == 1;
}

VOID
VmxRestrictControlRegisters(VOID)
/*++
Routine Description:
    Applies restrictions on the CR0 and CR4 registers to prepare this CPU
    for VMX operation. See Intel SDM Vol.3 Chapter 23.8
--*/
{
    __writecr0(VmxApplyCR0Restrictions(__readcr0());
    __writecr4(VmxApplyCR4Restrictions(__readcr4());
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
VmxInjectEvent(
    _In_ APIC_EXCEPTION_VECTOR Vector,
    _In_ X86_INTERRUPT_TYPE Type,
    _In_ ULONG ErrCode
)
/*++
Routine Description:
    Injects a VMX event into the guest, such as an exception or interrupt
--*/
{
    return;
}

VOID
VmxAdvanceGuest(VOID)
/*++
Routine Description:
    Advances the guest's RIP to the next instruction
--*/
{
    VmxWrite(GUEST_RIP, VmxRead(GUEST_RIP) + VmxRead(VMEXIT_INSTRUCTION_LENGTH))
}


