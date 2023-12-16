#include <improvisor.h>
#include <vcpu/vmcall.h>
#include <os/input.h>
#include <detour.h>
#include <macro.h>

INPUT_CONTEXT gInputContext;

NTSTATUS
InpInit(VOID)
{
	NTSTATUS Status;
	PDRIVER_OBJECT pDriverObject = NULL;
	UNICODE_STRING DriverName;

	RtlInitUnicodeString(&DriverName, L"\\Driver\\mouhid");

	Status = ObReferenceObjectByName(&DriverName,
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
		NULL,
		0,
		*IoDriverObjectType,
		KernelMode,
		NULL,
		&pDriverObject);

	if (!NT_SUCCESS(Status))
	{
		ImpLog("InpInit: ObReferenceObjectByName failed with 0x%x\n", Status);
		return Status;
	}

	InpInstallPnPNotificationCallback(pDriverObject, InpPnPNotificationCallback);

	gInputContext.MouHidInfo.DriverObject = pDriverObject;

	Status = InpCollectHidDeviceInfo(pDriverObject, MOUHID_CONNECT_DATA_OFFSET, &gInputContext.MouHidInfo);

	if (!NT_SUCCESS(Status))
	{
		ImpLog("InpCollectHidDeviceInfo: InpCollectHidDeviceInfo failed.\n");
		goto exit;
	}

	InpInstallServiceCallbackHooks(&gInputContext.MouHidInfo, &InpMouHidServiceCallbackHook);

	RtlInitUnicodeString(&DriverName, L"\\Driver\\kbdhid");

	Status = ObReferenceObjectByName(&DriverName,
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
		NULL,
		0,
		*IoDriverObjectType,
		KernelMode,
		NULL,
		&pDriverObject);

	if (!NT_SUCCESS(Status))
	{
		ImpLog("InpInit: ObReferenceObjectByName failed with 0x%x\n", Status);
		goto exit;
	}

	gInputContext.KbdHidInfo.DriverObject = pDriverObject;

	Status = InpCollectHidDeviceInfo(pDriverObject, KBDHID_CONNECT_DATA_OFFSET, &gInputContext.KbdHidInfo);

	if (!NT_SUCCESS(Status))
	{
		ImpLog("InpCollectHidDeviceInfo: InpCollectHidDeviceInfo failed.\n");
		goto exit;
	}

	InpInstallServiceCallbackHooks(&gInputContext.KbdHidInfo, &InpKbdHidServiceCallbackHook);

exit:
	if (!NT_SUCCESS(Status))
	{
		InpFreeHidDriverInfo(&gInputContext.MouHidInfo);
		InpFreeHidDriverInfo(&gInputContext.KbdHidInfo);
	}

	return Status;
}

NTSTATUS
InpInstallPnPNotificationCallback(
	IN PDRIVER_OBJECT pDriverObject,
	IN PDRIVER_NOTIFICATION_CALLBACK_ROUTINE pNotificationRoutine
)
{
	return IoRegisterPlugPlayNotification(
		EventCategoryDeviceInterfaceChange,
		0,
		(PVOID)&GUID_DEVINTERFACE_MOUSE,
		pDriverObject,
		pNotificationRoutine,
		NULL,
		&gInputContext.PnpNotifEntry);
}

