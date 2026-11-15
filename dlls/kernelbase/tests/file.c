/*
 * Copyright (C) 2023 Paul Gofman for CodeWeavers
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

#include <stdarg.h>
#include <stdlib.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <ioringapi.h>

#include "wine/test.h"

static HRESULT (WINAPI *pQueryIoRingCapabilities)(IORING_CAPABILITIES *);

static void test_ioring_caps(void)
{
    IORING_CAPABILITIES caps;
    HRESULT hr;

    if (!pQueryIoRingCapabilities)
    {
        win_skip("QueryIoRingCapabilities is not available, skipping tests.\n");
        return;
    }

    memset(&caps, 0xcc, sizeof(caps));
    hr = pQueryIoRingCapabilities(&caps);
    todo_wine ok(hr == S_OK, "got %#lx.\n", hr);
}

static void test_extended_file_attributes(void)
{
    static const DWORD ext_mask = FILE_ATTRIBUTE_TEMPORARY |
                                  FILE_ATTRIBUTE_OFFLINE |
                                  FILE_ATTRIBUTE_NOT_CONTENT_INDEXED |
                                  FILE_ATTRIBUTE_INTEGRITY_STREAM |
                                  FILE_ATTRIBUTE_NO_SCRUB_DATA;
    WCHAR temp_dir[MAX_PATH], path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA data;
    HANDLE h;
    BOOL ret;

    GetTempPathW(ARRAY_SIZE(temp_dir), temp_dir);
    ret = GetTempFileNameW(temp_dir, L"xat", 0, path);
    ok(ret, "GetTempFileNameW failed, error %lu\n", GetLastError());

    ret = SetFileAttributesW(path, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_OFFLINE);
    ok(ret, "SetFileAttributesW failed, error %lu\n", GetLastError());

    memset(&data, 0xcc, sizeof(data));
    ret = GetFileAttributesExW(path, GetFileExInfoStandard, &data);
    ok(ret, "GetFileAttributesExW failed, error %lu\n", GetLastError());
    ok((data.dwFileAttributes & (FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_OFFLINE)) ==
       (FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_OFFLINE),
       "got attributes %#lx\n", data.dwFileAttributes);

    ret = SetFileAttributesW(path, ext_mask);
    ok(ret, "SetFileAttributesW(ext_mask) failed, error %lu\n", GetLastError());

    memset(&data, 0xcc, sizeof(data));
    ret = GetFileAttributesExW(path, GetFileExInfoStandard, &data);
    ok(ret, "GetFileAttributesExW failed, error %lu\n", GetLastError());
    ok((data.dwFileAttributes & ext_mask) == ext_mask,
       "expected all ext bits %#lx, got %#lx\n", ext_mask, data.dwFileAttributes);

    ret = SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    ok(ret, "SetFileAttributesW(NORMAL) failed, error %lu\n", GetLastError());
    ret = GetFileAttributesExW(path, GetFileExInfoStandard, &data);
    ok(ret, "GetFileAttributesExW failed, error %lu\n", GetLastError());
    ok(!(data.dwFileAttributes & ext_mask),
       "expected cleared ext bits, got %#lx\n", data.dwFileAttributes);

    ret = SetFileAttributesW(path, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_OFFLINE);
    ok(ret, "SetFileAttributesW failed, error %lu\n", GetLastError());
    ret = DeleteFileW(path);
    ok(ret, "DeleteFileW failed, error %lu\n", GetLastError());

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(h != INVALID_HANDLE_VALUE, "CreateFileW failed, error %lu\n", GetLastError());
    CloseHandle(h);

    ret = GetFileAttributesExW(path, GetFileExInfoStandard, &data);
    ok(ret, "GetFileAttributesExW failed, error %lu\n", GetLastError());
    ok(!(data.dwFileAttributes & ext_mask),
       "expected fresh file without ext bits, got %#lx\n", data.dwFileAttributes);
    DeleteFileW(path);

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_NO_SCRUB_DATA,
                    NULL);
    ok(h != INVALID_HANDLE_VALUE, "CreateFileW with ext attrs failed, error %lu\n", GetLastError());
    CloseHandle(h);

    ret = GetFileAttributesExW(path, GetFileExInfoStandard, &data);
    ok(ret, "GetFileAttributesExW failed, error %lu\n", GetLastError());
    ok((data.dwFileAttributes & (FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_NO_SCRUB_DATA)) ==
       (FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_NO_SCRUB_DATA),
       "expected creation ext bits preserved, got %#lx\n", data.dwFileAttributes);

    DeleteFileW(path);
}

static void test_zone_identifier_ads(void)
{
    static const char motw[] = "[ZoneTransfer]\r\nZoneId=3\r\n";
    WCHAR temp_dir[MAX_PATH], path[MAX_PATH], stream_path[MAX_PATH + 32];
    WIN32_FILE_ATTRIBUTE_DATA data;
    char buf[64];
    DWORD bytes;
    HANDLE h;
    BOOL ret;

    GetTempPathW(ARRAY_SIZE(temp_dir), temp_dir);
    ret = GetTempFileNameW(temp_dir, L"mtw", 0, path);
    ok(ret, "GetTempFileNameW failed, error %lu\n", GetLastError());

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(h != INVALID_HANDLE_VALUE, "CreateFileW base failed, error %lu\n", GetLastError());
    ret = WriteFile(h, "hello", 5, &bytes, NULL);
    ok(ret && bytes == 5, "WriteFile base failed, error %lu\n", GetLastError());
    CloseHandle(h);

    wcscpy(stream_path, path);
    wcscat(stream_path, L":Zone.Identifier");

    SetLastError(0xdeadbeef);
    ret = GetFileAttributesExW(stream_path, GetFileExInfoStandard, &data);
    ok(!ret, "GetFileAttributesExW on absent Zone.Identifier should fail\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "got error %lu\n", GetLastError());

    h = CreateFileW(stream_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(h != INVALID_HANDLE_VALUE, "CreateFileW(Zone.Identifier write) failed, error %lu\n", GetLastError());
    ret = WriteFile(h, motw, sizeof(motw) - 1, &bytes, NULL);
    ok(ret && bytes == sizeof(motw) - 1, "WriteFile(Zone.Identifier) failed, error %lu\n", GetLastError());
    CloseHandle(h);

    memset(&data, 0, sizeof(data));
    ret = GetFileAttributesExW(stream_path, GetFileExInfoStandard, &data);
    ok(ret, "GetFileAttributesExW(Zone.Identifier) failed, error %lu\n", GetLastError());
    ok(data.nFileSizeLow == sizeof(motw) - 1 && data.nFileSizeHigh == 0,
       "expected size %Iu, got %lu:%lu\n", sizeof(motw) - 1, data.nFileSizeHigh, data.nFileSizeLow);

    h = CreateFileW(stream_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(h != INVALID_HANDLE_VALUE, "CreateFileW(Zone.Identifier read) failed, error %lu\n", GetLastError());
    memset(buf, 0, sizeof(buf));
    ret = ReadFile(h, buf, sizeof(buf), &bytes, NULL);
    ok(ret, "ReadFile(Zone.Identifier) failed, error %lu\n", GetLastError());
    ok(bytes == sizeof(motw) - 1, "expected %Iu bytes, got %lu\n", sizeof(motw) - 1, bytes);
    ok(!memcmp(buf, motw, sizeof(motw) - 1), "Zone.Identifier content mismatch: %s\n", buf);
    CloseHandle(h);

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(h != INVALID_HANDLE_VALUE, "CreateFileW base read failed, error %lu\n", GetLastError());
    memset(buf, 0, sizeof(buf));
    ret = ReadFile(h, buf, sizeof(buf), &bytes, NULL);
    ok(ret && bytes == 5 && !memcmp(buf, "hello", 5), "base file corrupted: bytes=%lu\n", bytes);
    CloseHandle(h);

    ret = DeleteFileW(stream_path);
    ok(ret, "DeleteFileW(Zone.Identifier) failed, error %lu\n", GetLastError());
    ret = GetFileAttributesExW(stream_path, GetFileExInfoStandard, &data);
    ok(!ret && GetLastError() == ERROR_FILE_NOT_FOUND, "Zone.Identifier still present after delete\n");

    DeleteFileW(path);
}

START_TEST(file)
{
    HMODULE hmod;

    hmod = LoadLibraryA("kernelbase.dll");
    pQueryIoRingCapabilities = (void *)GetProcAddress(hmod, "QueryIoRingCapabilities");

    test_ioring_caps();
    test_extended_file_attributes();
    test_zone_identifier_ads();
}
