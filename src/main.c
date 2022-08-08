#include "improvisor.h"
#include "vmm.h"

BOOLEAN gIsHypervisorRunning;

VOID
DriverUnload(
    IN PDRIVER_OBJECT DriverObject
)
{
    if (gIsHypervisorRunning)
        VmmShutdownHypervisor();
}

NTSTATUS 
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING SharedObjectName
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(SharedObjectName);

#ifdef _DEBUG 
    // Set our driver unload routine so we can unload the driver for debugging
    DriverObject->DriverUnload = DriverUnload;
#endif

    Status = VmmStartHypervisor();
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to launch VMM...(%X)\n", Status);
        return Status;
    }

    gIsHypervisorRunning = TRUE;

    return Status;
}