NTSTATUS
InpCollectHidDeviceInfo(
	IN PDRIVER_OBJECT pDriverObject,
	IN ULONG64 uConnectDataOffset,
	OUT PHIDDRIVER_INFO pHidDriverInfo
)
{
	NTSTATUS Status;
	ULONG uDeviceObjectCount = 0;
	PDEVICE_OBJECT* pDeviceObjectList = NULL;

	Status = IoEnumerateDeviceObjectList(pDriverObject, NULL, 0, &uDeviceObjectCount);

	if (!NT_SUCCESS(Status) && Status != 0xC0000023)
	{
		ImpLog("InpCollectHidDeviceInfo: IoEnumerateDeviceObjectList failed with 0x%x\n", Status);
		goto exit;
	}

	if (uDeviceObjectCount != 0)
	{
		pDeviceObjectList = (PDEVICE_OBJECT*)ExAllocatePoolWithTag(NonPagedPool, uDeviceObjectCount * sizeof(PDEVICE_OBJECT), 'tsuR');

		if (pDeviceObjectList == NULL)
		{
			ImpLog("InpCollectHidDeviceInfo: Failed to allocate device object list.\n");
			Status = STATUS_INSUFFICIENT_RESOURCES;
			goto exit;
		}

		Status = IoEnumerateDeviceObjectList(pDriverObject, pDeviceObjectList, uDeviceObjectCount * sizeof(PDEVICE_OBJECT), &uDeviceObjectCount);

		if (!NT_SUCCESS(Status))
		{
			ImpLog("InpCollectHidDeviceInfo: IoEnumerateDeviceObjectList failed with 0x%x\n", Status);
			goto exit;
		}

		pHidDriverInfo->DeviceCount = uDeviceObjectCount;
		pHidDriverInfo->DeviceInfo = (PHIDDEVICE_INFO)ExAllocatePoolWithTag(NonPagedPool, uDeviceObjectCount * sizeof(HIDDEVICE_INFO), 'tsuR');

		if (pHidDriverInfo->DeviceInfo == NULL)
		{
			ImpLog("InpCollectHidDeviceInfo: Failed to allocate device info list.\n");
			Status = STATUS_INSUFFICIENT_RESOURCES;
			goto exit;
		}

		for (ULONG i = 0; i < uDeviceObjectCount; i++)
		{
			PHIDDEVICE_INFO pCurDeviceInfo = &pHidDriverInfo->DeviceInfo[i];
			PDEVICE_OBJECT pCurDeviceObject = pDeviceObjectList[i];

			pCurDeviceInfo->ConnectData = RVA_PTR(pCurDeviceObject->DeviceExtension, uConnectDataOffset);

			ObDereferenceObject(pCurDeviceObject);
		}
	}
	else
	{
		ImpLog("InpCollectHidDeviceInfo: Device count is 0.\n");
	}

exit:
	if (pDeviceObjectList)
	{
		ExFreePoolWithTag(pDeviceObjectList, POOL_TAG);
	}

	if (!NT_SUCCESS(Status))
	{
		if (pHidDriverInfo->DeviceInfo)
		{
			ExFreePoolWithTag(pHidDriverInfo->DeviceInfo, 'tsuR');
			pHidDriverInfo->DeviceInfo = NULL;
		}
	}

	return Status;
}

VOID
InpFreeHidDriverInfo(
	IN PHIDDRIVER_INFO pHidDriverInfo
)
/*++
Routine Description:
	Invalidates everything inside of a HIDDRIVER_INFO structure.
--*/
{
	InpUninstallServiceCallbackHooks(pHidDriverInfo);

	if (pHidDriverInfo->DriverObject)
	{
		ObDereferenceObject(pHidDriverInfo->DriverObject);
		pHidDriverInfo->DriverObject = NULL;
	}
}

VOID
InpInstallServiceCallbackHooks(
	IN PHIDDRIVER_INFO pHidDriverInfo,
	IN PSERVICE_CALLBACK_ROUTINE pServiceCallbackRoutine
)
{
	for (ULONG i = 0; i < pHidDriverInfo->DeviceCount; i++)
	{
		PHIDDEVICE_INFO pCurDeviceInfo = &pHidDriverInfo->DeviceInfo[i];

		pCurDeviceInfo->OriginalServiceRoutine = (PSERVICE_CALLBACK_ROUTINE)InterlockedExchangePointer(&pCurDeviceInfo->ConnectData->ClassService, (PVOID)pServiceCallbackRoutine);
	}
}

VOID
InpUninstallServiceCallbackHooks(
	IN PHIDDRIVER_INFO pHidDriverInfo
)
{
	for (ULONG i = 0; i < pHidDriverInfo->DeviceCount; i++)
	{
		PHIDDEVICE_INFO pCurDeviceInfo = &pHidDriverInfo->DeviceInfo[i];

		pCurDeviceInfo->OriginalServiceRoutine = (PSERVICE_CALLBACK_ROUTINE)InterlockedExchangePointer(&pCurDeviceInfo->ConnectData->ClassService, (PVOID)pCurDeviceInfo->OriginalServiceRoutine);
	}
}

