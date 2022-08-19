#include "improvisor.h"
#include "vmcall.h"
#include "vmm.h"

BOOLEAN gIsHypervisorRunning;

VOID
LogThreadEntry(VOID)
{
    CHAR Log[512] = { 0 };
    
    while (!gIsHypervisorRunning);

    LARGE_INTEGER Time = {
        .QuadPart = -500000
    };

#if 1
    while (TRUE)
    {
#if 1
        // Get one log from the hypervisor and print it
        HYPERCALL_RESULT HResult = VmGetLogRecords(Log, 1);
        if (HResult != HRESULT_SUCCESS)
            continue;

        ImpDebugPrint("LOG: %s", Log);
#else
        PIMP_LOG_RECORD Log = NULL;
        if (!NT_SUCCESS(ImpRetrieveLogRecord(&Log)))
            continue;

        ImpDebugPrint("%s", Log->Buffer);
#endif

        KeDelayExecutionThread(KernelMode, FALSE, &Time);
    }
#endif
}

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

    OBJECT_ATTRIBUTES ObjectAttributes;
    InitializeObjectAttributes(&ObjectAttributes,
        NULL,
        OBJ_KERNEL_HANDLE,
        NULL,
        NULL);

    HANDLE LogThread = NULL;
    Status = PsCreateSystemThread(
        &LogThread,
        THREAD_ALL_ACCESS,
        &ObjectAttributes,
        NULL,
        NULL,
        (PKSTART_ROUTINE)LogThreadEntry,
        NULL);
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to start log thread... (%X)\n", Status);
        return Status;
    }

    Status = VmmStartHypervisor();
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to launch VMM...(%X)\n", Status);
        return Status;
    }

    gIsHypervisorRunning = TRUE;

    return Status;
}
