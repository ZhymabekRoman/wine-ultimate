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

#define COBJMACROS

#include <windows.h>
#include <initguid.h>
#include <ole2.h>
#include <xmllite.h>
#include <shlwapi.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(wineappx);

struct appx_identity
{
    WCHAR name[256];
    WCHAR publisher[512];
    WCHAR version[64];
    WCHAR arch[64];
    WCHAR resource_id[64];
    WCHAR publisher_id[64];
    WCHAR full_name[512];
    WCHAR family_name[512];
};

struct appx_application
{
    WCHAR id[128];
    WCHAR executable[256];
};

struct appx_dependency
{
    WCHAR name[256];
    WCHAR min_version[64];
    WCHAR publisher[512];
};

struct appx_activatable_class
{
    WCHAR class_id[256];
    WCHAR in_proc_server[MAX_PATH];
    WCHAR threading_model[32];
};

struct appx_package
{
    struct appx_identity identity;
    BOOL is_framework;
    WCHAR package_path[MAX_PATH];

    struct appx_application *applications;
    DWORD applications_count;
    DWORD applications_capacity;

    struct appx_dependency *dependencies;
    DWORD dependencies_count;
    DWORD dependencies_capacity;

    struct appx_activatable_class *classes;
    DWORD classes_count;
    DWORD classes_capacity;
};

static BOOL add_application(struct appx_package *pkg, const WCHAR *id, const WCHAR *executable)
{
    const WCHAR *p;
    if (pkg->applications_count >= pkg->applications_capacity)
    {
        DWORD cap = pkg->applications_capacity ? pkg->applications_capacity * 2 : 8;
        void *mem = realloc(pkg->applications, cap * sizeof(*pkg->applications));
        if (!mem) return FALSE;
        pkg->applications = mem;
        pkg->applications_capacity = cap;
    }
    lstrcpynW(pkg->applications[pkg->applications_count].id, id ? id : L"", 128);
    p = wcsrchr(executable, L'\\');
    if (!p) p = wcsrchr(executable, L'/');
    p = p ? p + 1 : executable;
    lstrcpynW(pkg->applications[pkg->applications_count].executable, p, 256);
    pkg->applications_count++;
    return TRUE;
}

static BOOL add_dependency(struct appx_package *pkg, const WCHAR *name, const WCHAR *min_ver, const WCHAR *pub)
{
    if (pkg->dependencies_count >= pkg->dependencies_capacity)
    {
        DWORD cap = pkg->dependencies_capacity ? pkg->dependencies_capacity * 2 : 16;
        void *mem = realloc(pkg->dependencies, cap * sizeof(*pkg->dependencies));
        if (!mem) return FALSE;
        pkg->dependencies = mem;
        pkg->dependencies_capacity = cap;
    }
    lstrcpynW(pkg->dependencies[pkg->dependencies_count].name, name ? name : L"", 256);
    lstrcpynW(pkg->dependencies[pkg->dependencies_count].min_version, min_ver ? min_ver : L"", 64);
    lstrcpynW(pkg->dependencies[pkg->dependencies_count].publisher, pub ? pub : L"", 512);
    pkg->dependencies_count++;
    return TRUE;
}

static BOOL add_activatable_class(struct appx_package *pkg, const WCHAR *class_id, const WCHAR *in_proc_server, const WCHAR *threading_model)
{
    if (pkg->classes_count >= pkg->classes_capacity)
    {
        DWORD cap = pkg->classes_capacity ? pkg->classes_capacity * 2 : 64;
        void *mem = realloc(pkg->classes, cap * sizeof(*pkg->classes));
        if (!mem) return FALSE;
        pkg->classes = mem;
        pkg->classes_capacity = cap;
    }
    lstrcpynW(pkg->classes[pkg->classes_count].class_id, class_id ? class_id : L"", 256);
    lstrcpynW(pkg->classes[pkg->classes_count].in_proc_server, in_proc_server ? in_proc_server : L"", MAX_PATH);
    lstrcpynW(pkg->classes[pkg->classes_count].threading_model,
              (threading_model && threading_model[0]) ? threading_model : L"Both", 32);
    pkg->classes_count++;
    return TRUE;
}

static void crockford_base32_encode(const BYTE *hash8, WCHAR *out)
{
    static const WCHAR alphabet[] = L"0123456789abcdefghjkmnpqrstvwxyz";
    UINT64 val = 0;
    int i;

    for (i = 0; i < 8; i++)
        val = (val << 8) | hash8[i];

    for (i = 0; i < 13; i++)
    {
        int shift = 64 - 5 * (i + 1);
        if (shift >= 0)
            out[i] = alphabet[(val >> shift) & 0x1f];
        else
            out[i] = alphabet[(val << (-shift)) & 0x1f];
    }
    out[13] = 0;
}

