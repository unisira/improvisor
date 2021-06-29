#ifndef IMP_CPU_H
#define IMP_CPU_H

typedef struct _CPU_STATE
{
    UINT64 Rax;
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
