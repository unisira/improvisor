#ifndef IMP_CPU_H
#define IMP_CPU_H

#include <ntdef.h>

#include <arch/segment.h>

// Unaligned 128-bit integer type
typedef struct _M128
{
	UINT64 Low;
	UINT64 High;
} M128, *PM128;

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
	UINT16 Cs;
	UINT16 Ss;
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

EXTERN_C
VOID
__sgdt(PX86_PSEUDO_DESCRIPTOR);

EXTERN_C
VOID
__lgdt(PX86_PSEUDO_DESCRIPTOR);

EXTERN_C
UINT64
__segmentar(X86_SEGMENT_SELECTOR);

EXTERN_C
UINT16
__readldt(VOID);

EXTERN_C
VOID
__writeldt(UINT16);

EXTERN_C
UINT16
__readtr(VOID);

EXTERN_C
VOID
__writetr(UINT16);

EXTERN_C
UINT16
__readcs(VOID);

EXTERN_C
VOID
__writecs(UINT16);

EXTERN_C
UINT16
__readss(VOID);

EXTERN_C
VOID
__writess(UINT16);

EXTERN_C
UINT16
__readds(VOID);

EXTERN_C
VOID
__writeds(UINT16);

EXTERN_C
UINT16
__reades(VOID);

EXTERN_C
VOID
__writees(UINT16);

EXTERN_C
UINT16
__readfs(VOID);

EXTERN_C
VOID
__writefs(UINT16);

EXTERN_C
UINT16
__readgs(VOID);

EXTERN_C
VOID
__writegs(UINT16);

EXTERN_C
VOID
__enable(VOID);

EXTERN_C
VOID
__disable(VOID);

EXTERN_C
VOID
__writecr2(UINT64);

EXTERN_C
VOID
__invept(UINT64, PVOID);

EXTERN_C
VOID
__invvpid(UINT64, PVOID);

EXTERN_C
VOID
__invd(VOID);

EXTERN_C
VOID
__invlpg(PVOID);

#endif
