#include <improvisor.h>
#include <arch/interrupt.h>
#include <arch/memory.h>
#include <vcpu/vmcall.h>
#include <mm/vpte.h>
#include <mm/mm.h>
#include <vmm.h>
#include <vmx.h>

// Information relevant to caching, reading or writing virtual addresses in an address space
typedef union _HYPERCALL_VIRT_EX 
{
	UINT64 Value;

	struct
	{
		UINT64 Cr3 : 16;
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

typedef enum _HYPERCALL_CR3_TARGET
{
	CR3_INVALID = 0,
	// Use the system CR3
	CR3_CACHE_SYSTEM,
	// Use the current value of GUEST_CR3 in the VMCS 
	CR3_CACHE_GUEST
} HYPERCALL_CR3_TARGET;

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
// RAX              | HYPERCALL_INFO structure
// RBX              | extended hypercall info structure, hypercall dependant 
// RCX              | OPT: Buffer address 
// RDX              | OPT: Target address

VMM_EVENT_STATUS
VmAbortHypercall(
	_In_ PHYPERCALL_INFO Hypercall,
	_In_ UINT16 Status
)
{
	Hypercall->Result = Status;
	return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VmGetProcessCr3(
	_In_ HYPERCALL_VIRT_EX VirtEx
)
{
	// TODO: Implement me
	return VMM_EVENT_CONTINUE;
}

VMM_EVENT_STATUS
VmGetCachedCr3(
	_In_ PVCPU Vcpu,
	_In_ HYPERCALL_VIRT_EX VirtEx,
	_Out_ PUINT64 Target
)
{
	// Invalid target addr...
	if (Target == NULL)
		return VMM_EVENT_ABORT;

	switch (VirtEx.Cr3)
	{
	case CR3_CACHE_GUEST: *Target = VmxRead(GUEST_CR3); break;
	case CR3_CACHE_SYSTEM: *Target = Vcpu->SystemDirectoryBase; break;
	case CR3_INVALID:
	{
		ImpLog("Invalid CR3 target...\n");
		return VMM_EVENT_ABORT;
	} break;
	}

	return VMM_EVENT_CONTINUE;
}

// TODO: Design better system for reading and writing processes

VMM_API
VMM_EVENT_STATUS
VmHandleHypercall(
	_In_ PVCPU Vcpu,
	_In_ PGUEST_STATE GuestState,
	_In_ PHYPERCALL_INFO Hypercall
)
{
	UINT64 GuestCr3 = VmxRead(GUEST_CR3);

	Hypercall->Result = HRESULT_SUCCESS;

	switch (Hypercall->Id) 
	{
	case HYPERCALL_READ_VIRT:
	{
		if (GuestState->Rdx == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);
		
		if (GuestState->Rcx == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);

		HYPERCALL_VIRT_EX VirtEx = {
			.Value = GuestState->Rbx
		};

		if (VirtEx.Cr3 == CR3_INVALID || VirtEx.Size == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_EXT_INFO);

		UINT64 Cr3 = 0;
		if (VmGetCachedCr3(Vcpu, VirtEx, &Cr3) != VMM_EVENT_CONTINUE)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_EXT_INFO);

		PMM_VPTE Vpte = NULL; 
		if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
			return VmAbortHypercall(Hypercall, HRESULT_INSUFFICIENT_RESOURCES);
		
		SIZE_T SizeRead = 0;
		while (VirtEx.Size > SizeRead)
		{
			// MaxReadable is the maximum amount of bytes before hitting a page boundary
			const SIZE_T MaxReadable = PAGE_SIZE - max(PAGE_OFFSET(GuestState->Rcx + SizeRead), PAGE_OFFSET(GuestState->Rdx + SizeRead));

			if (!NT_SUCCESS(MmMapGuestVirt(Vpte, GuestCr3, GuestState->Rcx + SizeRead)))
				return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);

			SIZE_T SizeToRead = VirtEx.Size - SizeRead > MaxReadable ? MaxReadable : VirtEx.Size - SizeRead;
			
			if (!NT_SUCCESS(MmReadGuestVirt(Cr3, GuestState->Rdx + SizeRead, SizeToRead, RVA_PTR(Vpte->MappedVirtAddr, SizeRead))))
				return VmAbortHypercall(Hypercall, HRESULT_INVALID_SIZE);
			
			SizeRead += SizeToRead;
		}
		   
		MmFreeVpte(Vpte);
	} break;
	case HYPERCALL_WRITE_VIRT: 
	{
		if (GuestState->Rdx == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);
		
		if (GuestState->Rcx == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);

		HYPERCALL_VIRT_EX VirtEx = {
			.Value = GuestState->Rbx
		};

		if (VirtEx.Cr3 == 0 || VirtEx.Size == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_EXT_INFO);

		UINT64 Cr3;
		if (VmGetCachedCr3(Vcpu, VirtEx, &Cr3) != VMM_EVENT_CONTINUE)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_EXT_INFO);

		PMM_VPTE Vpte = NULL;
		if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
			return VmAbortHypercall(Hypercall, HRESULT_INSUFFICIENT_RESOURCES);

		SIZE_T SizeWritten = 0;
		while (VirtEx.Size > SizeWritten)
		{
			// MaxReadable is the maximum amount of bytes before hitting a page boundary in Src or Dst
			const SIZE_T MaxWriteable = PAGE_SIZE - max(PAGE_OFFSET(GuestState->Rcx + SizeWritten), PAGE_OFFSET(GuestState->Rdx + SizeWritten));

			if (!NT_SUCCESS(MmMapGuestVirt(Vpte, GuestCr3, GuestState->Rcx + SizeWritten)))
				return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);
		
			SIZE_T SizeToWrite = VirtEx.Size - SizeWritten > MaxWriteable ? MaxWriteable : VirtEx.Size - SizeWritten;
			
			if (!NT_SUCCESS(MmWriteGuestVirt(Cr3, GuestState->Rdx + SizeWritten, SizeToWrite, RVA_PTR(Vpte->MappedVirtAddr, SizeWritten))))
				return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);

			SizeWritten += SizeToWrite;
		}

		MmFreeVpte(Vpte);
	} break;
	case HYPERCALL_VIRT_SIGSCAN: 
	{
		if (GuestState->Rcx == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_SIGSCAN_BUFFER);

		if (GuestState->Rdx == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);

		HYPERCALL_VIRT_EX VirtEx = {
			.Value = GuestState->Rbx
		};

		if (VirtEx.Cr3 == 0 || VirtEx.Size == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_EXT_INFO);

		UINT64 Cr3;
		if (VmGetCachedCr3(Vcpu, VirtEx, &Cr3) != VMM_EVENT_CONTINUE)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_EXT_INFO);

		PMM_VPTE Vpte = NULL;
		if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
			return VmAbortHypercall(Hypercall, HRESULT_INSUFFICIENT_RESOURCES);

		PMM_VPTE Vpte = NULL;
		if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
			return VmAbortHypercall(Hypercall, HRESULT_INSUFFICIENT_RESOURCES);

		// Map the signature scan buffer, we do this instead of copying it using MmReadGuestVirt as HYPERCALL_SIGSCAN_BUFFER is a variable-length struct
		if (!NT_SUCCESS(MmMapGuestVirt(Vpte, GuestCr3, GuestState->Rcx)))
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_SIGSCAN_BUFFER);

		PHYPERCALL_SIGSCAN_BUFFER SigScan = Vpte->MappedVirtAddr;

		SIZE_T SizeScanned = 0;
		while (SizeScanned < VirtEx.Size - SigScan->PatternSize)
		{
			// Temporary data holding the bytes currently being checked against our pattern
			// NOTE: This limites the pattern length to 128 or whatever size this array becomes
			UCHAR Data[128] = { 0 };

			// Read `HYPERCALL_SIGSCAN_BUFFER::PatternSize` bytes
			if (!NT_SUCCESS(MmReadGuestVirt(Cr3, SigScan->Address + SizeScanned, SigScan->PatternSize, Data)))
				return VmAbortHypercall(Hypercall, HRESULT_INVALID_SIZE);

			SIZE_T i = 0;
			for (i; i < SigScan->PatternSize; i++)
			{
				// If the current byte doesn't match and the current pattern byte isn't a wildcard, check the next address in the space
				if (Data[i] != SigScan->Pattern[i] && SigScan->Pattern[i] != 0xCC)
					break;
			}

			// If `i` == `HYPERCALL_SIGSCAN_BUFFER::PatternSize`, then we achieved a full match
			if (i == SigScan->PatternSize)
				break;
			else
				// Advance to the next byte and try again
				SizeScanned += 1;
		}

		// Write the result to the target variable
		if (!NT_SUCCESS(MmWriteGuestVirt(GuestCr3, GuestState->Rdx, sizeof(UINT64), SigScan->Address + SizeScanned)))
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);
	} break;
	case HYPERCALL_PHYS_TO_VIRT:
	{
		if (GuestState->Rcx == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);

		if (GuestState->Rdx == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);

		HYPERCALL_VIRT_EX VirtEx = {
			.Value = GuestState->Rbx
		};

		if (VirtEx.Cr3 == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_EXT_INFO);

		UINT64 Cr3;
		if (VmGetCachedCr3(Vcpu, VirtEx, &Cr3) != VMM_EVENT_CONTINUE)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_EXT_INFO);

		UINT64 Result = MmResolveGuestVirtAddr(Cr3, GuestState->Rdx);
		if (Result == -1)
			Hypercall->Result = HRESULT_INVALID_TARGET_ADDR;
		else
			if (!NT_SUCCESS(MmWriteGuestVirt(GuestCr3, GuestState->Rcx, sizeof(UINT64), Result)))
				return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);
	} break;
	case HYPERCALL_FIND_PROCESS:
	{
		PVOID Process = NULL;
	} break;
	case HYPERCALL_SHUTDOWN_VCPU:
	{
		Vcpu->Mode = VCPU_MODE_SHUTDOWN;

		// Write the current VCPU to GuestState->Rdx, the VCPU table are mapped as host memory,
		// but by the time this function returns VcpuLeaveVmx will have disabled EPT
		if (!NT_SUCCESS(MmWriteGuestVirt(GuestCr3, GuestState->Rdx, sizeof(PVCPU), Vcpu)))
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);
	} break;
	case HYPERCALL_GET_SYSTEM_CR3:
	{
		// TODO: Finish this, add signature parsing function & scanner 
	} break;
	case HYPERCALL_EPT_REMAP_PAGES:
	{
		HYPERCALL_REMAP_PAGES_EX RemapEx = {
			.Value = GuestState->Rbx
		};

		// Target (RCX) and Buffer (RDX) are used as GPA and PA respectively
		if (!NT_SUCCESS(
			EptMapMemoryRange(
				Vcpu->Vmm->Ept.Pml4,
				GuestState->Rcx,
				GuestState->Rdx,
				RemapEx.Size,
				RemapEx.Permissions)
			))
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_GUEST_PHYSADDR);

		EptInvalidateCache();
	} break;
	case HYPERCALL_HIDE_HOST_RESOURCES:
	{
		// Loop condition is not wrong, head is always the last one used, one is ignored at the end
		PIMP_ALLOC_RECORD CurrRecord = gHostAllocationsHead;
		while (CurrRecord != NULL)
		{
			// Only hide host allocations or memory allocations that aren't needed anymore
			if ((CurrRecord->Flags & (IMP_SHADOW_ALLOCATION | IMP_HOST_ALLOCATION)) == 0)
				goto skip;

			if (!NT_SUCCESS(
				EptMapMemoryRange(
					Vcpu->Vmm->Ept.Pml4,
					CurrRecord->PhysAddr,
					Vcpu->Vmm->Ept.DummyPagePhysAddr,
					CurrRecord->Size,
					EPT_PAGE_RW)
				))
				return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);

		skip:
			CurrRecord = (PIMP_ALLOC_RECORD)CurrRecord->Records.Blink;
		}

		EptInvalidateCache();
	} break;
	case HYPERCALL_ADD_LOG_RECORD:
	{
		if (GuestState->Rcx == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);

		PIMP_LOG_RECORD Log = NULL;
		if (!NT_SUCCESS(ImpAllocateLogRecord(&Log)))
			return VmAbortHypercall(Hypercall, HRESULT_LOG_RECORD_OVERFLOW);

		if (!NT_SUCCESS(MmReadGuestVirt(GuestCr3, GuestState->Rcx, IMP_LOG_SIZE, Log->Buffer)))
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);
	}
	case HYPERCALL_GET_LOG_RECORDS:
	{
		if (GuestState->Rcx == 0)
			return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);

		HYPERCALL_GET_LOGS_EX LogEx = {
			.Value = GuestState->Rbx
		};

		SIZE_T Count = 0;
		while (LogEx.Count > Count)
		{
			PIMP_LOG_RECORD CurrLog = NULL;
			if (!NT_SUCCESS(ImpRetrieveLogRecord(&CurrLog)))
				return VmAbortHypercall(Hypercall, HRESULT_LOG_RECORD_OVERFLOW);

			if (!NT_SUCCESS(MmWriteGuestVirt(GuestCr3, GuestState->Rcx + IMP_LOG_SIZE * Count, IMP_LOG_SIZE, CurrLog->Buffer)))
				return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);

			Count++;
		}
	} break;
	default:
		Hypercall->Result = HRESULT_UNKNOWN_HCID;
		VmxInjectEvent(EXCEPTION_UNDEFINED_OPCODE, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
		return VMM_EVENT_INTERRUPT;
	}

	return VMM_EVENT_CONTINUE;
}

