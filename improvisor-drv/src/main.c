#include <improvisor.h>
#include <vcpu/vmcall.h>
#include <vmm.h>

BOOLEAN gIsHypervisorRunning;

VOID
LogThreadEntry(PVOID A)
{
    // Align this to PAGE_SIZE to avoid it crossing page boundaries on the stack 
    // NOTE: The stack can become paged out, how should I do this instead?
    // Allocate some storage? I'd rather not greatly reduce the amount of time taken
    // for each hypercall to be sent, should I allocate a piece of storage per-hypercall?
    // that is mighty smart sire (it isn't)
    PCHAR Log = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, 'DBG');
    //DECLSPEC_ALIGN(PAGE_SIZE) CHAR Log[512] = { 0 };

    // `Log` might need to be touched to ensure it is paged, IDK..... See MiSetNonPagedPoolNoSteal

    while (!gIsHypervisorRunning)
        _mm_pause();

    LARGE_INTEGER Time = {
        .QuadPart = -500
    };

    while (gIsHypervisorRunning)
    {
        // Get one log from the hypervisor and print it
        HYPERCALL_RESULT HResult = VmGetLogRecords(Log, 1);
        if (HResult != HRESULT_SUCCESS)
        {
            // ImpDebugPrint("VmGetLogRecords failed: %X\n", HResult);
            // Skip to the wait
            goto delay;
        }

        ImpDebugPrint("LOG: %s", Log);

#if 0
        PVOID Process = NULL;
        // Try to get pointer to current process
        HResult = VmOpenProcess(0, &Process);
        if (HResult != HRESULT_SUCCESS)
        {
            ImpDebugPrint("VmOpenProcess failed: %X\n", HResult);
            // Skip to the wait
            continue;
        }

        ImpDebugPrint("Current process: %p\n", Process);
#endif

delay:
        KeDelayExecutionThread(KernelMode, FALSE, &Time);
    }

    ImpDebugPrint("Exiting logger thread..\n");
}

VOID
DriverUnload(
    IN PDRIVER_OBJECT DriverObject
)
{
    if (gIsHypervisorRunning)
        VmmShutdownHypervisor();

    ImpDebugPrint("Done shutting down the hypervisor, Bye\n");

    LARGE_INTEGER Time = {
        .QuadPart = -5000000
    };

    KeDelayExecutionThread(KernelMode, FALSE, &Time);
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

#if 0
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
#endif

    Status = VmmStartHypervisor();
    if (!NT_SUCCESS(Status))
    {
        ImpDebugPrint("Failed to launch VMM...(%X)\n", Status);
        return Status;
    }

    gIsHypervisorRunning = TRUE;

    LARGE_INTEGER Time = {
        .QuadPart = -5 * 1000 * 1000
    };

    KeDelayExecutionThread(KernelMode, FALSE, &Time);

    VmmShutdownHypervisor();

    ImpDebugPrint("Done shutting down the hypervisor (%i), Bye\n", __vcpu_is_virtualised());

    DbgBreakPoint();

    // Hook PsSetLoadImageNotifyRoutine
    // Check for Gdrv.sys [LDR_PARAMS::DriverName]
    // Cleanup Gdrv.sys
    // GdCleanPDDBCache
    // GdCleanCiDllData
    // GdCleanUnloadedDrivers
 
    // TODO: 
    // Finish some important VM-exit handlers
    // Finish unloading - use a random CPUID leaf to detect hypervisor presence
    // Unload and free all resources, also set up the LDR config stuff
    // Virtualise interrupts and exceptions properly
    // Virtualise CR reads/writes (should be correct but check)
    // Virtualise all exiting instructions
    // Write tests for all VM-exits and run them when in debug mode

    return Status;
}
