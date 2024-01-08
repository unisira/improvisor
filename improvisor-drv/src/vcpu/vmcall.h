#ifndef IMP_VMCALL_H
#define IMP_VMCALL_H

#include <vcpu/vcpu.h>
#include <hash.h>
#include <ept.h>

// System PID is always 4, so no need to do vm_open on System
#define VM_SYSTEM_PID (4U)
// Current PID is aliased as -1
#define VM_CURRENT_PID (-1U)

// This bit is set if the error was something we can handle
#define HRESULT_MARKER (0x8000)

#define HRESULT_SUCCESS (HRESULT_MARKER | 0x100)
// Unknown hypercall ID
#define HRESULT_UNKNOWN_HCID (HRESULT_MARKER | 0x101) 
// Invalid target address (RDX) value
#define HRESULT_INVALID_DESTINATION_ADDR (HRESULT_MARKER | 0x102)
// Invalid size of virtual read
#define HRESULT_INVALID_SIZE (HRESULT_MARKER | 0x103)
// Invalid buffer address (RCX) value
#define HRESULT_INVALID_SOURCE_ADDR (HRESULT_MARKER | 0x104)
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

typedef enum _HYPERCALL_ID
{
	// Read a guest virtual address, using translations from a specified process's address space
	HYPERCALL_READ_VIRT = 0x56504D49 /* 'IMPV' */,
	// Read a guest virtual address, using translations from a specified process's address space
	HYPERCALL_WRITE_VIRT,
	// Scan for a byte signature inside of a virtual address range using translations specified
	HYPERCALL_VIRT_SIGSCAN,
	// Convert a physical address into a virtual address in a specific address space
	HYPERCALL_PHYS_TO_VIRT,
	// Lookup a process by name using a FNV1a hash, returns a handle to the process which can be used for reading/writing
	HYPERCALL_FIND_PROCESS,
	// Shutdown the current VCPU and free its resources
	HYPERCALL_SHUTDOWN_VCPU,
	// Returns the value of the system CR3 in RAX
	HYPERCALL_GET_SYSTEM_CR3,
	// Remap a GPA to a virtual address passed
	HYPERCALL_EPT_REMAP_PAGES,
	// Hide all host resource allocations from guest physical memory
	HYPERCALL_HIDE_HOST_RESOURCES,
	// Create a hidden translation to a given physical address
	HYPERCALL_MAP_HIDDEN_MEMORY,
	// Add a log record to the VMM
	HYPERCALL_ADD_LOG_RECORD,
	// Retrieve log records from VMM
	HYPERCALL_GET_LOG_RECORDS
} HYPERCALL_ID, PHYPERCALL_ID;

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
// Hypercall implementations
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
VmOpenProcess(
	_In_ UINT64 Name,
	_Out_ PVOID* Handle
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
