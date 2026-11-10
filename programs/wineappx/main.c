/*
 * Wine Appx/MSIX Package Registrar utility
 *
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
#include <stdlib.h>
#include <wchar.h>

HRESULT WINAPI AppxRegisterPackageManifest(const WCHAR *manifest_path);
HRESULT WINAPI AppxUnregisterPackageByName(const WCHAR *package_full_name);

static void print_usage(void)
{
    wprintf(L"Wine Appx/MSIX Package Registrar\n\n");
    wprintf(L"Usage:\n");
    wprintf(L"    wineappx.exe -r <path_to_AppxManifest.xml | package_directory>\n");
    wprintf(L"    wineappx.exe -u <package_full_name>\n\n");
    wprintf(L"Options:\n");
    wprintf(L"    -r, --register    Register an Appx/MSIX package\n");
    wprintf(L"    -u, --unregister  Unregister an Appx/MSIX package\n");
    wprintf(L"    -h, --help        Display this help message\n");
}

int __cdecl wmain(int argc, WCHAR *argv[])
{
    const WCHAR *cmd;

    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    cmd = argv[1];
    if (!wcscmp(cmd, L"-h") || !wcscmp(cmd, L"/h") || !wcscmp(cmd, L"--help") || !wcscmp(cmd, L"/?"))
    {
        print_usage();
        return 0;
    }

    if (!wcscmp(cmd, L"-r") || !wcscmp(cmd, L"/r") || !wcscmp(cmd, L"--register") || !wcscmp(cmd, L"-register"))
    {
        WCHAR dos_path[MAX_PATH];
        const WCHAR *input_path;
        HRESULT hr;

        if (argc < 3)
        {
            wprintf(L"Error: Missing path to AppxManifest.xml or package directory.\n");
            print_usage();
            return 1;
        }

        input_path = argv[2];
        if (input_path[0] == L'/')
        {
            typedef WCHAR * (CDECL *p_wine_get_dos_file_name)(const char *);
            p_wine_get_dos_file_name p_wine_get_dos = (void *)GetProcAddress(GetModuleHandleA("KERNEL32"), "wine_get_dos_file_name");
            if (p_wine_get_dos)
            {
                char unix_path[MAX_PATH];
                WCHAR *res;
                WideCharToMultiByte(CP_UNIXCP, 0, input_path, -1, unix_path, sizeof(unix_path), NULL, NULL);
                res = p_wine_get_dos(unix_path);
                if (res)
                {
                    lstrcpynW(dos_path, res, MAX_PATH);
                    HeapFree(GetProcessHeap(), 0, res);
                }
                else
                    lstrcpynW(dos_path, input_path, MAX_PATH);
            }
            else
                lstrcpynW(dos_path, input_path, MAX_PATH);
        }
        else
        {
            GetFullPathNameW(input_path, MAX_PATH, dos_path, NULL);
        }

        hr = AppxRegisterPackageManifest(dos_path);
        if (SUCCEEDED(hr))
        {
            wprintf(L"Package registered successfully.\n");
            return 0;
        }
        else
        {
            wprintf(L"Error: Registration failed (0x%08lx).\n", hr);
            return 4;
        }
    }
    else if (!wcscmp(cmd, L"-u") || !wcscmp(cmd, L"/u") || !wcscmp(cmd, L"--unregister") || !wcscmp(cmd, L"-unregister"))
    {
        const WCHAR *full_name;
        HRESULT hr;

        if (argc < 3)
        {
            wprintf(L"Error: Missing package full name.\n");
            print_usage();
            return 1;
        }

        full_name = argv[2];
        hr = AppxUnregisterPackageByName(full_name);
        if (SUCCEEDED(hr))
        {
            wprintf(L"Package '%ls' successfully unregistered.\n", full_name);
            return 0;
        }
        else
        {
            wprintf(L"Error: Unregistration failed (0x%08lx).\n", hr);
            return 4;
        }
    }
    else
    {
        wprintf(L"Error: Unknown option '%ls'.\n", cmd);
        print_usage();
        return 1;
    }
}