static BOOL derive_publisher_id(const WCHAR *publisher, WCHAR *out, DWORD cch)
{
    DWORD der_size = 0;
    BYTE *der_bytes = NULL;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE hash[32];
    DWORD hash_len = sizeof(hash);
    BOOL ret = FALSE;

    if (!publisher || !publisher[0] || cch < 14) return FALSE;

    if (wcsstr(publisher, L"Microsoft Corporation"))
    {
        lstrcpynW(out, L"8wekyb3d8bbwe", cch);
        return TRUE;
    }

    if (CertStrToNameW(X509_ASN_ENCODING, publisher, CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG,
                       NULL, NULL, &der_size, NULL) && der_size > 0)
    {
        der_bytes = malloc(der_size);
        if (der_bytes && CertStrToNameW(X509_ASN_ENCODING, publisher,
                                        CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG,
                                        NULL, der_bytes, &der_size, NULL))
        {
            if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            {
                if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
                {
                    if (CryptHashData(hHash, der_bytes, der_size, 0) &&
                        CryptGetHashParam(hHash, HP_HASHVAL, hash, &hash_len, 0) &&
                        hash_len >= 8)
                    {
                        crockford_base32_encode(hash, out);
                        ret = TRUE;
                    }
                    CryptDestroyHash(hHash);
                }
                CryptReleaseContext(hProv, 0);
            }
        }
        free(der_bytes);
    }

    if (!ret)
    {
        if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
            {
                if (CryptHashData(hHash, (const BYTE *)publisher, lstrlenW(publisher) * sizeof(WCHAR), 0) &&
                    CryptGetHashParam(hHash, HP_HASHVAL, hash, &hash_len, 0) &&
                    hash_len >= 8)
                {
                    crockford_base32_encode(hash, out);
                    ret = TRUE;
                }
                CryptDestroyHash(hHash);
            }
            CryptReleaseContext(hProv, 0);
        }
    }

    return ret;
}

static void determine_package_identity(const WCHAR *package_path, struct appx_package *pkg)
{
    const WCHAR *folder = wcsrchr(package_path, L'\\');
    DWORD name_len;
    BOOL folder_matches = FALSE;

    if (!folder) folder = wcsrchr(package_path, L'/');
    folder = folder ? folder + 1 : package_path;

    name_len = lstrlenW(pkg->identity.name);
    if (name_len > 0 && !wcsncmp(folder, pkg->identity.name, name_len) && folder[name_len] == L'_')
    {
        if (pkg->identity.version[0] && wcsstr(folder, pkg->identity.version))
            folder_matches = TRUE;
    }

    if (folder_matches)
    {
        const WCHAR *pub_id;
        lstrcpynW(pkg->identity.full_name, folder, ARRAY_SIZE(pkg->identity.full_name));
        pub_id = wcsrchr(folder, L'_');
        if (pub_id && pub_id[1])
            lstrcpynW(pkg->identity.publisher_id, pub_id + 1, ARRAY_SIZE(pkg->identity.publisher_id));
    }

    if (!pkg->identity.publisher_id[0])
    {
        derive_publisher_id(pkg->identity.publisher, pkg->identity.publisher_id,
                            ARRAY_SIZE(pkg->identity.publisher_id));
    }

    swprintf(pkg->identity.family_name, ARRAY_SIZE(pkg->identity.family_name),
             L"%ls_%ls", pkg->identity.name, pkg->identity.publisher_id);

    if (!pkg->identity.full_name[0])
    {
        const WCHAR *arch = pkg->identity.arch[0] ? pkg->identity.arch : L"neutral";
        const WCHAR *res = pkg->identity.resource_id[0] ? pkg->identity.resource_id : L"";
        if (res[0])
        {
            swprintf(pkg->identity.full_name, ARRAY_SIZE(pkg->identity.full_name),
                     L"%ls_%ls_%ls_%ls_%ls",
                     pkg->identity.name, pkg->identity.version, arch, res, pkg->identity.publisher_id);
        }
        else
        {
            swprintf(pkg->identity.full_name, ARRAY_SIZE(pkg->identity.full_name),
                     L"%ls_%ls_%ls__%ls",
                     pkg->identity.name, pkg->identity.version, arch, pkg->identity.publisher_id);
        }
    }
}

