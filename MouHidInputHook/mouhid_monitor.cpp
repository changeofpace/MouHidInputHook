/*++

Copyright (c) 2019 changeofpace. All rights reserved.

Use of this source code is governed by the MIT license. See the 'LICENSE' file
for more information.

--*/

#include "mouhid_monitor.h"

#include <ntddmou.h>

#include "debug.h"
#include "log.h"
#include "mouclass.h"
#include "mouhid_hook_manager.h"


//=============================================================================
// Constants
//=============================================================================
#define MODULE_TITLE    "MouHid Monitor"


//=============================================================================
// Private Types
//=============================================================================
typedef struct _HOOK_CALLBACK_CONTEXT
{
    _Interlocked_ volatile POINTER_ALIGNMENT LONG64 PacketIndex;

} HOOK_CALLBACK_CONTEXT, *PHOOK_CALLBACK_CONTEXT;

typedef struct _MOUHID_MONITOR_CONTEXT
{
    POINTER_ALIGNMENT ERESOURCE Resource;
    _Guarded_by_(Resource) HANDLE RegistrationHandle;
    _Guarded_by_(Resource) PHOOK_CALLBACK_CONTEXT CallbackContext;

} MOUHID_MONITOR_CONTEXT, *PMOUHID_MONITOR_CONTEXT;


//=============================================================================
// Module Globals
//=============================================================================
EXTERN_C static MOUHID_MONITOR_CONTEXT g_MhmContext = {};


//=============================================================================
// Private Prototypes
//=============================================================================
EXTERN_C
static
MHK_HOOK_CALLBACK_ROUTINE
MhmpHookCallback;

EXTERN_C
static
MHK_NOTIFICATION_CALLBACK_ROUTINE
MhmpNotificationCallback;


//=============================================================================
// Meta Interface
//=============================================================================
_Use_decl_annotations_
EXTERN_C
NTSTATUS
MhmDriverEntry()
/*++

Routine Description:

    Initializes the MouHid Monitor module.

Required Modules:

    None

Remarks:

    If successful, the caller must call MhmDriverUnload when the driver is
    unloaded.

--*/
{
    BOOLEAN fResourceInitialized = FALSE;
    NTSTATUS ntstatus = STATUS_SUCCESS;

    DBG_PRINT("Loading %s.", MODULE_TITLE);

    ntstatus = ExInitializeResourceLite(&g_MhmContext.Resource);
    if (!NT_SUCCESS(ntstatus))
    {
        ERR_PRINT("ExInitializeResourceLite failed: 0x%X", ntstatus);
        goto exit;
    }
    //
    fResourceInitialized = TRUE;

    DBG_PRINT("%s loaded.", MODULE_TITLE);

exit:
    if (!NT_SUCCESS(ntstatus))
    {
        if (fResourceInitialized)
        {
            VERIFY(ExDeleteResourceLite(&g_MhmContext.Resource));
        }
    }

    return ntstatus;
}


_Use_decl_annotations_
EXTERN_C
VOID
MhmDriverUnload()
{
    DBG_PRINT("Unloading %s.", MODULE_TITLE);

    VERIFY(MhmDisableMouHidMonitor());

    VERIFY(ExDeleteResourceLite(&g_MhmContext.Resource));

    DBG_PRINT("%s unloaded.", MODULE_TITLE);
}


//=============================================================================
// Public Interface
//=============================================================================
_Use_decl_annotations_
EXTERN_C
NTSTATUS
MhmQueryMouHidMonitor(
    PBOOLEAN pfEnabled
)
{
    NTSTATUS ntstatus = STATUS_SUCCESS;

    //
    // Zero out parameters.
    //
    *pfEnabled = FALSE;

    ExEnterCriticalRegionAndAcquireResourceShared(&g_MhmContext.Resource);

    //
    // Set out parameters.
    //
    if (g_MhmContext.RegistrationHandle)
    {
        *pfEnabled = TRUE;
    }

    ExReleaseResourceAndLeaveCriticalRegion(&g_MhmContext.Resource);

    return ntstatus;
}


_Use_decl_annotations_
EXTERN_C
NTSTATUS
MhmEnableMouHidMonitor()
/*++

Routine Description:

    Registers an MHK callback which logs mouse input data packets in the input
    packet stream.

Remarks:

    If successful, the caller must disable the monitor by calling
    MhmDisableMouHidMonitor.

--*/
{
    PHOOK_CALLBACK_CONTEXT pCallbackContext = NULL;
    HANDLE RegistrationHandle = NULL;
    NTSTATUS ntstatus = STATUS_SUCCESS;

    DBG_PRINT("Enabling %s.", MODULE_TITLE);

    ExEnterCriticalRegionAndAcquireResourceExclusive(&g_MhmContext.Resource);

    if (g_MhmContext.RegistrationHandle)
    {
        ERR_PRINT("%s is already enabled.", MODULE_TITLE);
        ntstatus = STATUS_ALREADY_REGISTERED;
        goto exit;
    }

    pCallbackContext = (PHOOK_CALLBACK_CONTEXT)ExAllocatePool(
        NonPagedPool,
        sizeof(*pCallbackContext));
    if (!pCallbackContext)
    {
        ntstatus = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }
    //
    RtlSecureZeroMemory(pCallbackContext, sizeof(*pCallbackContext));

    ntstatus = MhkRegisterCallbacks(
        MhmpHookCallback,
        MhmpNotificationCallback,
        pCallbackContext,
        &RegistrationHandle);
    if (!NT_SUCCESS(ntstatus))
    {
        ERR_PRINT("MhkRegisterCallbacks failed: 0x%X", ntstatus);
        goto exit;
    }

    DBG_PRINT("%s enabled.", MODULE_TITLE);

    //
    // Update the global context.
    //
    g_MhmContext.RegistrationHandle = RegistrationHandle;
    g_MhmContext.CallbackContext = pCallbackContext;

exit:
    if (!NT_SUCCESS(ntstatus))
    {
        if (pCallbackContext)
        {
            ExFreePool(pCallbackContext);
        }
    }

    ExReleaseResourceAndLeaveCriticalRegion(&g_MhmContext.Resource);

    return ntstatus;
}


