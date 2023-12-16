#pragma once

#include <initguid.h>
#include <ntifs.h>
#include <ntddk.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <ntdd8042.h>
#include <stdarg.h>
#include <wdm.h>
#include <Wdmguid.h>

#pragma region NT internals
EXTERN_C POBJECT_TYPE* IoDriverObjectType;

EXTERN_C
NTSTATUS
NTAPI
ObReferenceObjectByName(
    _In_        PUNICODE_STRING ObjectName,
    _In_        ULONG           Attributes,
    _In_opt_    PACCESS_STATE   AccessState,
    _In_opt_    ACCESS_MASK     DesiredAccess,
    _In_        POBJECT_TYPE    ObjectType,
    _In_        KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PVOID           ParseContext,
    _Out_       PDRIVER_OBJECT* Object
);
#pragma endregion

#define MOUHID_CONNECT_DATA_OFFSET 0xE0
#define KBDHID_CONNECT_DATA_OFFSET 0x88

typedef struct _HIDDEVICE_INFO
/*++
DeviceObject:
    The device object of the current HID device.
ConnectData:
    Pointer to the CONNECT_DATA structure within the device extension.
OriginalServiceRoutine:
    The original ClassService inside of the CONNECT_DATA structure.
--*/
{
    PCONNECT_DATA               ConnectData;
    PSERVICE_CALLBACK_ROUTINE   OriginalServiceRoutine;
    USHORT                      UnitID;
} HIDDEVICE_INFO, * PHIDDEVICE_INFO;

typedef struct _HIDDRIVER_INFO
/*++
DriverObject:
    The driver object of the current HID driver.
DeviceInfo:
    List containing a HIDDEVICE_INFO entry for each device connected to the HID driver.
DeviceCount:
    The number of entries in DeviceInfo.
--*/
{
    PDRIVER_OBJECT  DriverObject;
    PHIDDEVICE_INFO DeviceInfo;
    ULONG           DeviceCount;
    BOOLEAN         UnitIDsResolved;
} HIDDRIVER_INFO, * PHIDDRIVER_INFO;

typedef struct _INPUT_CONTEXT
{
    HIDDRIVER_INFO  MouHidInfo;
    HIDDRIVER_INFO  KbdHidInfo;
    PVOID           PnpNotifEntry;
    ULONG           MouseFlags;
    BOOLEAN         IsBlockingMouseInput;
    BOOLEAN         IsBlockingKbdInput;
    BOOLEAN         KeyStates[0x100];
}INPUT_CONTEXT, * PINPUT_CONTEXT;

EXTERN_C INPUT_CONTEXT gInputContext;

NTSTATUS
InpInit(VOID);

NTSTATUS
InpInstallPnPNotificationCallback(
    IN PDRIVER_OBJECT pDriverObject,
    IN PDRIVER_NOTIFICATION_CALLBACK_ROUTINE pNotificationRoutine
);

NTSTATUS
InpCollectHidDeviceInfo(
    IN PDRIVER_OBJECT pDriverObject,
    IN ULONG64 uConnectDataOffset,
    OUT PHIDDRIVER_INFO pMouHidDriverInfo
);

VOID
InpFreeHidDriverInfo(
    IN PHIDDRIVER_INFO pHidDriverInfo
);

VOID
InpInstallServiceCallbackHooks(
    IN PHIDDRIVER_INFO pHidDriverInfo,
    IN PSERVICE_CALLBACK_ROUTINE pServiceCallbackRoutine
);

VOID
InpUninstallServiceCallbackHooks(
    IN PHIDDRIVER_INFO pHidDriverInfo
);

VOID
InpResolveDeviceUnitIDs(
    IN PHIDDRIVER_INFO pHidDriverInfo,
    IN PDEVICE_OBJECT pDeviceObject,
    IN PVOID pInputPacket
);

VOID
InpCallOriginalServiceCallback(
    IN PHIDDRIVER_INFO pHidDriverInfo,
    IN PDEVICE_OBJECT pDeviceObject,
    IN PVOID pInputDataStart,
    IN PVOID pInputDataEnd,
    IN PULONG pInputDataCount
);

VOID
InpMouHidServiceCallbackHook(
    IN PDEVICE_OBJECT pDeviceObject,
    IN PMOUSE_INPUT_DATA pInputDataStart,
    IN PMOUSE_INPUT_DATA pInputDataEnd,
    IN PULONG pInputDataCount
);

VOID
InpKbdHidServiceCallbackHook(
    IN PDEVICE_OBJECT pDeviceObject,
    IN PKEYBOARD_INPUT_DATA pInputDataStart,
    IN PKEYBOARD_INPUT_DATA pInputDataEnd,
    IN PULONG pInputDataCount
);

VOID
InpDispatchMouseInput(
    IN MOUSE_INPUT_DATA mouseInputData
);

VOID
InpDispatchKbdInput(
    IN KEYBOARD_INPUT_DATA kbdInputData
);

NTSTATUS
InpInitialise(VOID);

DRIVER_NOTIFICATION_CALLBACK_ROUTINE InpPnPNotificationCallback;