static BOOL parse_appx_manifest(const WCHAR *manifest_path, struct appx_package *pkg)
{
    HANDLE hFile;
    DWORD file_size, read_bytes;
    char *buf;
    HGLOBAL hMem;
    IStream *stream = NULL;
    IXmlReader *reader = NULL;
    HRESULT hr;
    XmlNodeType node_type;
    BOOL in_properties = FALSE;
    BOOL in_framework = FALSE;
    BOOL in_inproc_server = FALSE;
    BOOL in_path = FALSE;
    DWORD server_class_start = 0;
    WCHAR current_inproc_server[MAX_PATH] = {0};

    hFile = CreateFileW(manifest_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        wprintf(L"Error: Failed to open '%ls' (error %lu)\n", manifest_path, GetLastError());
        return FALSE;
    }

    file_size = GetFileSize(hFile, NULL);
    if (file_size == INVALID_FILE_SIZE || file_size == 0)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    buf = malloc(file_size);
    if (!buf)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    if (!ReadFile(hFile, buf, file_size, &read_bytes, NULL) || read_bytes == 0)
    {
        free(buf);
        CloseHandle(hFile);
        return FALSE;
    }
    CloseHandle(hFile);

    hMem = GlobalAlloc(GMEM_MOVEABLE, read_bytes);
    if (!hMem)
    {
        free(buf);
        return FALSE;
    }
    memcpy(GlobalLock(hMem), buf, read_bytes);
    GlobalUnlock(hMem);
    free(buf);

    hr = CreateStreamOnHGlobal(hMem, TRUE, &stream);
    if (FAILED(hr)) return FALSE;

    hr = CreateXmlReader(&IID_IXmlReader, (void **)&reader, NULL);
    if (FAILED(hr))
    {
        IStream_Release(stream);
        return FALSE;
    }

    hr = IXmlReader_SetInput(reader, (IUnknown *)stream);
    if (FAILED(hr))
    {
        IXmlReader_Release(reader);
        IStream_Release(stream);
        return FALSE;
    }

    while (IXmlReader_Read(reader, &node_type) == S_OK)
    {
        const WCHAR *local_name = NULL;

        switch (node_type)
        {
        case XmlNodeType_Element:
            IXmlReader_GetLocalName(reader, &local_name, NULL);
            if (!local_name) break;

            if (!wcscmp(local_name, L"Identity"))
            {
                if (IXmlReader_MoveToFirstAttribute(reader) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName(reader, &aname, NULL);
                        IXmlReader_GetValue(reader, &aval, NULL);
                        if (aname && aval)
                        {
                            if (!wcscmp(aname, L"Name"))
                                lstrcpynW(pkg->identity.name, aval, ARRAY_SIZE(pkg->identity.name));
                            else if (!wcscmp(aname, L"Publisher"))
                                lstrcpynW(pkg->identity.publisher, aval, ARRAY_SIZE(pkg->identity.publisher));
                            else if (!wcscmp(aname, L"Version"))
                                lstrcpynW(pkg->identity.version, aval, ARRAY_SIZE(pkg->identity.version));
                            else if (!wcscmp(aname, L"ProcessorArchitecture"))
                                lstrcpynW(pkg->identity.arch, aval, ARRAY_SIZE(pkg->identity.arch));
                            else if (!wcscmp(aname, L"ResourceId"))
                                lstrcpynW(pkg->identity.resource_id, aval, ARRAY_SIZE(pkg->identity.resource_id));
                        }
                    } while (IXmlReader_MoveToNextAttribute(reader) == S_OK);
                    IXmlReader_MoveToElement(reader);
                }
            }
            else if (!wcscmp(local_name, L"Properties"))
            {
                in_properties = TRUE;
            }
            else if (in_properties && !wcscmp(local_name, L"Framework"))
            {
                in_framework = TRUE;
            }
            else if (!wcscmp(local_name, L"PackageDependency"))
            {
                WCHAR dep_name[256] = {0};
                WCHAR min_ver[64] = {0};
                WCHAR pub[512] = {0};

                if (IXmlReader_MoveToFirstAttribute(reader) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName(reader, &aname, NULL);
                        IXmlReader_GetValue(reader, &aval, NULL);
                        if (aname && aval)
                        {
                            if (!wcscmp(aname, L"Name"))
                                lstrcpynW(dep_name, aval, ARRAY_SIZE(dep_name));
                            else if (!wcscmp(aname, L"MinVersion"))
                                lstrcpynW(min_ver, aval, ARRAY_SIZE(min_ver));
                            else if (!wcscmp(aname, L"Publisher"))
                                lstrcpynW(pub, aval, ARRAY_SIZE(pub));
                        }
                    } while (IXmlReader_MoveToNextAttribute(reader) == S_OK);
                    IXmlReader_MoveToElement(reader);
                }
                if (dep_name[0])
                    add_dependency(pkg, dep_name, min_ver, pub);
            }
            else if (!wcscmp(local_name, L"Application"))
            {
                WCHAR app_id[128] = {0};
                WCHAR app_exe[256] = {0};

                if (IXmlReader_MoveToFirstAttribute(reader) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName(reader, &aname, NULL);
                        IXmlReader_GetValue(reader, &aval, NULL);
                        if (aname && aval)
                        {
                            if (!wcscmp(aname, L"Id"))
                                lstrcpynW(app_id, aval, ARRAY_SIZE(app_id));
                            else if (!wcscmp(aname, L"Executable"))
                                lstrcpynW(app_exe, aval, ARRAY_SIZE(app_exe));
                        }
                    } while (IXmlReader_MoveToNextAttribute(reader) == S_OK);
                    IXmlReader_MoveToElement(reader);
                }
                if (app_exe[0])
                    add_application(pkg, app_id, app_exe);
            }
            else if (!wcscmp(local_name, L"InProcessServer"))
            {
                in_inproc_server = TRUE;
                server_class_start = pkg->classes_count;
                current_inproc_server[0] = 0;

                if (IXmlReader_MoveToFirstAttribute(reader) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName(reader, &aname, NULL);
                        IXmlReader_GetValue(reader, &aval, NULL);
                        if (aname && aval && !wcscmp(aname, L"Path"))
                            lstrcpynW(current_inproc_server, aval, ARRAY_SIZE(current_inproc_server));
                    } while (IXmlReader_MoveToNextAttribute(reader) == S_OK);
                    IXmlReader_MoveToElement(reader);
                }
            }
            else if (in_inproc_server && !wcscmp(local_name, L"Path"))
            {
                in_path = TRUE;
            }
            else if (in_inproc_server && !wcscmp(local_name, L"ActivatableClass"))
            {
                WCHAR class_id[256] = {0};
                WCHAR threading_model[32] = {0};

                if (IXmlReader_MoveToFirstAttribute(reader) == S_OK)
                {
                    do
                    {
                        const WCHAR *aname = NULL, *aval = NULL;
                        IXmlReader_GetLocalName(reader, &aname, NULL);
                        IXmlReader_GetValue(reader, &aval, NULL);
                        if (aname && aval)
                        {
                            if (!wcscmp(aname, L"ActivatableClassId"))
                                lstrcpynW(class_id, aval, ARRAY_SIZE(class_id));
                            else if (!wcscmp(aname, L"ThreadingModel"))
                                lstrcpynW(threading_model, aval, ARRAY_SIZE(threading_model));
                        }
                    } while (IXmlReader_MoveToNextAttribute(reader) == S_OK);
                    IXmlReader_MoveToElement(reader);
                }
                if (class_id[0])
                    add_activatable_class(pkg, class_id, current_inproc_server, threading_model);
            }
            break;

        case XmlNodeType_Text:
            if (in_path)
            {
                const WCHAR *val = NULL;
                IXmlReader_GetValue(reader, &val, NULL);
                if (val)
                    lstrcpynW(current_inproc_server, val, ARRAY_SIZE(current_inproc_server));
            }
            else if (in_framework)
            {
                const WCHAR *val = NULL;
                IXmlReader_GetValue(reader, &val, NULL);
                if (val && wcsstr(val, L"true"))
                    pkg->is_framework = TRUE;
            }
            break;

        case XmlNodeType_EndElement:
            IXmlReader_GetLocalName(reader, &local_name, NULL);
            if (!local_name) break;

            if (!wcscmp(local_name, L"Properties"))
                in_properties = FALSE;
            else if (!wcscmp(local_name, L"Framework"))
                in_framework = FALSE;
            else if (!wcscmp(local_name, L"Path"))
                in_path = FALSE;
            else if (!wcscmp(local_name, L"InProcessServer"))
            {
                DWORD j;
                for (j = server_class_start; j < pkg->classes_count; j++)
                {
                    if (!pkg->classes[j].in_proc_server[0] && current_inproc_server[0])
                        lstrcpynW(pkg->classes[j].in_proc_server, current_inproc_server, MAX_PATH);
                }
                in_inproc_server = FALSE;
                current_inproc_server[0] = 0;
            }
            break;

        default:
            break;
        }
    }

    IXmlReader_Release(reader);
    IStream_Release(stream);
    return TRUE;
}

