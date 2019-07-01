/*++

Copyright (c) 2019 changeofpace. All rights reserved.

Use of this source code is governed by the MIT license. See the 'LICENSE' file
for more information.

--*/

#include <Windows.h>

#include "debug.h"
#include "driver.h"

#include "log.h"


//=============================================================================
// Constants
//=============================================================================
#define CONSOLE_MODE_OPTIONS            0

#define INPUT_EVENT_BUFFER_SIZE         64

#define EXIT_PROCESS_VIRTUAL_KEY        (VK_RETURN)

#define QUERY_THREAD_INTERVAL_MS        5000
#define QUERY_THREAD_EXIT_TIMEOUT_MS    10000


//=============================================================================
// Private Types
//=============================================================================
typedef struct _CONSOLE_CONTEXT
{
    HANDLE StandardInputHandle;

    BOOL RestorePreviousMode;
    DWORD PreviousMode;

} CONSOLE_CONTEXT, *PCONSOLE_CONTEXT;

typedef struct _MOUHID_MONITOR_CLIENT_CONTEXT
{
    BOOL Active;
    CONSOLE_CONTEXT Console;

} MOUHID_MONITOR_CLIENT_CONTEXT, *PMOUHID_MONITOR_CLIENT_CONTEXT;


//=============================================================================
// Module Globals
//=============================================================================
EXTERN_C static MOUHID_MONITOR_CLIENT_CONTEXT g_ClientContext = {};


//=============================================================================
// Private Interface
//=============================================================================
_Check_return_
static
DWORD
WINAPI
QueryMouHidMonitorThread(
    _In_ PVOID pContext
)
{
    PMOUHID_MONITOR_CLIENT_CONTEXT pClientContext = NULL;
    BOOL fMouHidInputMonitorEnabled = FALSE;
    DWORD exitstatus = ERROR_SUCCESS;

    pClientContext = (PMOUHID_MONITOR_CLIENT_CONTEXT)pContext;

    for (; pClientContext->Active;)
    {
        Sleep(QUERY_THREAD_INTERVAL_MS);

        //
        // If either of these calls fail then continue to the next iteration so
        //  that we can make another attempt.
        //
        if (!DrvQueryMouHidInputMonitor(&fMouHidInputMonitorEnabled))
        {
            ERR_PRINT("DrvQueryMouHidInputMonitor failed: %u\n",
                GetLastError());
            continue;
        }
        //
        if (!fMouHidInputMonitorEnabled)
        {
            INF_PRINT(
                "Detected mouse device changes. Enabling MouHid Monitor.\n");

            if (!DrvEnableMouHidInputMonitor())
            {
                ERR_PRINT("DrvEnableMouHidInputMonitor failed: %u\n",
                    GetLastError());
                continue;
            }
        }
    }

    return exitstatus;
}


_Check_return_
static
BOOL
WaitForExitEvent(
    _In_ HANDLE hStdIn
)
{
    INPUT_RECORD InputEvents[INPUT_EVENT_BUFFER_SIZE] = {};
    DWORD nEventsRead = 0;
    DWORD i = 0;
    BOOL status = TRUE;

    INF_PRINT("MouHid Input Monitor enabled.\n");
    INF_PRINT("Press ENTER to exit.\n");

    for (;;)
    {
        status = ReadConsoleInputW(
            hStdIn,
            InputEvents,
            ARRAYSIZE(InputEvents),
            &nEventsRead);
        if (!status)
        {
            ERR_PRINT("ReadConsoleInputW failed: %u\n", GetLastError());
            goto exit;
        }

        for (i = 0; i < nEventsRead; ++i)
        {
            if (KEY_EVENT != InputEvents[i].EventType)
            {
                continue;
            }

            if (EXIT_PROCESS_VIRTUAL_KEY ==
                InputEvents[i].Event.KeyEvent.wVirtualKeyCode)
            {
                INF_PRINT("Exiting.\n");
                goto exit;
            }
        }
    }

exit:
    return status;
}


_Check_return_
static
BOOL
WINAPI
CtrlSignalHandlerRoutine(
    _In_ DWORD dwCtrlType
)
{
    switch (dwCtrlType)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            if (g_ClientContext.Console.RestorePreviousMode)
            {
                VERIFY(SetConsoleMode(
                    g_ClientContext.Console.StandardInputHandle,
                    g_ClientContext.Console.PreviousMode));

                g_ClientContext.Console.RestorePreviousMode = FALSE;
            }

            break;

        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
        default:
            break;
    }

    return FALSE;
}