_Use_decl_annotations_
EXTERN_C
NTSTATUS
MhmDisableMouHidMonitor()
{
    HANDLE RegistrationHandle = NULL;
    NTSTATUS ntstatus = STATUS_SUCCESS;

    DBG_PRINT("Disabling %s.", MODULE_TITLE);

    ExEnterCriticalRegionAndAcquireResourceExclusive(&g_MhmContext.Resource);

    RegistrationHandle = g_MhmContext.RegistrationHandle;
    if (!RegistrationHandle)
    {
        WRN_PRINT("%s is not enabled.", MODULE_TITLE);
        goto exit;
    }

    ntstatus = MhkUnregisterCallbacks(RegistrationHandle);
    if (!NT_SUCCESS(ntstatus))
    {
        ERR_PRINT("MhkUnregisterCallbacks failed: 0x%X", ntstatus);
        goto exit;
    }

    ExFreePool(g_MhmContext.CallbackContext);

    //
    // Update the global context.
    //
    g_MhmContext.RegistrationHandle = NULL;
    g_MhmContext.CallbackContext = NULL;

    DBG_PRINT("%s disabled.", MODULE_TITLE);

exit:
    ExReleaseResourceAndLeaveCriticalRegion(&g_MhmContext.Resource);

    return ntstatus;
}


//=============================================================================
// Private Interface
//=============================================================================
_Use_decl_annotations_
EXTERN_C
static
VOID
NTAPI
MhmpHookCallback(
    PMOUSE_SERVICE_CALLBACK_ROUTINE pServiceCallbackOriginal,
    PDEVICE_OBJECT pClassDeviceObject,
    PMOUSE_INPUT_DATA pInputDataStart,
    PMOUSE_INPUT_DATA pInputDataEnd,
    PULONG pnInputDataConsumed,
    PVOID pContext
)
{
    PHOOK_CALLBACK_CONTEXT pCallbackContext = NULL;
    PMOUSE_INPUT_DATA pInputPacket = NULL;
    ULONG64 PacketIndex = 0;

    pCallbackContext = (PHOOK_CALLBACK_CONTEXT)pContext;

    //
    // Log each packet in the input buffer.
    //
    for (pInputPacket = pInputDataStart;
        pInputPacket < pInputDataEnd;
        ++pInputPacket)
    {
        PacketIndex = InterlockedIncrement64(&pCallbackContext->PacketIndex);

        MclPrintInputPacket(
            PacketIndex,
            pServiceCallbackOriginal,
            pClassDeviceObject,
            pInputPacket);
    }

    //
    // Invoke the original service callback.
    //
    pServiceCallbackOriginal(
        pClassDeviceObject,
        pInputDataStart,
        pInputDataEnd,
        pnInputDataConsumed);
}


_Use_decl_annotations_
EXTERN_C
static
VOID
NTAPI
MhmpNotificationCallback(
    HANDLE RegistrationHandle,
    MOUSE_PNP_NOTIFICATION_EVENT Event,
    PVOID pContext
)
{
    UNREFERENCED_PARAMETER(pContext);

#if defined(DBG)
    if (MousePnpNotificationEventArrival == Event)
    {
        DBG_PRINT("Received MHK notification. (Arrival)");
    }
    else if (MousePnpNotificationEventRemoval == Event)
    {
        DBG_PRINT("Received MHK notification. (Removal)");
    }
    else
    {
        ERR_PRINT("Received MHK notification. (Unknown)");
        DEBUG_BREAK;
    }
#else
    UNREFERENCED_PARAMETER(Event);
#endif

    ExEnterCriticalRegionAndAcquireResourceExclusive(&g_MhmContext.Resource);

    if (g_MhmContext.RegistrationHandle != RegistrationHandle)
    {
        ERR_PRINT("Unexpected registration handle: %p", RegistrationHandle);
        DEBUG_BREAK;
        goto exit;
    }

    //
    // The MouHid Hook Manager unregistered our MHK callbacks before it
    //  invoked this notification routine. Release the callback context and
    //  update the global context.
    //
    ExFreePool(g_MhmContext.CallbackContext);

    g_MhmContext.RegistrationHandle = NULL;
    g_MhmContext.CallbackContext = NULL;

    DBG_PRINT("%s disabled.", MODULE_TITLE);

exit:
    ExReleaseResourceAndLeaveCriticalRegion(&g_MhmContext.Resource);
}
