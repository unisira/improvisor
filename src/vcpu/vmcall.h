#ifndef IMP_VMCALL_H
#define IMP_VMCALL_H

#include <vcpu/vcpu.h>
#include <ept.h>

// This bit is set if the error was something we can handle
#define HRESULT_MARKER (0x8000)

#define HRESULT_SUCCESS (HRESULT_MARKER | 0x100)
// Unknown hypercall ID
#define HRESULT_UNKNOWN_HCID (HRESULT_MARKER | 0x101) 
// Invalid target address (RDX) value
#define HRESULT_INVALID_TARGET_ADDR (HRESULT_MARKER | 0x102)
// Invalid size of virtual read
#define HRESULT_INVALID_SIZE (HRESULT_MARKER | 0x103)
// Invalid buffer address (RCX) value
#define HRESULT_INVALID_BUFFER_ADDR (HRESULT_MARKER | 0x104)
// Insufficient resources to complete the requested task
#define HRESULT_INSUFFICIENT_RESOURCES (HRESULT_MARKER | 0x105)
// The extended hypercall info (RBX) value was invalid 
#define HRESULT_INVALID_EXT_INFO (HRESULT_MARKER | 0x106)
// The amount of log records requested was more than we currently had.
#define HRESULT_LOG_RECORD_OVERFLOW (HRESULT_MARKER | 0x107)
// The given process handle was invalid
#define HRESULT_INVALID_PROCESS_HANDLE (HRESULT_MARKER | 0x108)
// An invalid signature scan buffer was supplied
#define HRESULT_INVALID_SIGSCAN_BUFFER (HRESULT_MARKER | 0x109)
// An invalid guest physical address was supplied
#define HRESULT_INVALID_GUEST_PHYSADDR (HRESULT_MARKER | 0x10A)

typedef ULONG HYPERCALL_RESULT; 

typedef union _HYPERCALL_INFO
{
	UINT64 Value;

	struct
	{
		UINT64 Id : 32;
		UINT64 Result: 16;
	};
} HYPERCALL_INFO, *PHYPERCALL_INFO; 

VMM_EVENT_STATUS
VmHandleHypercall(
	_In_ PVCPU Vcpu,
	_In_ PGUEST_STATE GuestState,
	_In_ PHYPERCALL_INFO Hypercall
);

//
// Hypercall implementations for kernel level code
//

HYPERCALL_RESULT
VmReadSystemMemory(
	_In_ PVOID Src,
	_In_ PVOID Dst,
	_In_ SIZE_T Size
);

HYPERCALL_RESULT
VmWriteSystemMemory(
	_In_ PVOID Src,
	_In_ PVOID Dst,
	_In_ SIZE_T Size
);

HYPERCALL_RESULT
VmShutdownVcpu(
	_Out_ PVCPU* pVcpu
);

HYPERCALL_RESULT
VmEptRemapPages(
	_In_ UINT64 GuestPhysAddr,
	_In_ UINT64 PhysAddr,
	_In_ SIZE_T Size,
	_In_ EPT_PAGE_PERMISSIONS Permissions
);

HYPERCALL_RESULT
VmGetLogRecords(
	_In_ PVOID Dst,
	_In_ SIZE_T Count
);

HYPERCALL_RESULT
VmAddLogRecord(
	_In_ PVOID Src
);

#endif
