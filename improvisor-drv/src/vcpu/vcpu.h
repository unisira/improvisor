#ifndef IMP_VCPU_H
#define IMP_VCPU_H

#include <improvisor.h>
#include <arch/interrupt.h>
#include <arch/segment.h>
#include <arch/cpu.h>
#include <vcpu/tsc.h>
#include <mm/mm.h>
#include <vmx.h>

// Emulation was successful, continue execution
#define VMM_EVENT_CONTINUE (0x00000000)
// An interrupt/exception was injected into the guest
#define VMM_EVENT_INTERRUPT (0x00000001)
// Retry execution of the current instruction
#define VMM_EVENT_RETRY (0x00000002)
// The hypervisor encountered an error and should shut down immediately
#define VMM_EVENT_ABORT (0x00000003)

#pragma pack(push, 1)
typedef struct _GUEST_STATE
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
	UINT64 Rip;
	UINT64 Cr8;
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
} GUEST_STATE, *PGUEST_STATE;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _VCPU_CONTEXT
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
	UINT64 Rip;
	UINT64 Rsp;
	UINT64 RFlags;
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
} VCPU_CONTEXT, *PVCPU_CONTEXT;
#pragma pack(pop)

// Base of other VCPU delegates so Status can be generically accessed
typedef struct _VCPU_DELEGATE_PARAMS
{
	NTSTATUS Status;
} VCPU_DELEGATE_PARAMS, *PVCPU_DELEGATE_PARAMS;

typedef struct _VCPU_SPAWN_PARAMS
{
	NTSTATUS Status;
	struct _VMM_CONTEXT* VmmContext;
	ULONG ActiveVcpuCount;
	ULONG FailedCoreMask;
} VCPU_SPAWN_PARAMS, *PVCPU_SPAWN_PARAMS;

typedef struct _VCPU_SHUTDOWN_PARAMS
{
	NTSTATUS Status;
	struct _VMM_CONTEXT* VmmContext;
	ULONG FailedCoreMask;
} VCPU_SHUTDOWN_PARAMS, *PVCPU_SHUTDOWN_PARAMS;

typedef union _VCPU_STACK
{
	UINT8 Data[0x6000];

	struct 
	{
		struct _VCPU* Vcpu;
	} Cache;
} VCPU_STACK, *PVCPU_STACK;

typedef enum _MTF_EVENT_TYPE
{
	MTF_EVENT_MEASURE_VMENTRY,
	MTF_EVENT_RESET_EPT_PERMISSIONS
} MTF_EVENT_TYPE, *PMTF_EVENT_TYPE;

typedef struct _MTF_EVENT
{
	MTF_EVENT_TYPE Type;

	union
	{
		// RESET_EPT_PERMISSIONS
		struct
		{
			UINT64 GuestPhysAddr;
			UINT64 PhysAddr;
			UINT64 Permissions;
		};
	};
} MTF_EVENT, *PMTF_EVENT;

typedef struct _MTF_EVENT_ENTRY
{
	LIST_ENTRY Links;
	MTF_EVENT Event;
	BOOLEAN Valid;
} MTF_EVENT_ENTRY, *PMTF_EVENT_ENTRY;

typedef enum _VCPU_MODE
{
	VCPU_MODE_INACTIVE,
	VCPU_MODE_LAUNCHED,
	VCPU_MODE_GUEST,
	VCPU_MODE_HOST,
	VCPU_MODE_SHUTDOWN
} VCPU_MODE, *PVCPU_MODE;

