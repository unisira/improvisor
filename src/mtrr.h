#ifndef IMP_MTRR_H
#define IMP_MTRR_H

#include <ntdef.h>

#include "arch/mtrr.h"

UINT64
MtrrGetRegionSize(
    _In_ UINT64 PhysAddr
);

UINT64
MtrrGetRegionBase(
    _In_ UINT64 PhysAddr
);

MEMORY_TYPE
MtrrGetRegionType(
    _In_ UINT64 PhysAddr
);

UINT64
MtrrGetRegionEnd(
    _In_ UINT64 PhysAddr
);

NTSTATUS
MtrrInitialise(VOID);

#endif
