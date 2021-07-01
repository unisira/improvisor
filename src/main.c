#include "improvisor.h"
#include "vmm.h"

// Define for WINDBG usage & hot loading & unloading
#define _DEBUG

VOID
DriverUnload(
    IN PDRIVER_OBJECT DriverObject
)
{
    VmmShutdownHypervisor();
}

NTSTATUS 
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
)
{
    UNREFERENCED_PARAMETER(RegistryPath);

#ifdef _DEBUG 
    // Set our driver unload routine so we can unload the driver for debugging
    DriverObject->DriverUnload = DriverUnload;
#endif

    return VmmStartHypervisor();
}