VOID
InpResolveDeviceUnitIDs(
	IN PHIDDRIVER_INFO pHidDriverInfo,
	IN PDEVICE_OBJECT pDeviceObject,
	IN PVOID pInputPacket
)
{
	BOOLEAN UnitIDsResolved = TRUE;

	for (ULONG i = 0; i < pHidDriverInfo->DeviceCount; i++)
	{
		PHIDDEVICE_INFO pCurDeviceInfo = &pHidDriverInfo->DeviceInfo[i];

		if (!pCurDeviceInfo->UnitID)
		{
			if (pCurDeviceInfo->ConnectData->ClassDeviceObject == pDeviceObject)
				pCurDeviceInfo->UnitID = *(PUSHORT)pInputPacket;

			UnitIDsResolved = FALSE;
		}
	}

	pHidDriverInfo->UnitIDsResolved = UnitIDsResolved;
}

VOID
InpCallOriginalServiceCallback(
	IN PHIDDRIVER_INFO pHidDriverInfo,
	IN PDEVICE_OBJECT pDeviceObject,
	IN PVOID pInputDataStart,
	IN PVOID pInputDataEnd,
	IN PULONG pInputDataCount
)
{
	for (ULONG i = 0; i < pHidDriverInfo->DeviceCount; i++)
	{
		PHIDDEVICE_INFO pCurDeviceInfo = &pHidDriverInfo->DeviceInfo[i];

		if (pCurDeviceInfo->ConnectData->ClassDeviceObject == pDeviceObject)
		{
			pCurDeviceInfo->OriginalServiceRoutine(pDeviceObject, pInputDataStart, pInputDataEnd, pInputDataCount);
			break;
		}
	}
}

VOID
InpMouHidServiceCallbackHook(
	IN PDEVICE_OBJECT pDeviceObject,
	IN PMOUSE_INPUT_DATA pInputDataStart,
	IN PMOUSE_INPUT_DATA pInputDataEnd,
	IN PULONG pInputDataCount
)
{
	PMOUSE_INPUT_DATA pInputPacket = NULL;

	if (!gInputContext.MouHidInfo.UnitIDsResolved)
		InpResolveDeviceUnitIDs(&gInputContext.MouHidInfo, pDeviceObject, pInputDataStart);

	for (pInputPacket = pInputDataStart;
		pInputPacket < pInputDataEnd;
		++pInputPacket)
	{
		if (pInputPacket->ButtonFlags)
			gInputContext.MouseFlags = pInputPacket->ButtonFlags;

		ImpLog(
			"Mouse Packet for DO=%p: ID=%hu IF=0x%03hX BF=0x%03hX"
			" BD=0x%04hX RB=0x%X EI=0x%X LX=%d LY=%d\n",
			pDeviceObject,
			pInputPacket->UnitId,
			pInputPacket->Flags,
			pInputPacket->ButtonFlags,
			pInputPacket->ButtonData,
			pInputPacket->RawButtons,
			pInputPacket->ExtraInformation,
			pInputPacket->LastX,
			pInputPacket->LastY
		);
	}

	if (!gInputContext.IsBlockingMouseInput)
		InpCallOriginalServiceCallback(&gInputContext.MouHidInfo, pDeviceObject, pInputDataStart, pInputDataEnd, pInputDataCount);
}

VOID
InpKbdHidServiceCallbackHook(
	IN PDEVICE_OBJECT pDeviceObject,
	IN PKEYBOARD_INPUT_DATA pInputDataStart,
	IN PKEYBOARD_INPUT_DATA pInputDataEnd,
	IN PULONG pInputDataCount
)
{
	PKEYBOARD_INPUT_DATA pInputPacket = NULL;

	if (!gInputContext.KbdHidInfo.UnitIDsResolved)
		InpResolveDeviceUnitIDs(&gInputContext.KbdHidInfo, pDeviceObject, pInputDataStart);

	for (pInputPacket = pInputDataStart;
		pInputPacket < pInputDataEnd;
		++pInputPacket)
	{
		ImpLog(
			"Keyboard Packet for DO=%p: ID=%hu FL=0x%03hX MC=0x%03hX EI=0x%03hX\n",
			pDeviceObject,
			pInputPacket->UnitId,
			pInputPacket->Flags,
			pInputPacket->MakeCode,
			pInputPacket->ExtraInformation
		);

		gInputContext.KeyStates[pInputPacket->MakeCode] = (BOOLEAN)pInputPacket->Flags;
	}

	if (!gInputContext.IsBlockingKbdInput)
		InpCallOriginalServiceCallback(&gInputContext.KbdHidInfo, pDeviceObject, pInputDataStart, pInputDataEnd, pInputDataCount);
}

