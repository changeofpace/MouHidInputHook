/*++

Copyright (c) 2019 changeofpace. All rights reserved.

Use of this source code is governed by the MIT license. See the 'LICENSE' file
for more information.

--*/

#pragma once

#if defined(_KERNEL_MODE)
#include <fltKernel.h>
#else
#include <Windows.h>
#endif

#include <devioctl.h>

//=============================================================================
// Names
//=============================================================================
#define DRIVER_NAME_U           L"MouHidInputHook"
#define LOCAL_DEVICE_PATH_U     (L"\\\\.\\" DRIVER_NAME_U)
#define NT_DEVICE_NAME_U        (L"\\Device\\" DRIVER_NAME_U)
#define SYMBOLIC_LINK_NAME_U    (L"\\DosDevices\\" DRIVER_NAME_U)

//=============================================================================
// Ioctls
//=============================================================================
#define FILE_DEVICE_MOUHID_INPUT_HOOK   51382

#define IOCTL_QUERY_MOUHID_INPUT_MONITOR    \
    CTL_CODE(                               \
        FILE_DEVICE_MOUHID_INPUT_HOOK,      \
        3300,                               \
        METHOD_BUFFERED,                    \
        FILE_ANY_ACCESS)

#define IOCTL_ENABLE_MOUHID_INPUT_MONITOR   \
    CTL_CODE(                               \
        FILE_DEVICE_MOUHID_INPUT_HOOK,      \
        3500,                               \
        METHOD_BUFFERED,                    \
        FILE_ANY_ACCESS)

#define IOCTL_DISABLE_MOUHID_INPUT_MONITOR  \
    CTL_CODE(                               \
        FILE_DEVICE_MOUHID_INPUT_HOOK,      \
        3501,                               \
        METHOD_BUFFERED,                    \
        FILE_ANY_ACCESS)

//=============================================================================
// IOCTL_QUERY_MOUHID_INPUT_MONITOR
//=============================================================================
typedef struct _QUERY_MOUHID_INPUT_MONITOR_REPLY
{
    BOOLEAN Enabled;

} QUERY_MOUHID_INPUT_MONITOR_REPLY, *PQUERY_MOUHID_INPUT_MONITOR_REPLY;
