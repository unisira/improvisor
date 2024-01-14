#include "vmcall.h"

// Information relevant to caching, reading or writing virtual addresses in an address space
typedef union _HYPERCALL_VIRT_EX 
{
	UINT64 Value;

	struct
	{
		UINT64 Pid : 32;
		UINT64 Size : 32;
	};
} HYPERCALL_VIRT_EX, *PHYPERCALL_VIRT_EX;

typedef union _HYPERCALL_SIGSCAN_BUFFER
{
	// The virtual address to start the scan from
	UINT64 Address;
	// The size of the pattern
	SIZE_T PatternSize;
	// Variable length array containing the pattern
	UCHAR Pattern[1];
} HYPERCALL_SIGSCAN_BUFFER, *PHYPERCALL_SIGSCAN_BUFFER;

typedef union _HYPERCALL_REMAP_PAGES_EX
{
	UINT64 Value;

	struct
	{
		UINT64 Size : 32;
		UINT64 Permissions : 8;
	};
} HYPERCALL_REMAP_PAGES_EX, *PHYPERCALL_REMAP_PAGES_EX;

typedef union _HYPERCALL_GET_LOGS_EX
{
	UINT64 Value;

	struct
	{
		UINT64 Count : 32;
	};
} HYPERCALL_GET_LOGS_EX, *PHYPERCALL_GET_LOGS_EX;

EXTERN_C
HYPERCALL_INFO
__vmcall(
	HYPERCALL_INFO HypercallInfo,
	UINT64 ExtHypercallInfo,
	PVOID BufferAddress,
	PVOID TargetAddress
);

// Hypercall system overview:
// System register  | Use
// -----------------|-------------------------------------------------------
// RAX              | HYPERCALL_INFO structure
// RBX              | extended hypercall info structure, hypercall dependant 
// RCX              | OPT: Buffer address 
// RDX              | OPT: Target address

HYPERCALL_RESULT
VmGetLogRecords(
	PVOID Dst,
	SIZE_T Count
)
{
	HYPERCALL_INFO Hypercall = {
		.Id = HYPERCALL_GET_LOG_RECORDS,
		.Result = HRESULT_SUCCESS
	};

	HYPERCALL_GET_LOGS_EX LogEx = {
		.Count = Count,
	};

	Hypercall = __vmcall(Hypercall, LogEx.Value, Dst, NULL);

	return Hypercall.Result;
}

HYPERCALL_RESULT
VmReadMemory(
    VM_PID Pid,
	PVOID Src,
	PVOID Dst,
	SIZE_T Size
)
{
	HYPERCALL_INFO Hypercall = {
		.Id = HYPERCALL_READ_VIRT,
		.Result = HRESULT_SUCCESS
	};

	HYPERCALL_VIRT_EX VirtEx = {
		.Pid = Pid,
		.Size = Size
	};

	Hypercall = __vmcall(Hypercall, VirtEx.Value, Dst, Src);

	return (HYPERCALL_RESULT)Hypercall.Result;
}

HYPERCALL_RESULT
VmWriteMemory(
    UINT16 Pid,
	PVOID Src,
	PVOID Dst,
	SIZE_T Size
)
{
	HYPERCALL_INFO Hypercall = {
		.Id = HYPERCALL_WRITE_VIRT,
		.Result = HRESULT_SUCCESS
	};

	HYPERCALL_VIRT_EX VirtEx = {
		.Pid = Pid,
		.Size = Size
	};

	Hypercall = __vmcall(Hypercall, VirtEx.Value, Src, Dst);

	return Hypercall.Result;
}

HYPERCALL_RESULT
VmOpenProcess(
	_In_ UINT64 Name,
	_Out_ PVM_PID Handle
)
/*++
Routine Description:
	Attempts to open a process given the FNV1A hash of its name 
--*/
{
	HYPERCALL_INFO Hypercall = {
		.Id = HYPERCALL_FIND_PROCESS,
		.Result = HRESULT_SUCCESS
	};

	Hypercall = __vmcall(Hypercall, 0, Name, Handle);

	return Hypercall.Result;
}

HYPERCALL_RESULT
VmGetActiveVpteCount(
    PSIZE_T VpteCount
)
/*++
Routine Description:
	Returns the amount of VPTEs currently being used by the VMM 
--*/
{
    HYPERCALL_INFO Hypercall = {
		.Id = HYPERCALL_GET_VPTE_COUNT,
		.Result = HRESULT_SUCCESS
	};

	Hypercall = __vmcall(Hypercall, 0, NULL, VpteCount);

	return Hypercall.Result;
}
