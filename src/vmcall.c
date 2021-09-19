#include "arch/interrupt.h"
#include "vmcall.h"
#include "vmx.h"
#include "mm.h"

#define HRESULT_SUCCESS (0x8100)
// Unknown hypercall ID
#define HRESULT_UNKNOWN_HCID (0x8101) 
// Invalid target address (RDX) value
#define HRESULT_INVALID_TARGET_ADDR (0x8102)
// Invalid buffer address (RCX) value
#define HRESULT_INVALID_BUFFER_ADDR (0x8103)
// Insufficient resources to complete the requested task
#define HRESULT_INSUFFICIENT_RESOURCES (0x8104)
// The extended hypercall info (RBX) value was invalid 
#define HRESULT_INVALID_EXT_INFO (0x8105)

typedef struct 

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
        UINT64 
    };
} HYPERCALL_SIGSCAN_EX, *PHYPERCALL_SIGSCAN_EX;

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
} HYPERCALL_ID, PHYPERCALL_ID;

// Hypercall system overview:
// System register  | Use
// RAX              | HYPERCALL_INFO structure
// RBX              | extended hypercall info structure, hypercall dependant 
// RCX              | OPT: Buffer address 
// RDX              | OPT: Target address

NTSTATUS
VmCreateTranslationCache(
    _In_ SIZE_T Count
)
/*++
Routine Description:
    Creates a list of address translation cache entries used to speed up reads/writes to common locations
--*/
{
    // TODO Future: Finish this, could improve performance big time
}

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

        Hypercall->Result = HRESULT_SUCCESS; 
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

        Hypercall->Result = HRESULT_SUCCESS;
    } break;
    case HYPERCALL_VIRT_SIGSCAN: 
    {
        // TODO: Finish this, add signature parsing function & scanner 
    } break;
    case HYPERCALL_SHUTDOWN_VCPU:
    {
        Vcpu->IsShuttingDown = TRUE;
    } break;
    default:
        Hypercall->Result = HRESULT_UNKNOWN_HCID;
        VmxInjectEvent(EXCEPTION_UNDEFINED_OPCODE, INTERRUPT_TYPE_HARDWARE_EXCEPTION, 0);
        return VMM_EVENT_INTERRUPT;
    }

    return VMM_EVENT_CONTINUE;
}
