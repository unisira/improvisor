#ifndef IMP_VCPU_INTERRUPTS_H
#define IMP_VCPU_INTERRUPTS_H

#include <arch/segment.h>
#include <arch/cpu.h>
#include <vcpu/vcpu.h>

extern const X86_SEGMENT_SELECTOR gVmmCsSelector;
extern const X86_SEGMENT_SELECTOR gVmmTssSelector;

// Returns guest execution to where the host faulted and alerts the debugger.
EXTERN_C
VOID
__panic(UINT64);

// Saves CPU state and transfers execution to VcpuHandleHostException
EXTERN_C
VOID
__vmm_generic_intr_handler();

#pragma pack(push, 1)
typedef struct _VCPU_TRAP_FRAME
{
	// Register State
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

	// Interrupt Vector
	UINT64 Vector;

	// Machine Trap Frame
	UINT64 Error;
	UINT64 Rip;
	UINT64 Cs;
	UINT64 RFlags;
	UINT64 Rsp;
	UINT64 Ss;
} VCPU_TRAP_FRAME, * PVCPU_TRAP_FRAME;
#pragma pack(pop)

NTSTATUS
VcpuPrepareHostGDT(
	_In_ PVCPU Vcpu
);

NTSTATUS
VcpuPrepareHostIDT(
	_In_ PVCPU Vcpu
);

BOOLEAN
VcpuHandleHostException(
	_In_ PVCPU Vcpu,
	_In_ PVCPU_TRAP_FRAME TrapFrame
);

#endif
