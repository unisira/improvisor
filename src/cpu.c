#include "cpu.h"

EXTERN
VOID
__cpu_save_state(
    _Out_ PCPU_STATE
);

EXTERN
DECLSPEC_NORETURN
VOID
__cpu_restore_state(
    _In_ PCPU_STATE
);

VOID
CpuSaveState(
    _Out_ PCPU_STATE CpuState
)
/*++
Routine Description:
    Saves an exhaustive state of the current CPU, including GPRs, debug registers, AVX registers
    The current stack pointer & base pointer and RIP
--*/
{
    __cpu_save_state(CpuState);
}

DECLSPEC_NORETURN
VOID
CpuRestoreState(
    _In_ PCPU_STATE CpuState
)
/*++
Routine Description:
    Restores the state of the current CPU from the contents of `CpuState`
--*/
{
    __cpu_restore_state(CpuState);
}

