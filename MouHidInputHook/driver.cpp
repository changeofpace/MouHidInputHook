/*++

Copyright (c) 2019 changeofpace. All rights reserved.

Use of this source code is governed by the MIT license. See the 'LICENSE' file
for more information.

--*/

#include <fltKernel.h>

#include "debug.h"
#include "log.h"
#include "mouhid.h"
#include "mouhid_hook_manager.h"
#include "mouhid_monitor.h"

#include "../Common/ioctl.h"


//=============================================================================
// Private Prototypes
//=============================================================================
EXTERN_C
DRIVER_INITIALIZE
DriverEntry;

EXTERN_C
static
DRIVER_UNLOAD
DriverUnload;

_Dispatch_type_(IRP_MJ_CREATE)
EXTERN_C
static
DRIVER_DISPATCH
DispatchCreate;

_Dispatch_type_(IRP_MJ_CLOSE)
EXTERN_C
static
DRIVER_DISPATCH
DispatchClose;

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
EXTERN_C
static
DRIVER_DISPATCH
DispatchDeviceControl;


//=============================================================================
// Meta Interface
//=============================================================================
_Use_decl_annotations_
EXTERN_C
NTSTATUS
DriverEntry(
    PDRIVER_OBJECT pDriverObject,
    PUNICODE_STRING pRegistryPath
)
{
    PDEVICE_OBJECT pDeviceObject = NULL;
    UNICODE_STRING usDeviceName = {};
    UNICODE_STRING usSymbolicLinkName = {};
    BOOLEAN fSymbolicLinkCreated = FALSE;
    BOOLEAN fMclLoaded = FALSE;
    BOOLEAN fMhkLoaded = FALSE;
    BOOLEAN fMhmLoaded = FALSE;
    NTSTATUS ntstatus = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(pRegistryPath);

    DBG_PRINT("Loading %ls.", NT_DEVICE_NAME_U);

    usDeviceName = RTL_CONSTANT_STRING(NT_DEVICE_NAME_U);

    ntstatus = IoCreateDevice(
        pDriverObject,
        0,
        &usDeviceName,
        FILE_DEVICE_MOUHID_INPUT_HOOK,
        FILE_DEVICE_SECURE_OPEN,
        TRUE,
        &pDeviceObject);
    if (!NT_SUCCESS(ntstatus))
    {
        ERR_PRINT("IoCreateDevice failed: 0x%X", ntstatus);
        goto exit;
    }
    //
    pDriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
    pDriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
    pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
        DispatchDeviceControl;
    pDriverObject->DriverUnload = DriverUnload;

    //
    // Create a symbolic link for the user mode client.
    //
    usSymbolicLinkName = RTL_CONSTANT_STRING(SYMBOLIC_LINK_NAME_U);

    ntstatus = IoCreateSymbolicLink(&usSymbolicLinkName, &usDeviceName);
    if (!NT_SUCCESS(ntstatus))
    {
        ERR_PRINT("IoCreateSymbolicLink failed: 0x%X", ntstatus);
        goto exit;
    }
    //
    fSymbolicLinkCreated = TRUE;

    //
    // Load the driver modules.
    //
    ntstatus = MhdDriverEntry();
    if (!NT_SUCCESS(ntstatus))
    {
        ERR_PRINT("MhdDriverEntry failed: 0x%X", ntstatus);
        goto exit;
    }

    ntstatus = MclDriverEntry(pDriverObject);
    if (!NT_SUCCESS(ntstatus))
    {
        ERR_PRINT("MclDriverEntry failed: 0x%X", ntstatus);
        goto exit;
    }
    //
    fMclLoaded = TRUE;

    ntstatus = MhkDriverEntry();
    if (!NT_SUCCESS(ntstatus))
    {
        ERR_PRINT("MhkDriverEntry failed: 0x%X", ntstatus);
        goto exit;
    }
    //
    fMhkLoaded = TRUE;

    ntstatus = MhmDriverEntry();
    if (!NT_SUCCESS(ntstatus))
    {
        ERR_PRINT("MhmDriverEntry failed: 0x%X", ntstatus);
        goto exit;
    }
    //
    fMhmLoaded = TRUE;

    DBG_PRINT("%ls loaded.", NT_DEVICE_NAME_U);

exit:
    if (!NT_SUCCESS(ntstatus))
    {
        if (fMhmLoaded)
        {
            MhmDriverUnload();
        }

        if (fMhkLoaded)
        {
            MhkDriverUnload();
        }

        if (fMclLoaded)
        {
            MclDriverUnload();
        }

        if (fSymbolicLinkCreated)
        {
            VERIFY(IoDeleteSymbolicLink(&usSymbolicLinkName));
        }

        if (pDeviceObject)
        {
            IoDeleteDevice(pDeviceObject);
        }
    }

    return ntstatus;
}


