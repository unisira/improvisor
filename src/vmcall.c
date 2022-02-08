#include "arch/interrupt.h"
#include "vmcall.h"
#include "vmx.h"
#include "vmm.h"
#include "mm.h"

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

typedef union _HYERPCALL_SIGSCAN_EX
{
    UINT64 Value;

    struct
    {
        UINT64 Cr3 : 16;
        UINT64 Size : 32;
    };
} HYPERCALL_SIGSCAN_EX, *PHYPERCALL_SIGSCAN_EX;

typedef union _HYPERCALL_REMAP_PAGES_EX
{
    UINT64 Value;

    struct
    {
        UINT64 Size : 32;
        UINT64 Permissions : 8;
    };
} HYPERCALL_REMAP_PAGES_EX, *PHYPERCALL_REMAP_PAGES_EX;

typedef enum _HYPERCALL_ID
{
    // Read a guest virtual address, using translations from a specified process's address space
    HYPERCALL_READ_VIRT = 0x56504D49 /* 'IMPV' */,
    // Read a guest virtual address, using translations from a specified process's address space
    HYPERCALL_WRITE_VIRT,
    // Scan for a byte signature inside of a virtual address range using translations specified
    HYPERCALL_VIRT_SIGSCAN,
    // Shutdown the current VCPU and free its resources
    HYPERCALL_SHUTDOWN_VCPU,
    // Returns the value of the system CR3 in RAX
    HYPERCALL_GET_SYSTEM_CR3,
    // Remap a GPA to a virtual address passed
    HYPERCALL_EPT_REMAP_PAGES,
    // Send a PDB to be used by the hypervisor
    HYPERCALL_PDB_BUFFER,
    // Get the last HYPERCALL_RESULT value
    HYPERCALL_GET_LAST_RESULT
} HYPERCALL_ID, PHYPERCALL_ID;

typedef enum _HYPERCALL_CACHED_CR3_TARGET
{
    INVALID = 0,
    // Use the system CR3
    SYSTEM,
    // Use the current value of GUEST_CR3 in the VMCS 
    GUEST
} HYPERCALL_CACHED_CR3_TARGET;

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

        if (VirtEx.Cr3 == 0 || VirtEx.Size == 0)
            return VmAbortHypercall(Hypercall, HRESULT_INVALID_EXT_INFO);

        PMM_VPTE Vpte = NULL; 
        if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
            return VmAbortHypercall(Hypercall, HRESULT_INSUFFICIENT_RESOURCES);
        
        SIZE_T SizeRead = 0;
        while (VirtEx.Size > SizeRead)
        {
            if (!NT_SUCCESS(MmMapGuestVirt(Vpte, GuestCr3, GuestState->Rcx)))
                return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);

            SIZE_T SizeToRead = VirtEx.Size - SizeRead > PAGE_SIZE ? PAGE_SIZE : VirtEx.Size - SizeRead;
            if (!NT_SUCCESS(MmReadGuestVirt(VirtEx.Cr3, GuestState->Rdx + SizeRead, SizeToRead, (PCHAR)Vpte->MappedVirtAddr + SizeRead)))
                return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);

            SizeRead += PAGE_SIZE;
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

        PMM_VPTE Vpte = NULL;
        if (!NT_SUCCESS(MmAllocateVpte(&Vpte)))
            return VmAbortHypercall(Hypercall, HRESULT_INSUFFICIENT_RESOURCES);

        SIZE_T SizeWritten = 0;
        while (VirtEx.Size > SizeWritten)
        {
            if (!NT_SUCCESS(MmMapGuestVirt(Vpte, GuestCr3, GuestState->Rcx)))
                return VmAbortHypercall(Hypercall, HRESULT_INVALID_BUFFER_ADDR);
        
            SIZE_T SizeToWrite = VirtEx.Size - SizeWritten > PAGE_SIZE ? PAGE_SIZE : VirtEx.Size - SizeWritten;
            if (!NT_SUCCESS(MmWriteGuestVirt(VirtEx.Cr3, GuestState->Rdx + SizeWritten, SizeToWrite, (PCHAR)Vpte->MappedVirtAddr + SizeWritten)))
                return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);

            SizeWritten += PAGE_SIZE;
        }

        MmFreeVpte(Vpte);
    } break;
    case HYPERCALL_VIRT_SIGSCAN: 
    {
        // TODO: Finish this, add signature parsing function & scanner 
    } break;
    case HYPERCALL_SHUTDOWN_VCPU:
    {
        Vcpu->IsShuttingDown = TRUE;

        // Write the current VCPU to GuestState->Rdx, the VCPU table are mapped as host memory,
        // but by the time this function returns VcpuLeaveVmx will have disabled EPT
        if (!NT_SUCCESS(MmWriteGuestVirt(GuestCr3, GuestState->Rdx, sizeof(PVCPU), Vcpu)))
            return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);
    } break;
    case HYPERCALL_EPT_REMAP_PAGES:
    {
        HYPERCALL_REMAP_PAGES_EX RemapEx = {
            .Value = GuestState->Rbx
        };

        // Target (RCX) and Buffer (RDX) are used as GPA and PA respectively
        if (!NT_SUCCESS(
            EptMapMemoryRange(
                Vcpu->Vmm->EptInformation.SystemPml4,
                GuestState->Rcx,
                GuestState->Rdx,
                RemapEx.Size,
                RemapEx.Permissions)
            ))
            return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);
    } break;
    case HYPERCALL_GET_LAST_RESULT:
    {
        if (!NT_SUCCESS(MmWriteGuestVirt(GuestCr3, GuestState->Rdx, sizeof(PVCPU), Vcpu)))
            return VmAbortHypercall(Hypercall, HRESULT_INVALID_TARGET_ADDR);
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
    return HRESULT_SUCCESS;
}

HYPERCALL_RESULT
VmWriteSystemMemory(
    _In_ PVOID Src,
    _In_ PVOID Dst,
    _In_ SIZE_T Size
)
{
    return HRESULT_SUCCESS;
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
VmGetLastResult(
    _Out_ HYPERCALL_RESULT* pResult
)
/*++
Routine Description:
    Writes the last hypercall result value in `pResult`
--*/
{
    HYPERCALL_INFO Hypercall = {
        .Id = HYPERCALL_GET_LAST_RESULT,
        .Result = HRESULT_SUCCESS
    };

    Hypercall = __vmcall(Hypercall, 0, NULL, pResult);

    return Hypercall.Result;
}