VOID
InpDispatchMouseInput(
	IN MOUSE_INPUT_DATA mouseInputData
)
{
	MOUSE_INPUT_DATA pInputData[2];
	PHIDDEVICE_INFO pDeviceInfo = NULL;
	ULONG uInputDataCount = 1;

	if (gInputContext.MouHidInfo.DeviceInfo == NULL)
	{
		ImpLog("InpDispatchKbdInput: Device not initialised.\n");
		return;
	}

	pDeviceInfo = &gInputContext.MouHidInfo.DeviceInfo[0];
	mouseInputData.UnitId = pDeviceInfo->UnitID;

	pInputData[0] = mouseInputData;

	pDeviceInfo->OriginalServiceRoutine(pDeviceInfo->ConnectData->ClassDeviceObject, &pInputData[0], &pInputData[1], &uInputDataCount);
}

VOID
InpDispatchKbdInput(
	IN KEYBOARD_INPUT_DATA kbdInputData
)
{
	KEYBOARD_INPUT_DATA pInputData[2];
	PHIDDEVICE_INFO pDeviceInfo = NULL;
	ULONG uInputDataCount = 1;

	if (gInputContext.KbdHidInfo.DeviceInfo == NULL)
	{
		ImpLog("InpDispatchKbdInput: Device not initialised.\n");
		return;
	}

	pDeviceInfo = &gInputContext.KbdHidInfo.DeviceInfo[0];
	kbdInputData.UnitId = pDeviceInfo->UnitID;

	pInputData[0] = kbdInputData;

	pDeviceInfo->OriginalServiceRoutine(pDeviceInfo->ConnectData->ClassDeviceObject, &pInputData[0], &pInputData[1], &uInputDataCount);
}

NTSTATUS
InpPnPNotificationCallback(
	PVOID NotificationStructure,
	PVOID Context
)
{
	NTSTATUS Status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(Context);

	PDEVICE_INTERFACE_CHANGE_NOTIFICATION pDeviceChangeContext = NotificationStructure;

	InpUninstallServiceCallbackHooks(&gInputContext.MouHidInfo);

	if (IsEqualGUID(&pDeviceChangeContext->Event, &GUID_DEVICE_INTERFACE_ARRIVAL))
	{
		ImpLog("InpPnPNotificationCallback: Device connected.\n");
	}
	else if (IsEqualGUID(&pDeviceChangeContext->Event, &GUID_DEVICE_INTERFACE_REMOVAL))
	{
		ImpLog("InpPnPNotificationCallback: Device removed.\n");
	}
	else
	{
		ImpLog("InpPnPNotificationCallback: Unknown Event GUID, please fix\n");
	}

	Status = InpCollectHidDeviceInfo(gInputContext.MouHidInfo.DriverObject, MOUHID_CONNECT_DATA_OFFSET, &gInputContext.MouHidInfo);

	if (!NT_SUCCESS(Status))
	{
		ImpLog("InpPnPNotificationCallback: InpCollectHidDeviceInfo failed with 0x%x\n", Status);
		return Status;
	}

	InpInstallServiceCallbackHooks(&gInputContext.MouHidInfo, &InpMouHidServiceCallbackHook);

	return STATUS_SUCCESS;
}

NTSTATUS
InpInitialise(VOID)
/*++
Routine Description:
	Initialises PnP callback hooks as well as all HID device dispatch hooks
--*/
{
	NTSTATUS Status = STATUS_SUCCESS;

	// PnpDeviceClassNotifyList, Hook one of these
#if 0
	Status = EhRegisterHook(FNV1A_HASH("InpPnPCallback"), EhTargetFunction, InpPnPNotificationCallback);
	if (!NT_SUCCESS(Status))
	{
		ImpDebugPrint("Failed to register test hook... (%X)\n", Status);
		return Status;
	}
#endif

	return Status;
}
