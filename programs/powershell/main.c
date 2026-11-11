/*
 * Copyright 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(powershell);

static BOOL file_exists(const WCHAR *path)
{
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static const WCHAR *get_command_arguments(const WCHAR *cmdline)
{
    BOOL in_quotes = FALSE;
    const WCHAR *p = cmdline;

    if (!p) return L"";

    while (*p && (*p == L' ' || *p == L'\t')) p++;

    while (*p)
    {
        if (*p == L'"')
            in_quotes = !in_quotes;
        else if (!in_quotes && (*p == L' ' || *p == L'\t'))
        {
            while (*p == L' ' || *p == L'\t') p++;
            return p;
        }
        p++;
    }
    return p;
}

int __cdecl wmain(int argc, WCHAR *argv[])
{
    WCHAR target[MAX_PATH] = {0};
    WCHAR pf_path[MAX_PATH];
    WCHAR winepwsh[MAX_PATH];
    const WCHAR *args;
    WCHAR *child_cmd;
    size_t cmd_len;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD exit_code = 1;
    BOOL is_pwsh = FALSE;

    /* Check if pwsh is disabled via environment for testing */
    if (!GetEnvironmentVariableW(L"WINE_NO_PWSH", NULL, 0) &&
        !GetEnvironmentVariableW(L"WINE_DISABLE_PWSH", NULL, 0))
    {
        if (ExpandEnvironmentStringsW(L"%ProgramFiles%\\PowerShell\\7\\pwsh.exe", pf_path, MAX_PATH) &&
            file_exists(pf_path))
        {
            lstrcpyW(target, pf_path);
            is_pwsh = TRUE;
        }
        else if (!GetEnvironmentVariableW(L"WINE_NO_Z_PWSH", NULL, 0) &&
                 file_exists(L"Z:\\var\\tmp\\pwsh-win-x64\\pwsh.exe"))
        {
            lstrcpyW(target, L"Z:\\var\\tmp\\pwsh-win-x64\\pwsh.exe");
            is_pwsh = TRUE;
        }
    }

    if (!is_pwsh)
    {
        if (!SearchPathW(NULL, L"winepowershell.exe", NULL, MAX_PATH, winepwsh, NULL))
        {
            ExpandEnvironmentStringsW(L"%SystemRoot%\\system32\\winepowershell.exe", winepwsh, MAX_PATH);
        }
        lstrcpyW(target, winepwsh);
    }

    args = get_command_arguments(GetCommandLineW());
    cmd_len = lstrlenW(target) + lstrlenW(args) + 16;
    child_cmd = malloc(cmd_len * sizeof(WCHAR));
    if (!child_cmd) return 1;

    if (args && *args)
        swprintf(child_cmd, cmd_len, L"\"%s\" %s", target, args);
    else
        swprintf(child_cmd, cmd_len, L"\"%s\"", target);

    WINE_TRACE("forwarding command line to %s: %s\n", wine_dbgstr_w(target), wine_dbgstr_w(child_cmd));

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (CreateProcessW(is_pwsh ? target : NULL, is_pwsh ? child_cmd : (args && *args ? child_cmd : target),
                       NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi) ||
        CreateProcessW(target, child_cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi) ||
        CreateProcessW(NULL, child_cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else
    {
        WINE_ERR("failed to spawn %s (cmd: %s), err %lu\n",
                 wine_dbgstr_w(target), wine_dbgstr_w(child_cmd), GetLastError());
    }

    free(child_cmd);
    return exit_code;
}