HYPERCALL_RESULT
VmReadSystemMemory(
	_In_ PVOID Src,
	_In_ PVOID Dst,
	_In_ SIZE_T Size
)
{
	HYPERCALL_INFO Hypercall = {
		.Id = HYPERCALL_READ_VIRT,
		.Result = HRESULT_SUCCESS
	};

	HYPERCALL_VIRT_EX VirtEx = {
		.Cr3 = CR3_CACHE_SYSTEM,
		.Size = Size
	};

	Hypercall = __vmcall(Hypercall, VirtEx.Value, Dst, Src);

	return Hypercall.Result;
}

HYPERCALL_RESULT
VmWriteSystemMemory(
	_In_ PVOID Src,
	_In_ PVOID Dst,
	_In_ SIZE_T Size
)
{
	HYPERCALL_INFO Hypercall = {
		.Id = HYPERCALL_WRITE_VIRT,
		.Result = HRESULT_SUCCESS
	};

	HYPERCALL_VIRT_EX VirtEx = {
		.Cr3 = CR3_CACHE_SYSTEM,
		.Size = Size
	};

	Hypercall = __vmcall(Hypercall, VirtEx.Value, Src, Dst);

	return Hypercall.Result;
}

HYPERCALL_RESULT
VmShutdownVcpu(
	_Out_ PVCPU* pVcpu
)
/*++
Routine Description:
	Shuts down VMX operation and returns 
--*/
{
	HYPERCALL_INFO Hypercall = {
		.Id = HYPERCALL_SHUTDOWN_VCPU,
		.Result = HRESULT_SUCCESS
	};

	Hypercall = __vmcall(Hypercall, 0, NULL, pVcpu);

	return Hypercall.Result;
}

HYPERCALL_RESULT
VmEptRemapPages(
	_In_ UINT64 GuestPhysAddr,
	_In_ UINT64 PhysAddr,
	_In_ SIZE_T Size,
	_In_ EPT_PAGE_PERMISSIONS Permissions
)
{
	HYPERCALL_INFO Hypercall = {
		.Id = HYPERCALL_EPT_REMAP_PAGES,
		.Result = HRESULT_SUCCESS
	};

	HYPERCALL_REMAP_PAGES_EX RemapEx = {
		.Permissions = Permissions,
		.Size = Size
	};

	Hypercall = __vmcall(Hypercall, RemapEx.Value, (PVOID)GuestPhysAddr, (PVOID)PhysAddr);

	return Hypercall.Result;
}

HYPERCALL_RESULT
VmGetLogRecords(
	_In_ PVOID Dst,
	_In_ SIZE_T Count
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
VmAddLogRecord(
	_In_ PVOID Src
)
{
	HYPERCALL_INFO Hypercall = {
		.Id = HYPERCALL_ADD_LOG_RECORD,
		.Result = HRESULT_SUCCESS
	};

	Hypercall = __vmcall(Hypercall, 0, Src, NULL);

	return Hypercall.Result;
}