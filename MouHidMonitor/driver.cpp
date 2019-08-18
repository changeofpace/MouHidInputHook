/*++

Copyright (c) 2019 changeofpace. All rights reserved.

Use of this source code is governed by the MIT license. See the 'LICENSE' file
for more information.

--*/

#include "driver.h"

#include "debug.h"

#include "../Common/ioctl.h"


//=============================================================================
// Private Types
//=============================================================================
typedef struct _DRIVER_CONTEXT
{
    HANDLE DeviceHandle;

} DRIVER_CONTEXT, *PDRIVER_CONTEXT;


//=============================================================================
// Module Globals
//=============================================================================
static DRIVER_CONTEXT g_DriverContext = {};


//=============================================================================
// Meta Interface
//=============================================================================
_Use_decl_annotations_
BOOL
DrvInitialization()
{
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    BOOL status = TRUE;

    hDevice = CreateFileW(
        LOCAL_DEVICE_PATH_U,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (INVALID_HANDLE_VALUE == hDevice)
    {
        status = FALSE;
        goto exit;
    }

    //
    // Initialize the global context.
    //
    g_DriverContext.DeviceHandle = hDevice;

exit:
    if (!status)
    {
        if (INVALID_HANDLE_VALUE != hDevice)
        {
            VERIFY(CloseHandle(hDevice));
        }
    }

    return status;
}


VOID
DrvTermination()
{
    VERIFY(CloseHandle(g_DriverContext.DeviceHandle));
}


//=============================================================================
// Public Interface
//=============================================================================

//
// Suppress signed/unsigned mismatch warnings for Ioctl codes.
//
#pragma warning(push)
#pragma warning(disable:4245)

_Use_decl_annotations_
BOOL
DrvQueryMouHidInputMonitor(
    PBOOL pfEnabled
)
{
    QUERY_MOUHID_INPUT_MONITOR_REPLY Reply = {};
    DWORD cbReturned = 0;
    BOOL status = TRUE;

    //
    // Zero out parameters.
    //
    *pfEnabled = FALSE;

    status = DeviceIoControl(
        g_DriverContext.DeviceHandle,
        IOCTL_QUERY_MOUHID_INPUT_MONITOR,
        NULL,
        0,
        &Reply,
        sizeof(Reply),
        &cbReturned,
        NULL);
    if (!status)
    {
        goto exit;
    }

    //
    // Set out parameters.
    //
    *pfEnabled = Reply.Enabled;

exit:
    return status;
}


_Use_decl_annotations_
BOOL
DrvEnableMouHidInputMonitor()
{
    DWORD cbReturned = 0;
    BOOL status = TRUE;

    status = DeviceIoControl(
        g_DriverContext.DeviceHandle,
        IOCTL_ENABLE_MOUHID_INPUT_MONITOR,
        NULL,
        0,
        NULL,
        0,
        &cbReturned,
        NULL);
    if (!status)
    {
        goto exit;
    }

exit:
    return status;
}


_Use_decl_annotations_
BOOL
DrvDisableMouHidInputMonitor()
{
    DWORD cbReturned = 0;
    BOOL status = TRUE;

    status = DeviceIoControl(
        g_DriverContext.DeviceHandle,
        IOCTL_DISABLE_MOUHID_INPUT_MONITOR,
        NULL,
        0,
        NULL,
        0,
        &cbReturned,
        NULL);
    if (!status)
    {
        goto exit;
    }

exit:
    return status;
}

#pragma warning(pop) // disable:4245
