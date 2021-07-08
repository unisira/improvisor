#ifndef IMP_CPUID_H
#define IMP_CPUID_h

#include "improvisor.h"
#include "arch/cpuid.h"

VOID
CpuidEmulateGuestCall(
    _Inout_ PX86_CPUID_ARGS Args
);

#endif