static void set_reg_str(HKEY hkey, const WCHAR *name, const WCHAR *value)
{
    if (value)
        RegSetValueExW(hkey, name, 0, REG_SZ, (const BYTE *)value, (lstrlenW(value) + 1) * sizeof(WCHAR));
}

static void set_reg_dword(HKEY hkey, const WCHAR *name, DWORD value)
{
    RegSetValueExW(hkey, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

static BOOL register_package(const struct appx_package *pkg)
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR key[600];
    HKEY hkey;
    LONG ret;
    DWORD r, i;

    wprintf(L"Registering package: %ls\n", pkg->identity.full_name);
    wprintf(L"  PackagePath: %ls\n", pkg->package_path);
    wprintf(L"  PackageFamilyName: %ls\n", pkg->identity.family_name);
    wprintf(L"  PackagePublisherId: %ls\n", pkg->identity.publisher_id);
    if (pkg->is_framework) wprintf(L"  (Framework package)\n");

    for (r = 0; r < ARRAY_SIZE(roots); r++)
    {
        HKEY root = roots[r];

        /* 1. Software\Microsoft\Windows NT\CurrentVersion\AppModel\Packages\<PackageFullName> */
        swprintf(key, ARRAY_SIZE(key),
                 L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\%ls",
                 pkg->identity.full_name);
        ret = RegCreateKeyExW(root, key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
        if (ret == ERROR_SUCCESS)
        {
            set_reg_str(hkey, L"PackagePath", pkg->package_path);
            set_reg_str(hkey, L"PackageRootPath", pkg->package_path);
            set_reg_str(hkey, L"PackageFullName", pkg->identity.full_name);
            set_reg_str(hkey, L"PackageFamilyName", pkg->identity.family_name);
            set_reg_str(hkey, L"PackagePublisher", pkg->identity.publisher);
            set_reg_str(hkey, L"PackagePublisherId", pkg->identity.publisher_id);
            set_reg_dword(hkey, L"Framework", pkg->is_framework ? 1 : 0);
            RegCloseKey(hkey);
        }
        else
        {
            WARN("Failed to create Packages key in hive %lu: %ld\n", r, ret);
        }

        /* 2. Software\Microsoft\Windows NT\CurrentVersion\AppModel\Exes\<Executable> */
        for (i = 0; i < pkg->applications_count; i++)
        {
            WCHAR aumid[512];
            swprintf(key, ARRAY_SIZE(key),
                     L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Exes\\%ls",
                     pkg->applications[i].executable);
            ret = RegCreateKeyExW(root, key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
            if (ret == ERROR_SUCCESS)
            {
                swprintf(aumid, ARRAY_SIZE(aumid), L"%ls!%ls",
                         pkg->identity.family_name, pkg->applications[i].id);
                set_reg_str(hkey, L"PackageFullName", pkg->identity.full_name);
                set_reg_str(hkey, L"PackagePath", pkg->package_path);
                set_reg_str(hkey, L"ApplicationUserModelId", aumid);
                RegCloseKey(hkey);
            }
        }

        /* 3. Software\Microsoft\Windows NT\CurrentVersion\AppModel\Activation\<ActivatableClassId>
         * AND Software\Microsoft\WindowsRuntime\ActivatableClassId\<ActivatableClassId> */
        for (i = 0; i < pkg->classes_count; i++)
        {
            WCHAR dll_path[MAX_PATH];
            const WCHAR *inproc = pkg->classes[i].in_proc_server;

            if (inproc[0] && (inproc[1] == L':' || (inproc[0] == L'\\' && inproc[1] == L'\\')))
                lstrcpynW(dll_path, inproc, MAX_PATH);
            else
                swprintf(dll_path, MAX_PATH, L"%ls\\%ls", pkg->package_path, inproc);

            /* AppModel\Activation */
            swprintf(key, ARRAY_SIZE(key),
                     L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Activation\\%ls",
                     pkg->classes[i].class_id);
            ret = RegCreateKeyExW(root, key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
            if (ret == ERROR_SUCCESS)
            {
                set_reg_str(hkey, L"PackageFullName", pkg->identity.full_name);
                set_reg_str(hkey, L"InProcessServer", dll_path);
                RegCloseKey(hkey);
            }

            /* WindowsRuntime\ActivatableClassId */
            swprintf(key, ARRAY_SIZE(key),
                     L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\%ls",
                     pkg->classes[i].class_id);
            ret = RegCreateKeyExW(root, key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL);
            if (ret == ERROR_SUCCESS)
            {
                set_reg_str(hkey, L"DllPath", dll_path);
                set_reg_str(hkey, L"ThreadingModel",
                            pkg->classes[i].threading_model[0] ? pkg->classes[i].threading_model : L"Both");
                set_reg_str(hkey, L"PackageFullName", pkg->identity.full_name);
                RegCloseKey(hkey);
            }
        }
    }

    wprintf(L"Registered %lu application(s) and %lu activatable class(es).\n",
            pkg->applications_count, pkg->classes_count);

    {
        HMODULE hMod = LoadLibraryW(L"appxdeploymentclient.dll");
        if (hMod)
        {
            HRESULT (WINAPI *pAppxRegisterPackage)(const WCHAR *, const WCHAR *, const WCHAR *) =
                (void *)GetProcAddress(hMod, "AppxRegisterPackage");
            if (pAppxRegisterPackage)
                pAppxRegisterPackage(pkg->identity.full_name, pkg->package_path, pkg->identity.family_name);
            FreeLibrary(hMod);
        }
    }

    return TRUE;
}

static void append_to_path(const WCHAR *new_dir)
{
    HKEY hkey;
    LONG ret;
    DWORD type = 0, size = 0;
    WCHAR *old_path = NULL;
    WCHAR *new_path = NULL;
    DWORD new_len;
    DWORD_PTR dwResult;

    if (!new_dir || !new_dir[0]) return;

    ret = RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, NULL, 0,
                          KEY_READ | KEY_WRITE, NULL, &hkey, NULL);
    if (ret != ERROR_SUCCESS) return;

    if (RegQueryValueExW(hkey, L"Path", NULL, &type, NULL, &size) == ERROR_SUCCESS && size > 0)
    {
        old_path = malloc(size + sizeof(WCHAR));
        if (old_path)
        {
            RegQueryValueExW(hkey, L"Path", NULL, &type, (BYTE *)old_path, &size);
            old_path[size / sizeof(WCHAR)] = 0;
        }
    }

    if (old_path && old_path[0])
    {
        const WCHAR *p = old_path;
        DWORD dir_len = lstrlenW(new_dir);
        while (p && *p)
        {
            const WCHAR *semi = wcschr(p, L';');
            size_t seg_len = semi ? (size_t)(semi - p) : wcslen(p);
            if (seg_len == dir_len && !wcsnicmp(p, new_dir, dir_len))
            {
                free(old_path);
                RegCloseKey(hkey);
                return;
            }
            p = semi ? semi + 1 : NULL;
        }

        new_len = lstrlenW(old_path) + 1 + dir_len + 1;
        new_path = malloc(new_len * sizeof(WCHAR));
        if (new_path)
            swprintf(new_path, new_len, L"%ls;%ls", old_path, new_dir);
    }
    else
    {
        new_len = lstrlenW(new_dir) + 1;
        new_path = malloc(new_len * sizeof(WCHAR));
        if (new_path)
            lstrcpyW(new_path, new_dir);
    }

    if (new_path)
    {
        RegSetValueExW(hkey, L"Path", 0, REG_EXPAND_SZ, (const BYTE *)new_path,
                       (lstrlenW(new_path) + 1) * sizeof(WCHAR));
        wprintf(L"Appended to HKCU\\Environment\\Path: %ls\n", new_dir);
        free(new_path);
    }

    free(old_path);
    RegCloseKey(hkey);

    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment",
                        SMTO_ABORTIFHUNG, 5000, &dwResult);
}

static BOOL resolve_dependency_path(const struct appx_package *pkg, const WCHAR *dep_name, WCHAR *out_path, DWORD cch)
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR key[600], name[256];
    DWORD r, i;

    for (r = 0; r < ARRAY_SIZE(roots); r++)
    {
        HKEY hkey;
        if (RegOpenKeyExW(roots[r], L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages",
                          0, KEY_READ, &hkey) == ERROR_SUCCESS)
        {
            DWORD dep_len = lstrlenW(dep_name);
            for (i = 0; ; i++)
            {
                DWORD name_size = ARRAY_SIZE(name);
                if (RegEnumKeyExW(hkey, i, name, &name_size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;
                if (!wcsncmp(name, dep_name, dep_len) && name[dep_len] == L'_')
                {
                    HKEY sub;
                    swprintf(key, ARRAY_SIZE(key),
                             L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\%ls", name);
                    if (RegOpenKeyExW(roots[r], key, 0, KEY_READ, &sub) == ERROR_SUCCESS)
                    {
                        DWORD size = cch * sizeof(WCHAR);
                        DWORD type = 0;
                        if (RegQueryValueExW(sub, L"PackagePath", NULL, &type, (BYTE *)out_path, &size) == ERROR_SUCCESS && out_path[0])
                        {
                            RegCloseKey(sub);
                            RegCloseKey(hkey);
                            return TRUE;
                        }
                        RegCloseKey(sub);
                    }
                }
            }
            RegCloseKey(hkey);
        }
    }

    {
        WCHAR parent_dir[MAX_PATH];
        WCHAR search_pattern[MAX_PATH];
        WIN32_FIND_DATAW fd;
        HANDLE hFind;
        WCHAR *slash;

        lstrcpynW(parent_dir, pkg->package_path, MAX_PATH);
        slash = wcsrchr(parent_dir, L'\\');
        if (!slash) slash = wcsrchr(parent_dir, L'/');
        if (slash)
        {
            *slash = 0;
            swprintf(search_pattern, MAX_PATH, L"%ls\\%ls_*", parent_dir, dep_name);
            hFind = FindFirstFileW(search_pattern, &fd);
            if (hFind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
                    {
                        swprintf(out_path, cch, L"%ls\\%ls", parent_dir, fd.cFileName);
                        FindClose(hFind);
                        return TRUE;
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
    }

    return FALSE;
}

static void process_dependencies(const struct appx_package *pkg)
{
    DWORD i;
    for (i = 0; i < pkg->dependencies_count; i++)
    {
        const WCHAR *dep = pkg->dependencies[i].name;
        BOOL is_framework_dep = (wcsstr(dep, L"VCLibs") != NULL ||
                                 wcsstr(dep, L"Native") != NULL ||
                                 wcsstr(dep, L"Xaml") != NULL);
        WCHAR dep_path[MAX_PATH];

        if (resolve_dependency_path(pkg, dep, dep_path, ARRAY_SIZE(dep_path)))
        {
            WCHAR manifest[MAX_PATH];
            const WCHAR *folder;
            WCHAR reg_key[600];
            HKEY hkey;

            wprintf(L"Resolved dependency %ls -> %ls\n", dep, dep_path);

            swprintf(manifest, MAX_PATH, L"%ls\\AppxManifest.xml", dep_path);
            if (GetFileAttributesW(manifest) != INVALID_FILE_ATTRIBUTES)
            {
                folder = wcsrchr(dep_path, L'\\');
                folder = folder ? folder + 1 : dep_path;
                swprintf(reg_key, ARRAY_SIZE(reg_key),
                         L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\%ls", folder);
                if (RegCreateKeyExW(HKEY_CURRENT_USER, reg_key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
                {
                    set_reg_str(hkey, L"PackagePath", dep_path);
                    set_reg_str(hkey, L"PackageRootPath", dep_path);
                    set_reg_str(hkey, L"PackageFullName", folder);
                    set_reg_dword(hkey, L"Framework", 1);
                    RegCloseKey(hkey);
                }
                if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, reg_key, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
                {
                    set_reg_str(hkey, L"PackagePath", dep_path);
                    set_reg_str(hkey, L"PackageRootPath", dep_path);
                    set_reg_str(hkey, L"PackageFullName", folder);
                    set_reg_dword(hkey, L"Framework", 1);
                    RegCloseKey(hkey);
                }
            }

            if (is_framework_dep)
            {
                WCHAR bin_path[MAX_PATH];
                DWORD attr;
                swprintf(bin_path, MAX_PATH, L"%ls\\bin", dep_path);
                attr = GetFileAttributesW(bin_path);
                if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                    append_to_path(bin_path);
                else
                    append_to_path(dep_path);
            }
        }
        else
        {
            TRACE("Dependency %ls not found locally\n", dep);
        }
    }
}

static BOOL unregister_package(const WCHAR *full_name)
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR key[600];
    DWORD r;

    wprintf(L"Unregistering package: %ls\n", full_name);

    for (r = 0; r < ARRAY_SIZE(roots); r++)
    {
        HKEY root = roots[r];
        HKEY hkey_exes, hkey_act, hkey_rt;

        /* 1. Delete Packages\<full_name> */
        swprintf(key, ARRAY_SIZE(key),
                 L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\%ls", full_name);
        RegDeleteTreeW(root, key);

        /* 2. Enumerate Exes */
        if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Exes",
                          0, KEY_READ | KEY_WRITE, &hkey_exes) == ERROR_SUCCESS)
        {
            WCHAR sub_name[256];
            DWORD idx = 0;
            while (1)
            {
                DWORD size = ARRAY_SIZE(sub_name);
                HKEY sub;
                BOOL match = FALSE;

                if (RegEnumKeyExW(hkey_exes, idx, sub_name, &size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;

                if (RegOpenKeyExW(hkey_exes, sub_name, 0, KEY_READ, &sub) == ERROR_SUCCESS)
                {
                    WCHAR pkg_name[512];
                    DWORD pkg_size = sizeof(pkg_name), type = 0;
                    if (RegQueryValueExW(sub, L"PackageFullName", NULL, &type, (BYTE *)pkg_name, &pkg_size) == ERROR_SUCCESS)
                    {
                        if (!wcsicmp(pkg_name, full_name))
                            match = TRUE;
                    }
                    RegCloseKey(sub);
                }

                if (match)
                    RegDeleteTreeW(hkey_exes, sub_name);
                else
                    idx++;
            }
            RegCloseKey(hkey_exes);
        }

        /* 3. Enumerate Activation */
        if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Activation",
                          0, KEY_READ | KEY_WRITE, &hkey_act) == ERROR_SUCCESS)
        {
            WCHAR sub_name[256];
            DWORD idx = 0;
            while (1)
            {
                DWORD size = ARRAY_SIZE(sub_name);
                HKEY sub;
                BOOL match = FALSE;

                if (RegEnumKeyExW(hkey_act, idx, sub_name, &size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;

                if (RegOpenKeyExW(hkey_act, sub_name, 0, KEY_READ, &sub) == ERROR_SUCCESS)
                {
                    WCHAR pkg_name[512];
                    DWORD pkg_size = sizeof(pkg_name), type = 0;
                    if (RegQueryValueExW(sub, L"PackageFullName", NULL, &type, (BYTE *)pkg_name, &pkg_size) == ERROR_SUCCESS)
                    {
                        if (!wcsicmp(pkg_name, full_name))
                            match = TRUE;
                    }
                    RegCloseKey(sub);
                }

                if (match)
                    RegDeleteTreeW(hkey_act, sub_name);
                else
                    idx++;
            }
            RegCloseKey(hkey_act);
        }

        /* 4. Enumerate WindowsRuntime\ActivatableClassId */
        if (RegOpenKeyExW(root, L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId",
                          0, KEY_READ | KEY_WRITE, &hkey_rt) == ERROR_SUCCESS)
        {
            WCHAR sub_name[256];
            DWORD idx = 0;
            while (1)
            {
                DWORD size = ARRAY_SIZE(sub_name);
                HKEY sub;
                BOOL match = FALSE;

                if (RegEnumKeyExW(hkey_rt, idx, sub_name, &size, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;

                if (RegOpenKeyExW(hkey_rt, sub_name, 0, KEY_READ, &sub) == ERROR_SUCCESS)
                {
                    WCHAR pkg_name[512];
                    DWORD pkg_size = sizeof(pkg_name), type = 0;
                    if (RegQueryValueExW(sub, L"PackageFullName", NULL, &type, (BYTE *)pkg_name, &pkg_size) == ERROR_SUCCESS)
                    {
                        if (!wcsicmp(pkg_name, full_name))
                            match = TRUE;
                    }
                    RegCloseKey(sub);
                }

                if (match)
                    RegDeleteTreeW(hkey_rt, sub_name);
                else
                    idx++;
            }
            RegCloseKey(hkey_rt);
        }
    }

    {
        HMODULE hMod = LoadLibraryW(L"appxdeploymentclient.dll");
        if (hMod)
        {
            HRESULT (WINAPI *pAppxDestagePackage)(const WCHAR *) = (void *)GetProcAddress(hMod, "AppxDestagePackage");
            if (pAppxDestagePackage) pAppxDestagePackage(full_name);
            FreeLibrary(hMod);
        }
    }

    wprintf(L"Package '%ls' successfully unregistered.\n", full_name);
    return TRUE;
}

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
        DWORD attr, len;
        struct appx_package pkg;
        WCHAR *p;

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
                {
                    lstrcpynW(dos_path, input_path, MAX_PATH);
                }
            }
            else
            {
                lstrcpynW(dos_path, input_path, MAX_PATH);
            }
        }
        else
        {
            GetFullPathNameW(input_path, MAX_PATH, dos_path, NULL);
        }

        attr = GetFileAttributesW(dos_path);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            len = lstrlenW(dos_path);
            if (len && (dos_path[len - 1] == L'\\' || dos_path[len - 1] == L'/'))
                dos_path[len - 1] = 0;
            lstrcatW(dos_path, L"\\AppxManifest.xml");
        }

        if (GetFileAttributesW(dos_path) == INVALID_FILE_ATTRIBUTES)
        {
            wprintf(L"Error: Manifest file '%ls' not found.\n", dos_path);
            return 2;
        }

        memset(&pkg, 0, sizeof(pkg));
        lstrcpynW(pkg.package_path, dos_path, MAX_PATH);
        p = wcsrchr(pkg.package_path, L'\\');
        if (!p) p = wcsrchr(pkg.package_path, L'/');
        if (p) *p = 0;

        if (!parse_appx_manifest(dos_path, &pkg))
        {
            wprintf(L"Error: Failed to parse '%ls'.\n", dos_path);
            return 3;
        }

        determine_package_identity(pkg.package_path, &pkg);

        if (!register_package(&pkg))
        {
            wprintf(L"Error: Registration failed.\n");
            return 4;
        }

        process_dependencies(&pkg);

        wprintf(L"Package '%ls' registered successfully.\n", pkg.identity.full_name);

        free(pkg.applications);
        free(pkg.dependencies);
        free(pkg.classes);
        return 0;
    }
    else if (!wcscmp(cmd, L"-u") || !wcscmp(cmd, L"/u") || !wcscmp(cmd, L"--unregister") || !wcscmp(cmd, L"-unregister"))
    {
        if (argc < 3)
        {
            wprintf(L"Error: Missing package full name to unregister.\n");
            print_usage();
            return 1;
        }

        if (!unregister_package(argv[2]))
        {
            wprintf(L"Error: Unregistration failed.\n");
            return 4;
        }
        return 0;
    }

    wprintf(L"Error: Unknown option '%ls'.\n", cmd);
    print_usage();
    return 1;
}
