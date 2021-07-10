#ifndef IMP_CPU_H
#define IMP_CPU_H

#include <wdm.h>

typedef struct _CPU_STATE
{
    UINT64 Rax;
    UINT64 Rbx;
    UINT64 Rcx;
    UINT64 Rdx;
    UINT64 Rsi;
    UINT64 Rdi;
    UINT64 R8;
    UINT64 R9;
    UINT64 R10;
    UINT64 R11;
    UINT64 R12;
    UINT64 R13;
    UINT64 R14;
    UINT64 R15;
    UINT64 Rbp;
    UINT64 Rsp;
    UINT64 Rip;
    UINT32 MxCsr;
    M128A Xmm0;
    M128A Xmm1;
    M128A Xmm2;
    M128A Xmm3;
    M128A Xmm4;
    M128A Xmm5;
    M128A Xmm6;
    M128A Xmm7;
    M128A Xmm8;
    M128A Xmm9;
    M128A Xmm10;
    M128A Xmm11;
    M128A Xmm12;
    M128A Xmm13;
    M128A Xmm14;
    M128A Xmm15;
    UINT64 RFlags;
} CPU_STATE, *PCPU_STATE;

EXTERN_C
VOID
__cpu_save_state(
    _Out_ PCPU_STATE
);

EXTERN_C
DECLSPEC_NORETURN
VOID
__cpu_restore_state(
    _In_ PCPU_STATE
);


#endif
