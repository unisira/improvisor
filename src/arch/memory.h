#ifndef IMP_ARCH_MEMORY_H
#define IMP_ARCH_MEMORY_H

#include <ntdef.h>

typedef union _X86_LA48
{
    UINT64 Value;

    struct
    {
        UINT64 PageOffset : 12;
        UINT64 PtIndex : 9;
        UINT64 PdIndex : 9;
        UINT64 PdptIndex : 9;
        UINT64 Pml4Index : 9;
        UINT64 Reserved1 : 16;
    };
} X86_LA48, *PX86_LA48;

typedef union _X86_LA57
{
    UINT64 Value;

    struct
    {
        UINT64 PageOffset : 12;
        UINT64 PtIndex : 9;
        UINT64 PdIndex : 9;
        UINT64 PdptIndex : 9;
        UINT64 Pml4Index : 9;
        UINT64 Pml5Index : 9;
        UINT64 Reserved1 : 7;
    };
} X86_LA57, *PX86_LA57;


#endif
