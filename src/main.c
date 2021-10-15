#include "improvisor.h"
#include "vmm.h"

// Define for WINDBG usage & hot loading & unloading
#define _DEBUG

BOOLEAN gIsHypervisorRunning;

VOID
DriverUnload(
    IN PDRIVER_OBJECT DriverObject
)
{
    if (gIsHypervisorRunning)
        VmmShutdownHypervisor(NULL);
}

NTSTATUS 
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(RegistryPath);

#ifdef _DEBUG 
    // Set our driver unload routine so we can unload the driver for debugging
    DriverObject->DriverUnload = DriverUnload;
#endif

    Status = VmmStartHypervisor();
    if (NT_SUCCESS(Status))
        gIsHypervisorRunning = TRUE;

    return Status;
}
