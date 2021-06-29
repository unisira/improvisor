#ifndef IMP_CPU_H
#define IMP_CPU_H

typedef struct _M128
{
    UINT64 Lower;
    UINT64 Upper;
} M128, *PM128

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
    UINT32 _Align1;
    UINT64 _Align2;
    M128 Xmm0;
    M128 Xmm1;
    M128 Xmm2;
    M128 Xmm3;
    M128 Xmm4;
    M128 Xmm5;
    M128 Xmm6;
    M128 Xmm7;
    M128 Xmm8;
    M128 Xmm9;
    M128 Xmm10;
    M128 Xmm11;
    M128 Xmm12;
    M128 Xmm13;
    M128 Xmm14;
    M128 Xmm15;
    UINT64 RFlags;
} CPU_STATE, PCPU_STATE;

VOID
CpuSaveState(
    _Out_ PCPU_STATE
);

DECLSPEC_NORETURN
VOID
CpuRestoreState(
    _In_ PCPU_STATE
);

#endif
