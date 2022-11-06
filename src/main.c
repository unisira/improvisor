#include <improvisor.h>
#include <vcpu/vmcall.h>
#include <vmm.h>

BOOLEAN gIsHypervisorRunning;

VOID
LogThreadEntry(PVOID A)
{
    CHAR Log[512] = { 0 };
    
    while (!gIsHypervisorRunning);

    LARGE_INTEGER Time = {
        .QuadPart = -500000
    };

#if 0
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
        NULL
    );

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

    // Hook PsSetLoadImageNotifyRoutine
    // Check for Capcom.sys [LDR_PARAMS::DriverName]
    // Cleanup Capcom.sys
    // CmCleanPDDBCache
    // CmCleanCiDllData
    // CmCleanUnloadedDrivers

    // TODO: 
    // Finish some important VM-exit handlers
    // Finish unloading - use a random CPUID leaf to detect hypervisor presence
    // Unload and free all resources, also set up the LDR config stuff
    // Virtualise interrupts and exceptions properly
    // Virtualise CR reads/writes (should be correct but check)
    // Virtualise all exiting instructions
    // 
    // C++ Rewrite has been postponed:
    // - It is an overengineered mess.
    // - Arrays are impossible to write nicely
    // C++ Rewrite will be the best thing ever

    return Status;
}