//=============================================================================
// Meta Interface
//=============================================================================
int
main(
    _In_ int argc,
    _In_ char* argv[]
)
{
    HANDLE hStdIn = NULL;
    BOOL fDriverInitialized = FALSE;
    BOOL fMouHidInputMonitorEnabled = FALSE;
    DWORD PreviousMode = 0;
    HANDLE hThread = NULL;
    DWORD ThreadId = 0;
    DWORD waitstatus = 0;
    DWORD ThreadExitCode = 0;
    int mainstatus = EXIT_SUCCESS;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    if (INVALID_HANDLE_VALUE == hStdIn || !hStdIn)
    {
        ERR_PRINT("GetStdHandle failed: %u\n", GetLastError());
        mainstatus = EXIT_FAILURE;
        goto exit;
    }
    //
    g_ClientContext.Console.StandardInputHandle = hStdIn;

    if (!SetConsoleCtrlHandler(CtrlSignalHandlerRoutine, TRUE))
    {
        ERR_PRINT("SetConsoleCtrlHandler failed: %u\n", GetLastError());
        mainstatus = EXIT_FAILURE;
        goto exit;
    }

    if (!DrvInitialization())
    {
        ERR_PRINT("DrvInitialization failed: %u\n", GetLastError());
        mainstatus = EXIT_FAILURE;
        goto exit;
    }
    //
    fDriverInitialized = TRUE;

    if (!DrvQueryMouHidInputMonitor(&fMouHidInputMonitorEnabled))
    {
        ERR_PRINT("DrvQueryMouHidInputMonitor failed: %u\n", GetLastError());
        mainstatus = EXIT_FAILURE;
        goto exit;
    }
    //
    if (fMouHidInputMonitorEnabled)
    {
        WRN_PRINT("MouHid Monitor is already enabled.\n");
        goto exit;
    }

    if (!GetConsoleMode(hStdIn, &PreviousMode))
    {
        ERR_PRINT("GetConsoleMode failed: %u\n", GetLastError());
        mainstatus = EXIT_FAILURE;
        goto exit;
    }
    //
    g_ClientContext.Console.PreviousMode = PreviousMode;
    g_ClientContext.Console.RestorePreviousMode = TRUE;

    //
    // Disable console input.
    //
    if (!SetConsoleMode(hStdIn, CONSOLE_MODE_OPTIONS))
    {
        ERR_PRINT("SetConsoleMode failed: %u\n", GetLastError());
        mainstatus = EXIT_FAILURE;
        goto exit;
    }

    if (!DrvEnableMouHidInputMonitor())
    {
        ERR_PRINT("DrvEnableMouHidInputMonitor failed: %u\n", GetLastError());
        mainstatus = EXIT_FAILURE;
        goto exit;
    }
    //
    fMouHidInputMonitorEnabled = TRUE;
    g_ClientContext.Active = TRUE;

    //
    // Create a thread which queries the MouHid monitor state at a defined
    //  interval.
    //
    hThread = CreateThread(
        NULL,
        0,
        QueryMouHidMonitorThread,
        &g_ClientContext,
        0,
        &ThreadId);
    if (!hThread)
    {
        ERR_PRINT("CreateThread failed: %u\n", GetLastError());
        mainstatus = EXIT_FAILURE;
        goto exit;
    }

    if (!WaitForExitEvent(hStdIn))
    {
        ERR_PRINT("WaitForExitEvent failed: %u\n", GetLastError());
        mainstatus = EXIT_FAILURE;
        goto exit;
    }

    //
    // Update the client context to indicate that the query thread should exit.
    //
    g_ClientContext.Active = FALSE;

    //
    // Wait for the query thread to exit.
    //
    waitstatus = WaitForSingleObject(hThread, QUERY_THREAD_EXIT_TIMEOUT_MS);
    switch (waitstatus)
    {
        case WAIT_OBJECT_0:
            break;

        case WAIT_TIMEOUT:
            ERR_PRINT("Timedout waiting for query thread to exit.\n");
            mainstatus = EXIT_FAILURE;
            goto exit;

        case WAIT_FAILED:
            ERR_PRINT("WaitForSingleObject failed: %u\n", GetLastError());
            mainstatus = EXIT_FAILURE;
            goto exit;

        default:
            ERR_PRINT("Unexpected wait status: %u\n", waitstatus);
            mainstatus = EXIT_FAILURE;
            goto exit;
    }

    if (!GetExitCodeThread(hThread, &ThreadExitCode))
    {
        ERR_PRINT("GetExitCodeThread failed: %u\n", GetLastError());
        mainstatus = EXIT_FAILURE;
        goto exit;
    }
    //
    if (ERROR_SUCCESS != ThreadExitCode)
    {
        ERR_PRINT("Unexpected query thread exit code: %u\n", ThreadExitCode);
        mainstatus = EXIT_FAILURE;
        goto exit;
    }

exit:
    if (hThread)
    {
        VERIFY(CloseHandle(hThread));
    }

    if (fMouHidInputMonitorEnabled)
    {
        VERIFY(DrvDisableMouHidInputMonitor());
    }

    if (hStdIn && g_ClientContext.Console.RestorePreviousMode)
    {
        VERIFY(SetConsoleMode(hStdIn, g_ClientContext.Console.PreviousMode));
    }

    if (fDriverInitialized)
    {
        VERIFY(DrvTermination());
    }

    return mainstatus;
}
