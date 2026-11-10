/*
 * Copyright 2017 Jactry Zeng for CodeWeavers
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
#include <ctype.h>
#include <wchar.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(powershell);

HRESULT WINAPI AppxRegisterPackageManifest(const WCHAR *manifest_path);

static BOOL handle_add_appx_package(int argc, WCHAR *argv[])
{
    int i, j;
    BOOL found_cmd = FALSE;
    WCHAR path[MAX_PATH] = {0};

    for (i = 1; i < argc; i++)
    {
        if (!argv[i]) continue;
        if (wcsstr(argv[i], L"Add-AppxPackage") || wcsstr(argv[i], L"add-appxpackage") ||
            wcsstr(argv[i], L"ADD-APPXPACKAGE"))
        {
            found_cmd = TRUE;
            break;
        }
    }
    if (!found_cmd) return FALSE;

    /* Check 1: single quotes '...' inside a single argument or spanning arguments */
    for (i = 1; i < argc; i++)
    {
        const WCHAR *str = argv[i];
        const WCHAR *q1 = wcschr(str, L'\'');
        if (q1)
        {
            const WCHAR *q2 = wcschr(q1 + 1, L'\'');
            if (q2)
            {
                size_t n = q2 - q1 - 1;
                if (n < MAX_PATH)
                {
                    memcpy(path, q1 + 1, n * sizeof(WCHAR));
                    path[n] = 0;
                    break;
                }
            }
            else
            {
                lstrcpynW(path, q1 + 1, MAX_PATH);
                for (j = i + 1; j < argc; j++)
                {
                    const WCHAR *end_q = wcschr(argv[j], L'\'');
                    lstrcatW(path, L" ");
                    if (end_q)
                    {
                        size_t cur_len = lstrlenW(path);
                        size_t part_len = end_q - argv[j];
                        if (cur_len + part_len < MAX_PATH)
                        {
                            memcpy(path + cur_len, argv[j], part_len * sizeof(WCHAR));
                            path[cur_len + part_len] = 0;
                        }
                        break;
                    }
                    else
                    {
                        lstrcatW(path, argv[j]);
                    }
                }
                if (path[0]) break;
            }
        }

        /* Double quotes "..." */
        q1 = wcschr(str, L'"');
        if (q1)
        {
            const WCHAR *q2 = wcschr(q1 + 1, L'"');
            if (q2)
            {
                size_t n = q2 - q1 - 1;
                if (n < MAX_PATH)
                {
                    memcpy(path, q1 + 1, n * sizeof(WCHAR));
                    path[n] = 0;
                    break;
                }
            }
            else
            {
                lstrcpynW(path, q1 + 1, MAX_PATH);
                for (j = i + 1; j < argc; j++)
                {
                    const WCHAR *end_q = wcschr(argv[j], L'"');
                    lstrcatW(path, L" ");
                    if (end_q)
                    {
                        size_t cur_len = lstrlenW(path);
                        size_t part_len = end_q - argv[j];
                        if (cur_len + part_len < MAX_PATH)
                        {
                            memcpy(path + cur_len, argv[j], part_len * sizeof(WCHAR));
                            path[cur_len + part_len] = 0;
                        }
                        break;
                    }
                    else
                    {
                        lstrcatW(path, argv[j]);
                    }
                }
                if (path[0]) break;
            }
        }
    }

    /* Check 2: if no quoted string found, look for tokens following -Register or -Path */
    if (!path[0])
    {
        for (i = 1; i < argc; i++)
        {
            const WCHAR *arg = argv[i];
            if (!wcsicmp(arg, L"-Register") || !wcsicmp(arg, L"/Register") ||
                !wcsicmp(arg, L"-register") || !wcsicmp(arg, L"--register") ||
                !wcsicmp(arg, L"-Path") || !wcsicmp(arg, L"/Path") ||
                !wcsicmp(arg, L"-path"))
            {
                if (i + 1 < argc && argv[i + 1][0] != L'-' && argv[i + 1][0] != L'/')
                {
                    lstrcpynW(path, argv[i + 1], MAX_PATH);
                    for (j = i + 2; j < argc; j++)
                    {
                        if (argv[j][0] == L'-' || argv[j][0] == L'/') break;
                        lstrcatW(path, L" ");
                        lstrcatW(path, argv[j]);
                    }
                    break;
                }
            }
        }
    }

    /* Check 3: search for tokens containing path separators or .xml */
    if (!path[0])
    {
        for (i = 1; i < argc; i++)
        {
            const WCHAR *arg = argv[i];
            if (!wcsicmp(arg, L"-command") || !wcsicmp(arg, L"/command")) continue;
            if (!wcsicmp(arg, L"Add-AppxPackage")) continue;
            if (arg[0] == L'-' || arg[0] == L'/') continue;

            if (wcschr(arg, L'\\') || wcschr(arg, L'/') || wcsstr(arg, L".xml") || wcsstr(arg, L".XML"))
            {
                lstrcpynW(path, arg, MAX_PATH);
                for (j = i + 1; j < argc; j++)
                {
                    if (argv[j][0] == L'-' || argv[j][0] == L'/') break;
                    lstrcatW(path, L" ");
                    lstrcatW(path, argv[j]);
                }
                break;
            }
        }
    }

    /* Strip any leading or trailing quotes/spaces */
    while (path[0] == L' ' || path[0] == L'\'' || path[0] == L'"')
        memmove(path, path + 1, (lstrlenW(path)) * sizeof(WCHAR));
    while (path[0] && (path[lstrlenW(path) - 1] == L' ' ||
                       path[lstrlenW(path) - 1] == L'\'' ||
                       path[lstrlenW(path) - 1] == L'"'))
        path[lstrlenW(path) - 1] = 0;

    if (path[0])
    {
        HRESULT hr;
        WINE_TRACE("Add-AppxPackage intercepted, registering manifest: %s\n", wine_dbgstr_w(path));
        hr = AppxRegisterPackageManifest(path);
        WINE_TRACE("AppxRegisterPackageManifest returned hr=0x%08lx\n", hr);
        return TRUE;
    }

    return FALSE;
}

int __cdecl wmain(int argc, WCHAR *argv[])
{
    int i;

    if (handle_add_appx_package(argc, argv))
        return 0;

    WINE_FIXME("stub.\n");
    for (i = 0; i < argc; i++)
    {
        WINE_FIXME("argv[%d] %s\n", i, wine_dbgstr_w(argv[i]));
        if (!wcsicmp(argv[i], L"-command") && i < argc - 1 && !wcscmp(argv[i + 1], L"-"))
        {
            char command[4096], *p;

            ++i;
            while (fgets(command, sizeof(command), stdin))
            {
                WINE_FIXME("command %s.\n", debugstr_a(command));
                p = command;
                while (*p && !isspace(*p)) ++p;
                *p = 0;
                if (!stricmp(command, "exit"))
                    break;
            }
        }
    }
    return 0;
}