typedef struct _VCPU
{
	PVOID Self;
	UINT8 Id;
	PCHAR MsrBitmap;
	RTL_BITMAP MsrLoReadBitmap;
	RTL_BITMAP MsrHiReadBitmap;
	RTL_BITMAP MsrLoWriteBitmap;
	RTL_BITMAP MsrHiWriteBitmap;
	PVCPU_STACK Stack;
	PVMX_REGION Vmcs;
	PVMX_REGION Vmxon;
	PX86_INTERRUPT_TRAP_GATE Idt;
	PX86_SEGMENT_DESCRIPTOR Gdt;
	PX86_TASK_STATE_SEGMENT Tss;
	UINT64 VmcsPhysical;
	UINT64 VmxonPhysical;
	UINT64 MsrBitmapPhysical;
	UINT64 Cr0ShadowableBits;
	UINT64 Cr4ShadowableBits;
	UINT64 SystemDirectoryBase;
	VCPU_MODE Mode;
	BOOLEAN IsUnrestrictedGuest;
	CPU_STATE LaunchState;
	VMX_STATE Vmx;
	TSC_STATUS Tsc;
	PMTF_EVENT_ENTRY MtfStackHead;
	ULONG LastHypercallResult;
	ULONG NumQueuedNMIs;
	BOOLEAN NMIsBlocked;
	struct _VMM_CONTEXT* Vmm; 
} VCPU, *PVCPU;

typedef enum _MSR_ACCESS
{
	MSR_READ,
	MSR_WRITE
} MSR_ACCESS, *PMSR_ACCESS;

typedef ULONG VMM_EVENT_STATUS;

EXTERN_C
PVCPU
__current_vcpu(VOID);

EXTERN_C
BOOLEAN
__vcpu_is_virtualised(VOID);

NTSTATUS 
VcpuSetup(
	_Inout_ PVCPU Vcpu,
	_In_ UINT8 Id
);

PVCPU
VcpuGetActiveVcpu(VOID);

BOOLEAN
VcpuIsGuest64Bit(
	_In_ PVCPU Vcpu
);

UCHAR
VcpuGetGuestCPL(
	_In_ PVCPU Vcpu
);

VOID
VcpuCacheCpuFeatures(
	_Inout_ PVCPU Vcpu
);

VOID
VcpuSpawnPerCpu(
	_Inout_ PVCPU_SPAWN_PARAMS Params
);

VOID
VcpuShutdownPerCpu(
	_Inout_ PVCPU_SHUTDOWN_PARAMS Params
);

VOID
VcpuShutdownVmx(
	_Inout_ PVCPU Vcpu,
	_Inout_ PCPU_STATE CpuState
);

VOID
VcpuPushMTFEventEx(
	_In_ PVCPU Vcpu,
	_In_ MTF_EVENT Event
);

VOID
VcpuPushMTFEvent(
	_In_ PVCPU Vcpu,
	_In_ MTF_EVENT_TYPE Event
);

VOID
VcpuPushPendingMTFEvent(
	_In_ PVCPU Vcpu,
	_In_ MTF_EVENT_TYPE Event
);

BOOLEAN
VcpuPopMTFEvent(
	_In_ PVCPU Vcpu,
	_Out_ PMTF_EVENT Event
);

VOID
VcpuSetControl(
	_Inout_ PVCPU Vcpu,
	_In_ VMX_CONTROL Control,
	_In_ BOOLEAN State
);

BOOLEAN
VcpuIsControlSupported(
	_Inout_ PVCPU Vcpu,
	_In_ VMX_CONTROL Control
);

VOID
VcpuCommitVmxState(
	_Inout_ PVCPU Vcpu
);

VOID
VcpuToggleExitOnMsr(
	_Inout_ PVCPU Vcpu,
	_In_ UINT32 Msr,
	_In_ MSR_ACCESS Access
);

VOID
VcpuDestroy(
	_Inout_ PVCPU Vcpu
);

NTSTATUS
VcpuPostSpawnInitialisation(
	_Inout_ PVCPU Vcpu
);

UINT64
VcpuGetVmExitStoreValue(
	_In_ UINT32 Index
);

NTSTATUS
VcpuLoadPDPTRs(
	_In_ PVCPU Vcpu
);

VOID
VcpuRecoverNMIBlocking(
	_In_ PVCPU Vcpu
);

VOID
VcpuEnableTscSpoofing(
	_Inout_ PVCPU Vcpu
);

UINT64
VcpuGetTscEventLatency(
	_In_ PVCPU Vcpu,
	_In_ TSC_EVENT_TYPE Type
);

VOID
VcpuUpdateLastTscEventEntry(
	_Inout_ PVCPU Vcpu,
	_In_ TSC_EVENT_TYPE Type
);

#endif

