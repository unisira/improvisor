#include "cpuid.h"

#include "../improvisor.h"

BOOLEAN
ArchCheckFeatureFlag(
    _In_ X86_CPU_FEATURE Feature
)
/*++
Routine Description:
    Checks if the current CPU supports this feature
--*/
{
    X86_CPUID_ARGS Args;

    __cpuidex(Args.Data, FEATURE_LEAF(Feature), FEATURE_SUBLEAF(Feature));

    const UINT32 Reg = Args.Data[FEATURE_REG(Feature)];
	
    return (Reg & FEATURE_MASK(Feature)) != 0;
}
