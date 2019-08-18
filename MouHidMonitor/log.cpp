/*++

Copyright (c) 2019 changeofpace. All rights reserved.

Use of this source code is governed by the MIT license. See the 'LICENSE' file
for more information.

--*/

#include "log.h"

#define NTSTRSAFE_NO_CB_FUNCTIONS

#include <cstdio>
#include <strsafe.h>

#include "debug.h"


//=============================================================================
// Constants
//=============================================================================
#define OUTPUT_BUFFER_CCH_MAX       512
#define MESSAGE_BUFFER_CCH_MAX      (OUTPUT_BUFFER_CCH_MAX - 80)

#define VALID_CONFIG_OUTPUT_MASK    (LOG_CONFIG_STDOUT | LOG_CONFIG_DEBUGGER)


//=============================================================================
// Private Types
//=============================================================================
typedef struct _LOG_CONTEXT {
    ULONG Config;
} LOG_CONTEXT, *PLOG_CONTEXT;


//=============================================================================
// Module Globals
//=============================================================================
static LOG_CONTEXT g_LogContext = {};


//=============================================================================
// Public Interface
//=============================================================================
_Use_decl_annotations_
BOOL
LogInitialization(
    ULONG Config
)
{
    BOOL status = TRUE;

    if ((~VALID_CONFIG_OUTPUT_MASK) & Config)
    {
        status = FALSE;
        goto exit;
    }

    //
    // Initialize the global context.
    //
    g_LogContext.Config = Config;

exit:
    return status;
}


_Use_decl_annotations_
HRESULT
LogPrint(
    LOG_LEVEL Level,
    ULONG Options,
    PCSTR pszFormat,
    ...
)
{
    PCSTR pszLevel = NULL;
    va_list VarArgs = {};
    CHAR szMessageBuffer[MESSAGE_BUFFER_CCH_MAX] = {};
    PCSTR pszOutputFormat = NULL;
    CHAR szOutputBuffer[OUTPUT_BUFFER_CCH_MAX] = {};
    int printstatus = 0;
    HRESULT hresult = S_OK;

    //
    // Set the log level prefix.
    //
    switch (Level)
    {
        case LogLevelDebug:     pszLevel = "DBG"; break;
        case LogLevelInfo:      pszLevel = "INF"; break;
        case LogLevelWarning:   pszLevel = "WRN"; break;
        case LogLevelError:     pszLevel = "ERR"; break;
        default:
            hresult = E_INVALIDARG;
            DEBUG_BREAK;
            goto exit;
    }

    va_start(VarArgs, pszFormat);
    hresult = StringCchVPrintfA(
        szMessageBuffer,
        RTL_NUMBER_OF(szMessageBuffer),
        pszFormat,
        VarArgs);
    va_end(VarArgs);
    if (FAILED(hresult))
    {
        DEBUG_BREAK;
        goto exit;
    }

    if (LOG_OPTION_APPEND_CRLF & Options)
    {
        pszOutputFormat = "%s  %04u:%04u  %s\r\n";
    }
    else
    {
        pszOutputFormat = "%s  %04u:%04u  %s";
    }

    hresult = StringCchPrintfA(
        szOutputBuffer,
        RTL_NUMBER_OF(szOutputBuffer),
        pszOutputFormat,
        pszLevel,
        GetCurrentProcessId(),
        GetCurrentThreadId(),
        szMessageBuffer);
    if (FAILED(hresult))
    {
        DEBUG_BREAK;
        goto exit;
    }

    if (LOG_CONFIG_DEBUGGER & g_LogContext.Config)
    {
        OutputDebugStringA(szOutputBuffer);
    }

    if (LOG_CONFIG_STDOUT & g_LogContext.Config)
    {
        printstatus = printf("%s", szOutputBuffer);
        if (0 > printstatus)
        {
            hresult = E_FAIL;
            DEBUG_BREAK;
            goto exit;
        }
    }

exit:
    return hresult;
}