_Use_decl_annotations_
EXTERN_C
static
VOID
DriverUnload(
    PDRIVER_OBJECT pDriverObject
)
{
    UNICODE_STRING usSymbolicLinkName = {};

    DBG_PRINT("Unloading %ls.", NT_DEVICE_NAME_U);

    //
    // Unload the driver modules.
    //
    MhmDriverUnload();
    MhkDriverUnload();
    MclDriverUnload();

    //
    // Release driver resources.
    //
    usSymbolicLinkName = RTL_CONSTANT_STRING(SYMBOLIC_LINK_NAME_U);

    VERIFY(IoDeleteSymbolicLink(&usSymbolicLinkName));

    if (pDriverObject->DeviceObject)
    {
        IoDeleteDevice(pDriverObject->DeviceObject);
    }

    DBG_PRINT("%ls unloaded.", NT_DEVICE_NAME_U);
}


//=============================================================================
// Private Interface
//=============================================================================
_Use_decl_annotations_
EXTERN_C
static
NTSTATUS
DispatchCreate(
    PDEVICE_OBJECT pDeviceObject,
    PIRP pIrp
)
{
    UNREFERENCED_PARAMETER(pDeviceObject);
    DBG_PRINT("Processing IRP_MJ_CREATE.");
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}


_Use_decl_annotations_
EXTERN_C
static
NTSTATUS
DispatchClose(
    PDEVICE_OBJECT pDeviceObject,
    PIRP pIrp
)
{
    NTSTATUS ntstatus = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(pDeviceObject);

    DBG_PRINT("Processing IRP_MJ_CLOSE.");

    //
    // Manually disable the MouHid Monitor in case the user mode client failed
    //  to disable it.
    //
    VERIFY(MhmDisableMouHidMonitor());

    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return ntstatus;
}


_Use_decl_annotations_
EXTERN_C
static
NTSTATUS
DispatchDeviceControl(
    PDEVICE_OBJECT pDeviceObject,
    PIRP pIrp
)
{
    PIO_STACK_LOCATION pIrpStack = IoGetCurrentIrpStackLocation(pIrp);
    PVOID pSystemBuffer = pIrp->AssociatedIrp.SystemBuffer;
    ULONG cbInput = pIrpStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG cbOutput = pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    PQUERY_MOUHID_INPUT_MONITOR_REPLY pQueryMouHidInputMonitorReply = NULL;
    ULONG_PTR Information = 0;
    NTSTATUS ntstatus = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(pDeviceObject);

    switch (pIrpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_QUERY_MOUHID_INPUT_MONITOR:
            DBG_PRINT("Processing IOCTL_QUERY_MOUHID_INPUT_MONITOR.");

            if (cbInput)
            {
                ntstatus = STATUS_INVALID_PARAMETER_4;
                goto exit;
            }

            pQueryMouHidInputMonitorReply =
                (PQUERY_MOUHID_INPUT_MONITOR_REPLY)pSystemBuffer;
            if (!pQueryMouHidInputMonitorReply)
            {
                ntstatus = STATUS_INVALID_PARAMETER_5;
                goto exit;
            }

            if (sizeof(*pQueryMouHidInputMonitorReply) != cbOutput)
            {
                ntstatus = STATUS_INVALID_BUFFER_SIZE;
                goto exit;
            }

            ntstatus = MhmQueryMouHidMonitor(
                &pQueryMouHidInputMonitorReply->Enabled);
            if (!NT_SUCCESS(ntstatus))
            {
                ERR_PRINT("MhmQueryMouHidMonitor failed: 0x%X", ntstatus);
                goto exit;
            }

            Information = sizeof(*pQueryMouHidInputMonitorReply);

            break;

        case IOCTL_ENABLE_MOUHID_INPUT_MONITOR:
            DBG_PRINT("Processing IOCTL_ENABLE_MOUHID_INPUT_MONITOR.");

            if (cbInput || cbOutput)
            {
                ntstatus = STATUS_INVALID_PARAMETER;
                goto exit;
            }

            ntstatus = MhmEnableMouHidMonitor();
            if (!NT_SUCCESS(ntstatus))
            {
                ERR_PRINT("MhmEnableMouHidMonitor failed: 0x%X", ntstatus);
                goto exit;
            }

            break;

        case IOCTL_DISABLE_MOUHID_INPUT_MONITOR:
            DBG_PRINT("Processing IOCTL_DISABLE_MOUHID_INPUT_MONITOR.");

            if (cbInput || cbOutput)
            {
                ntstatus = STATUS_INVALID_PARAMETER;
                goto exit;
            }

            ntstatus = MhmDisableMouHidMonitor();
            if (!NT_SUCCESS(ntstatus))
            {
                ERR_PRINT("MhmDisableMouHidMonitor failed: 0x%X", ntstatus);
                goto exit;
            }

            break;

        default:
            ERR_PRINT(
                "Unhandled IOCTL."
                " (MajorFunction = %hhu, MinorFunction = %hhu)",
                pIrpStack->MajorFunction,
                pIrpStack->MinorFunction);
            ntstatus = STATUS_UNSUCCESSFUL;
            goto exit;
    }

exit:
    pIrp->IoStatus.Information = Information;
    pIrp->IoStatus.Status = ntstatus;

    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return ntstatus;
}
