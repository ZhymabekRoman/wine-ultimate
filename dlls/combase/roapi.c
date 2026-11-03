/*
 * Copyright 2014 Martin Storsjo
 * Copyright 2016 Michael Müller
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
#include "objbase.h"
#include "initguid.h"
#include "roapi.h"
#include "roparameterizediid.h"
#include "roerrorapi.h"
#include "winstring.h"
#include "errhandlingapi.h"

#include "combase_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(combase);

struct activatable_class_data
{
    ULONG size;
    DWORD unk;
    DWORD module_len;
    DWORD module_offset;
    DWORD threading_model;
};

static HRESULT get_library_for_classid(const WCHAR *classid, WCHAR **out)
{
    ACTCTX_SECTION_KEYED_DATA data;
    HKEY hkey_root, hkey_class;
    DWORD type, size;
    HRESULT hr;
    WCHAR *buf = NULL;

    *out = NULL;

    /* search activation context first */
    data.cbSize = sizeof(data);
    if (FindActCtxSectionStringW(FIND_ACTCTX_SECTION_KEY_RETURN_HACTCTX, NULL,
            ACTIVATION_CONTEXT_SECTION_WINRT_ACTIVATABLE_CLASSES, classid, &data))
    {
        struct activatable_class_data *activatable_class = (struct activatable_class_data *)data.lpData;
        void *ptr = (BYTE *)data.lpSectionBase + activatable_class->module_offset;
        *out = wcsdup(ptr);
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.ApplicationModel.Core.", 30) ||
        !wcsncmp(classid, L"Windows.ApplicationModel.Background.", 36) ||
        !wcscmp(classid, L"Windows.ApplicationModel.AppInstance") ||
        !wcscmp(classid, L"Microsoft.Windows.AppLifecycle.AppInstance") ||
        !wcscmp(classid, L"Windows.ApplicationModel.StartupTask") ||
        !wcsncmp(classid, L"Windows.UI.ViewManagement.ApplicationView", 41) ||
        (!wcsncmp(classid, L"Windows.ApplicationModel.DataTransfer.", 38) &&
         wcsncmp(classid, L"Windows.ApplicationModel.DataTransfer.DragDrop.", 47)))
    {
        *out = wcsdup(L"twinapi.appcore.dll");
        return S_OK;
    }

    /* Windows.Networking.PushNotifications.* is the WNS channel surface: an
     * unpackaged prefix has no ActivatableClassId key for the classes, so route
     * the namespace to windows.networking.pushnotifications.dll, the module that
     * implements the classes' DllGetActivationFactory, per the own-name module
     * rule this file already applies to Windows.ApplicationModel.Core,
     * Windows.UI.Notifications and Windows.Security.Cryptography.  The module
     * covers the whole surface the twinapi.appcore stub answered (statics and
     * the ChannelsRevoked event) plus the statics2/statics3 GetForUser/GetDefault
     * points, the channel creation operations and the channel object. */
    if (!wcsncmp(classid, L"Windows.Networking.PushNotifications.", 37))
    {
        *out = wcsdup(L"windows.networking.pushnotifications.dll");
        return S_OK;
    }

    /* Windows.UI.Notifications.* is the toast/badge surface: the app's tray and
     * notification path activates Windows.UI.Notifications.ToastNotification-
     * Manager (Microsoft.Toolkit ToastNotificationManagerCompat.CreateToast-
     * Notifier logs "Failed initializing notifications" when the class does not
     * resolve) and hands the toast payload to ToastContentFactory/ToastNotification.
     * Windows serves the namespace out of twinapi.appcore.dll; an unpackaged
     * prefix has no ActivatableClassId key for the classes, so route the namespace
     * to the module that implements the classes' DllGetActivationFactory, like the
     * Windows.ApplicationModel.Core route above.  The namespace split is exact on
     * the trailing dot: the remaining Windows.UI.Notifications classes (the tile
     * and badge-manager surface twinapi does not implement) keep answering
     * E_NOINTERFACE from that factory. */
    if (!wcsncmp(classid, L"Windows.UI.Notifications.", 25))
    {
        *out = wcsdup(L"twinapi.appcore.dll");
        return S_OK;
    }

    /* Windows.Data.Xml.Dom.XmlDocument is the DOM the notification path builds its
     * toast payload into (IToastNotificationFactory::CreateToastNotification takes
     * the class), and Windows.Data.Xml.Xsl.XsltProcessor is its transform partner.
     * This tree's MSXML DOM provider, msxml6.dll, implements the classes'
     * DllGetActivationFactory over the msxml6 DOM, so route the namespace there for
     * the same unpackaged-prefix reason as the two routes above. */
    if (!wcsncmp(classid, L"Windows.Data.Xml.", 17))
    {
        *out = wcsdup(L"msxml6.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.Security.Cryptography.", 30))
    {
        *out = wcsdup(L"cryptowinrt.dll");
        return S_OK;
    }

    /* PRM/MRT resources layer: .NET Native (mrt100/SharedLibrary System.SR),
     * Microsoft.UI.Xaml and the app's compiled ResourceManager.Current call
     * sites activate Windows.ApplicationModel.Resources.Core.ResourceContext
     * during startup (Calculator: 8x) and FailFast c0000602 when it is absent.
     * Windows.System.MemoryManager is co-hosted as a no-op stub (1x warning). */
    if (!wcsncmp(classid, L"Windows.ApplicationModel.Resources.Core.", 38) ||
        !wcscmp(classid, L"Windows.System.MemoryManager"))
    {
        *out = wcsdup(L"windows.applicationmodel.resources.core.dll");
        return S_OK;
    }

    /* Classes consumed during XAML control construction: they fall through to the
     * ActivatableClassId registry lookup (absent in this unpackaged prefix) and return
     * REGDB_E_CLASSNOTREG, stalling the UI thread before the WebView2 environment is
     * created.  Route them to the Wine modules that already export DllGetActivationFactory. */
    if (!wcsncmp(classid, L"Windows.UI.Xaml.", 16) ||
        !wcscmp(classid, L"Windows.UI.Xaml") ||
        !wcscmp(classid, L"Windows.UI.ColorHelper"))
    {
        *out = wcsdup(L"windows.ui.xaml.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.UI.ViewManagement.", 26))
    {
        *out = wcsdup(L"windows.ui.dll");
        return S_OK;
    }

    /* Windows.UI.Composition.Compositor is the class the XAML/Composition root of an
     * unpackaged WinUI 3 app activates on its UI thread (WhatsApp Desktop startup).  Such
     * an app registers its activatable classes through its framework package, so an
     * unpackaged prefix has no ActivatableClassId key for the class: the registry lookup
     * below returns REGDB_E_CLASSNOTREG, RoGetActivationFactory logs "Failed to find
     * library" and the UI thread parks with the await on the compositor never completing.
     * Windows hosts the class in Windows.UI.Composition.dll (inproc, ThreadingModel Both)
     * and WinAppSDK self-contained apps activate it in-proc, so route the class to the
     * builtin module that implements its activation factory, mirroring the
     * Windows.UI.ViewManagement route above.  Keep the route exact-match: the sibling
     * Windows.UI.Composition classes are not activatable (they come from the Compositor
     * factory) and must keep falling through to the registry lookup. */
    if (!wcscmp(classid, L"Windows.UI.Composition.Compositor"))
    {
        *out = wcsdup(L"windows.ui.composition.dll");
        return S_OK;
    }

    /* Microsoft.UI.Composition.* is the Microsoft.WindowsAppRuntime composition namespace
     * that Microsoft.ui.xaml.dll activates for its composition tree:
     * ActivationFactoryCache::GetInteropCompositorFactoryPartner (0x18023586c) requests
     * Microsoft.UI.Composition.Compositor for IInteropCompositorFactoryPartner {22118adf}
     * and GetCompositionEasingFunctionStatics (0x18024f53c) requests
     * Microsoft.UI.Composition.CompositionEasingFunction; both FailFast the process when the
     * class does not resolve, and DCompTreeHost::CreateWinRTInteropCompositionDevice
     * (0x1802357b8) FailFasts when the factory refuses CreateInteropCompositor.  An
     * unpackaged app ships the provider (dcompi.dll) in its framework package, so an
     * unpackaged prefix has no ActivatableClassId key for these classes, and the
     * side-by-side probe below finds no "Microsoft.UI.Composition.dll" either: the
     * RoGetActivationFactory that opens the composition channel answers
     * REGDB_E_CLASSNOTREG.  Route the two classes XAML activates there to the builtin
     * provider, exact-match like the Windows.UI.Composition.Compositor route above: the
     * remaining Microsoft.UI.Composition classes are created through the Compositor, and a
     * prefix-separation route (wcsncmp over the namespace) is length-sensitive - comparing
     * past the literal's NUL makes it never match - so it is not used. */
    if (!wcscmp(classid, L"Microsoft.UI.Composition.Compositor") ||
        !wcscmp(classid, L"Microsoft.UI.Composition.CompositionEasingFunction"))
    {
        *out = wcsdup(L"dcompi.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.Foundation.", 19))
    {
        *out = wcsdup(L"wintypes.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.Storage.ApplicationData", 29))
    {
        *out = wcsdup(L"windows.storage.applicationdata.dll");
        return S_OK;
    }

    /* Microsoft.Web.WebView2.Core.* is an unpackaged native WinRT server shipped
     * side-by-side by the app.  It is resolved through the ActivatableClassId\DllPath
     * registry mapping (absolute path) below, so that the server loads regardless of
     * the activating process's own application directory. */
    if (!wcsncmp(classid, L"Windows.System.Threading.", 25))
    {
        *out = wcsdup(L"windows.system.threading.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"WhatsAppNative.", 15))
    {
        if (!wcsncmp(classid, L"WhatsAppNative.Voip.", 20))
            *out = wcsdup(L"WhatsAppNative.Voip.dll");
        else
            *out = wcsdup(L"WhatsAppNative.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.ApplicationModel.Calls.", 31))
    {
        *out = wcsdup(L"windows.applicationmodel.calls.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.Web.Http.", 17))
    {
        *out = wcsdup(L"windows.web.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.Networking.Sockets.", 27))
    {
        *out = wcsdup(L"windows.networking.sockets.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.Media.Editing.", 22) ||
        !wcsncmp(classid, L"Windows.Media.Transcoding.", 26) ||
        !wcsncmp(classid, L"Windows.Media.ClosedCaptioning.", 31) ||
        !wcsncmp(classid, L"Windows.Media.Audio.", 20))
    {
        *out = wcsdup(L"windows.media.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.Storage.Streams.", 24))
    {
        *out = wcsdup(L"windows.storage.dll");
        return S_OK;
    }

    if (!wcscmp(classid, L"Windows.Storage.StorageFile") ||
        !wcscmp(classid, L"Windows.Storage.StorageFolder") ||
        !wcscmp(classid, L"Windows.Storage.FileIO") ||
        !wcscmp(classid, L"Windows.Storage.PathIO"))
    {
        *out = wcsdup(L"windows.storage.dll");
        return S_OK;
    }

    /* load class registry key */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\WindowsRuntime\\ActivatableClassId",
                      0, KEY_READ, &hkey_root))
        return REGDB_E_READREGDB;
    if (RegOpenKeyExW(hkey_root, classid, 0, KEY_READ, &hkey_class))
    {
        size_t len, i;
        RegCloseKey(hkey_root);

        /* Unpackaged apps ship their activatable classes side-by-side in a dll named
         * after the class namespace (e.g. Microsoft.Windows.ApplicationModel.Resources.ResourceManager
         * is implemented by Microsoft.Windows.ApplicationModel.Resources.dll).  Probe every
         * dot separated namespace prefix of the class id, longest first, for a loadable dll. */
        len = lstrlenW( classid );
        for (i = len; i > 0; i--)
        {
            if (classid[i - 1] != '.' || i == len) continue;
            {
                WCHAR dll_name[MAX_PATH];
                HMODULE h;

                if (i + 5 >= MAX_PATH) continue;
                memcpy( dll_name, classid, i * sizeof(WCHAR) );
                wcscpy( dll_name + i, L".dll" );
                if ((h = LoadLibraryW( dll_name )))
                {
                    FreeLibrary( h );
                    TRACE( "Found class %s in side-by-side dll %s\n", debugstr_w( classid ), debugstr_w( dll_name ) );
                    *out = wcsdup( dll_name );
                    return S_OK;
                }
            }
        }
        WARN("Class %s not found in registry\n", debugstr_w(classid));
        return REGDB_E_CLASSNOTREG;
    }
    RegCloseKey(hkey_root);

    /* load (and expand) DllPath registry value */
    if (RegQueryValueExW(hkey_class, L"DllPath", NULL, &type, NULL, &size))
    {
        hr = REGDB_E_READREGDB;
        goto done;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ)
    {
        hr = REGDB_E_READREGDB;
        goto done;
    }
    if (!(buf = malloc(size)))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (RegQueryValueExW(hkey_class, L"DllPath", NULL, NULL, (BYTE *)buf, &size))
    {
        hr = REGDB_E_READREGDB;
        goto done;
    }
    if (type == REG_EXPAND_SZ)
    {
        WCHAR *expanded;
        DWORD len = ExpandEnvironmentStringsW(buf, NULL, 0);
        if (!(expanded = malloc(len * sizeof(WCHAR))))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        ExpandEnvironmentStringsW(buf, expanded, len);
        free(buf);
        buf = expanded;
    }

    *out = buf;
    return S_OK;

done:
    free(buf);
    RegCloseKey(hkey_class);
    return hr;
}


/***********************************************************************
 *      RoInitialize (combase.@)
 */
HRESULT WINAPI RoInitialize(RO_INIT_TYPE type)
{
    switch (type) {
    case RO_INIT_SINGLETHREADED:
        return CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    default:
        FIXME("type %d\n", type);
    case RO_INIT_MULTITHREADED:
        return CoInitializeEx(NULL, COINIT_MULTITHREADED);
    }
}

/***********************************************************************
 *      RoUninitialize (combase.@)
 */
void WINAPI RoUninitialize(void)
{
    CoUninitialize();
}

/***********************************************************************
 *      RoGetActivationFactory (combase.@)
 */
HRESULT WINAPI DECLSPEC_HOTPATCH RoGetActivationFactory(HSTRING classid, REFIID iid, void **class_factory)
{
    PFNGETACTIVATIONFACTORY pDllGetActivationFactory;
    IActivationFactory *factory;
    WCHAR *library;
    HMODULE module;
    HRESULT hr;

    FIXME("(%s, %s, %p): semi-stub\n", debugstr_hstring(classid), debugstr_guid(iid), class_factory);

    if (!iid || !class_factory)
        return E_INVALIDARG;

    if (!wcscmp(WindowsGetStringRawBuffer(classid, NULL), L"Windows.ApplicationModel.Resources.ResourceLoader"))
    {
        HMODULE hMod = LoadLibraryW(L"twinapi.appcore.dll");
        if (hMod)
        {
            PFNGETACTIVATIONFACTORY pfn = (void *)GetProcAddress(hMod, "DllGetActivationFactory");
            if (pfn && SUCCEEDED(pfn(classid, &factory)))
            {
                hr = IActivationFactory_QueryInterface(factory, iid, class_factory);
                IActivationFactory_Release(factory);
                return hr;
            }
        }
    }

    if (FAILED(hr = ensure_mta()))
        return hr;

    if (!wcscmp(WindowsGetStringRawBuffer(classid, NULL), L"Windows.System.UserProfile.DiagnosticsSettings") ||
        !wcscmp(WindowsGetStringRawBuffer(classid, NULL), L"Windows.System.Profile.PlatformDiagnosticsAndUsageDataSettings"))
    {
        TRACE("No activation factory for %s\n", debugstr_hstring(classid));
        return REGDB_E_CLASSNOTREG;
    }

    hr = get_library_for_classid(WindowsGetStringRawBuffer(classid, NULL), &library);
    if (FAILED(hr))
    {
        ERR("Failed to find library for %s\n", debugstr_hstring(classid));
        return hr;
    }

    if (!(module = LoadLibraryW(library)))
    {
        ERR("Failed to load module %s\n", debugstr_w(library));
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto done;
    }

    if (!(pDllGetActivationFactory = (void *)GetProcAddress(module, "DllGetActivationFactory")))
    {
        ERR("Module %s does not implement DllGetActivationFactory\n", debugstr_w(library));
        hr = E_FAIL;
        goto done;
    }

    TRACE("Found library %s for class %s\n", debugstr_w(library), debugstr_hstring(classid));

    hr = pDllGetActivationFactory(classid, &factory);
    if (SUCCEEDED(hr))
    {
        hr = IActivationFactory_QueryInterface(factory, iid, class_factory);
        if (SUCCEEDED(hr))
        {
            TRACE("Created interface %p\n", *class_factory);
            module = NULL;
        }
        IActivationFactory_Release(factory);
    }
    else
    {
        ERR("Class %s not found in %s, hr %#lx.\n", wine_dbgstr_hstring(classid), debugstr_w(library), hr);
    }

done:
    free(library);
    if (module) FreeLibrary(module);
    return hr;
}

struct str_buf
{
    char *data;
    size_t len, size;
};

static BOOL str_buf_add( struct str_buf *buf, const char *s, size_t len )
{
    if (buf->len + len > buf->size)
    {
        size_t size = max( buf->size * 2, buf->len + len );
        char *data = realloc( buf->data, size );
        if (!data) return FALSE;
        buf->data = data;
        buf->size = size;
    }
    memcpy( &buf->data[buf->len], s, len );
    buf->len += len;
    return TRUE;
}

static BOOL str_buf_add_utf8( struct str_buf *buf, const WCHAR *str, size_t len )
{
    size_t i;

    for (i = 0; i < len; i++)
    {
        WCHAR c = str[i];
        char u[4];
        size_t n = 0;

        if (c >= 0xd800 && c <= 0xdbff && i + 1 < len && str[i + 1] >= 0xdc00 && str[i + 1] <= 0xdfff)
        {
            UINT32 cp = 0x10000 + ((UINT32)c - 0xd800) * 0x400 + ((UINT32)str[++i] - 0xdc00);
            u[n++] = 0xf0 | (cp >> 18);
            u[n++] = 0x80 | ((cp >> 12) & 0x3f);
            u[n++] = 0x80 | ((cp >> 6) & 0x3f);
            u[n++] = 0x80 | (cp & 0x3f);
        }
        else if (c < 0x80) u[n++] = c;
        else if (c < 0x800)
        {
            u[n++] = 0xc0 | (c >> 6);
            u[n++] = 0x80 | (c & 0x3f);
        }
        else
        {
            u[n++] = 0xe0 | (c >> 12);
            u[n++] = 0x80 | ((c >> 6) & 0x3f);
            u[n++] = 0x80 | (c & 0x3f);
        }
        if (!str_buf_add( buf, u, n )) return FALSE;
    }
    return TRUE;
}

/*
 * SHA-1 (FIPS 180-4), single shot.  The parameterized-IID algorithm below is a UUIDv5-style
 * SHA-1 hash; combase does not import bcrypt, and the hashed input is a short signature string,
 * so this stays a private helper.
 */
static void sha1( const BYTE *msg, size_t len, BYTE out[20] )
{
    DWORD h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    size_t padded = ((len + 8) / 64 + 1) * 64, block, i;
    ULONGLONG bits = (ULONGLONG)len * 8;
    BYTE *buf = calloc( 1, padded );

    if (!buf) return;
    memcpy( buf, msg, len );
    buf[len] = 0x80;
    for (i = 0; i < 8; i++) buf[padded - 1 - i] = (BYTE)(bits >> (8 * i));

    for (block = 0; block < padded; block += 64)
    {
        DWORD w[80], a, b, c, d, e;

        for (i = 0; i < 16; i++)
            w[i] = (buf[block + i * 4] << 24) | (buf[block + i * 4 + 1] << 16)
                   | (buf[block + i * 4 + 2] << 8) | buf[block + i * 4 + 3];
        for (i = 16; i < 80; i++)
        {
            DWORD t = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (t << 1) | (t >> 31);
        }
        a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
        for (i = 0; i < 80; i++)
        {
            DWORD k, f;

            if (i < 20)      { k = 0x5a827999; f = (b & c) | (~b & d); }
            else if (i < 40) { k = 0x6ed9eba1; f = b ^ c ^ d; }
            else if (i < 60) { k = 0x8f1bbcdc; f = (b & c) | (b & d) | (c & d); }
            else             { k = 0xca62c1d6; f = b ^ c ^ d; }
            {
                DWORD t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
                e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
            }
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (i = 0; i < 5; i++)
    {
        out[i * 4] = h[i] >> 24;
        out[i * 4 + 1] = h[i] >> 16;
        out[i * 4 + 2] = h[i] >> 8;
        out[i * 4 + 3] = h[i];
    }
    free( buf );
}

/*
 * Parameterized interface IDs (PIIDs) and the metadata-derived signatures for the well-known
 * Windows.Foundation / Windows.Foundation.Collections surface.  RoGetParameterizedTypeInstanceIID
 * derives the instance IID as a UUIDv5-style SHA-1 over the UTF-8 type signature string
 * "pinterface({piid};arg1;...)" (see "The Windows Runtime type system", MDN signature grammar),
 * hashed with the wrt_pinterface namespace GUID 11f47ad5-7b73-42c0-abae-878b1e16adee fed as
 * RFC-4122-order octets; the leading 16 hash octets become the GUID with Data1/Data2 read
 * big-endian, Data3 stamped with version 5 and Data4[0] with the RFC-4122 variant (validated
 * against the published instantiation IIDs: IMapView<HSTRING,HSTRING> = 1e036276-2f60-55f6-
 * b7f3-f86079e6900b, IKeyValuePair<HSTRING,HSTRING> = 60310303-49c5-52e6-abc6-a9b36eccc716,
 * EventHandler<object> = 849456a8-1179-530e-910d-ddadfb6f476d, IReference<HSTRING> = fd416dfb-
 * 2a07-52eb-aae3-dfce14116c05).  The PIIDs below are the values from the Windows metadata,
 * matching the projection table embedded in Calculator's SharedLibrary.dll (UTF-16 strings
 * 6a79e863 IIterator, faa585ea IIterable, bbe1fa4c IVectorView, 913337e9 IVector, 3c2925fe IMap,
 * 65df2bf5 IMapView, 02b51929 IKeyValuePair, e480ce40 IObservableMap, 61c17706 IReference, ...).
 */
struct named_guid
{
    const WCHAR *name;
    const GUID guid;
};

static const struct named_guid pinterface_names[] =
{
    { L"Windows.Foundation.Collections.IIterable`1",                {0xfaa585ea, 0x6214, 0x4217, {0xaf, 0xda, 0x7f, 0x46, 0xde, 0x58, 0x69, 0xb3}}},
    { L"Windows.Foundation.Collections.IIterator`1",                {0x6a79e863, 0x4300, 0x459a, {0x99, 0x66, 0xcb, 0xb6, 0x60, 0x96, 0x3e, 0xe1}}},
    { L"Windows.Foundation.Collections.IKeyValuePair`2",            {0x02b51929, 0xc1c4, 0x4a7e, {0x89, 0x40, 0x03, 0x12, 0xb5, 0xc1, 0x85, 0x00}}},
    { L"Windows.Foundation.Collections.IMap`2",                     {0x3c2925fe, 0x8519, 0x45c1, {0xaa, 0x79, 0x19, 0x7b, 0x67, 0x18, 0xc1, 0xc1}}},
    { L"Windows.Foundation.Collections.IMapView`2",                 {0x65df2bf5, 0xbf39, 0x41b5, {0xae, 0xbc, 0x5a, 0x9d, 0x86, 0x5e, 0x47, 0x2b}}},
    { L"Windows.Foundation.Collections.IVector`1",                  {0x913337e9, 0x11a1, 0x4345, {0xa3, 0xa2, 0x4e, 0x7f, 0x95, 0x6e, 0x22, 0x2d}}},
    { L"Windows.Foundation.Collections.IVectorView`1",              {0xbbe1fa4c, 0xb0e3, 0x4583, {0xba, 0xef, 0x1f, 0x1b, 0x2e, 0x48, 0x3e, 0x56}}},
    { L"Windows.Foundation.Collections.IObservableMap`2",           {0xe480ce40, 0xa338, 0x4ada, {0xad, 0xcf, 0x27, 0x22, 0x72, 0xe4, 0x8c, 0xb9}}},
    { L"Windows.Foundation.Collections.IObservableVector`1",        {0x5917eb53, 0x50b4, 0x4a0d, {0xb3, 0x09, 0x65, 0x86, 0x2b, 0x3f, 0x1d, 0xbc}}},
    { L"Windows.Foundation.Collections.IMapChangedEventArgs`1",     {0x9939f4df, 0x050a, 0x4c0f, {0xaa, 0x60, 0x77, 0x07, 0x5f, 0x9c, 0x47, 0x77}}},
    { L"Windows.Foundation.Collections.MapChangedEventHandler`2",   {0x179517f3, 0x94ee, 0x41f8, {0xbd, 0xdc, 0x76, 0x8a, 0x89, 0x55, 0x44, 0xf3}}},
    { L"Windows.Foundation.Collections.VectorChangedEventHandler`1",{0x0c051752, 0x9fbf, 0x4c70, {0xaa, 0x0c, 0x0e, 0x4c, 0x82, 0xd9, 0xa7, 0x61}}},
    { L"Windows.Foundation.IReference`1",                           {0x61c17706, 0x2d65, 0x11e0, {0x9a, 0xe8, 0xd4, 0x85, 0x64, 0x01, 0x54, 0x72}}},
    { L"Windows.Foundation.IReferenceArray`1",                      {0x61c17707, 0x2d65, 0x11e0, {0x9a, 0xe8, 0xd4, 0x85, 0x64, 0x01, 0x54, 0x72}}},
    { L"Windows.Foundation.EventHandler`1",                         {0x61c1770d, 0x2d65, 0x11e0, {0x9a, 0xe8, 0xd4, 0x85, 0x64, 0x01, 0x54, 0x72}}},
    { L"Windows.Foundation.TypedEventHandler`2",                    {0x61c1770e, 0x2d65, 0x11e0, {0x9a, 0xe8, 0xd4, 0x85, 0x64, 0x01, 0x54, 0x72}}},
    { L"Windows.Foundation.IAsyncActionWithProgress`1",             {0x1f6db258, 0xe803, 0x48a1, {0x95, 0x46, 0xeb, 0x73, 0x53, 0x39, 0x88, 0x84}}},
    { L"Windows.Foundation.IAsyncOperation`1",                      {0x9fc2b0bb, 0xe446, 0x44e2, {0xaa, 0x61, 0x9c, 0xab, 0x8f, 0x63, 0x6a, 0xf2}}},
    { L"Windows.Foundation.IAsyncOperationWithProgress`2",          {0xb5d036d7, 0xe297, 0x498f, {0xba, 0x60, 0x02, 0x89, 0xe7, 0x6e, 0x23, 0xdd}}},
    { L"Windows.Foundation.AsyncOperationProgressHandler`2",        {0x55690902, 0x0aab, 0x421a, {0x87, 0x78, 0xf8, 0xce, 0x50, 0x26, 0xd7, 0x58}}},
    { L"Windows.Foundation.AsyncOperationCompletedHandler`1",       {0xfcdcf02c, 0xe5d8, 0x4478, {0x91, 0x5a, 0x4d, 0x90, 0xb7, 0x4b, 0x83, 0xa5}}},
    { L"Windows.Foundation.AsyncOperationWithProgressCompletedHandler`2",
                                                                     {0xe85df41d, 0x6aa7, 0x46e3, {0xa8, 0xe2, 0xf0, 0x09, 0xd8, 0x40, 0xc6, 0x27}}},
    { L"Windows.Foundation.AsyncActionProgressHandler`1",           {0x6d844858, 0x0cff, 0x4590, {0xae, 0x89, 0x95, 0xa5, 0xa5, 0xc8, 0xb4, 0xb8}}},
    { L"Windows.Foundation.AsyncActionWithProgressCompletedHandler`1",
                                                                     {0x9c029f91, 0xcc84, 0x44fd, {0xac, 0x26, 0x0a, 0x6c, 0x4e, 0x55, 0x52, 0x81}}},
};

static const struct named_guid iid_names[] =
{
    { L"Windows.Foundation.IInspectable",           {0xaf86e2e0, 0xbd9d, 0x45a5, {0x83, 0x78, 0x42, 0xd2, 0x07, 0x0e, 0xe4, 0x18}}},
    { L"Windows.Foundation.IStringable",            {0x96369f54, 0x8eb6, 0x48f0, {0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3}}},
    { L"Windows.Foundation.IActivationFactory",     {0x06499997, 0xf7b4, 0x45fe, {0xb7, 0x63, 0x75, 0x77, 0xd1, 0xd3, 0xcb, 0x4a}}},
    { L"Windows.Foundation.IReferenceTracker",      {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}}},
    { L"Windows.Foundation.IAgileObject",           {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}}},
};

/*
 * Base type / struct / enum / class signatures from the metadata (MS-WDMDB grammar).  The
 * struct and enum field lists below match the literals CoreRT carries in Calculator's
 * SharedLibrary.dll (e.g. struct(Windows.UI.Xaml.Interop.TypeName;string;enum(Windows.UI.Xaml.Interop.TypeKind;i4))
 * and enum(Windows.Foundation.Metadata.AttributeTargets;u4)), which pins the field order.
 * The rc() default-interface IIDs are the fork-verified WinRT interface IIDs.
 */
struct named_sig
{
    const WCHAR *name;
    const char *sig;
};

static const struct named_sig base_sigs[] =
{
    { L"Windows.Foundation.Boolean", "b1" },
    { L"Windows.Foundation.Char", "c2" },
    { L"Windows.Foundation.Int16", "i2" },
    { L"Windows.Foundation.UInt16", "u2" },
    { L"Windows.Foundation.Int32", "i4" },
    { L"Windows.Foundation.UInt32", "u4" },
    { L"Windows.Foundation.Int64", "i8" },
    { L"Windows.Foundation.UInt64", "u8" },
    { L"Windows.Foundation.Single", "f4" },
    { L"Windows.Foundation.Double", "f8" },
    { L"Windows.Foundation.String", "string" },
    { L"Windows.Foundation.Guid", "g16" },
    { L"Windows.Foundation.Object", "cinterface(IInspectable)" },
    { L"Windows.Foundation.IInspectable", "cinterface(IInspectable)" },
};

static const struct named_sig struct_sigs[] =
{
    { L"Windows.Foundation.Point", "struct(Windows.Foundation.Point;f4;f4)" },
    { L"Windows.Foundation.Size", "struct(Windows.Foundation.Size;f4;f4)" },
    { L"Windows.Foundation.Rect", "struct(Windows.Foundation.Rect;f4;f4;f4;f4)" },
    { L"Windows.Foundation.HResult", "struct(Windows.Foundation.HResult;i4)" },
    { L"Windows.Foundation.DateTime", "struct(Windows.Foundation.DateTime;i8)" },
    { L"Windows.Foundation.TimeSpan", "struct(Windows.Foundation.TimeSpan;i8)" },
    { L"Windows.Foundation.DateTimeOffset", "struct(Windows.Foundation.DateTimeOffset;i8)" },
    { L"Windows.Foundation.EventRegistrationToken", "struct(Windows.Foundation.EventRegistrationToken;i8)" },
    { L"Windows.Foundation.Numerics.Vector2", "struct(Windows.Foundation.Numerics.Vector2;f4;f4)" },
    { L"Windows.Foundation.Numerics.Vector3", "struct(Windows.Foundation.Numerics.Vector3;f4;f4;f4)" },
    { L"Windows.Foundation.Numerics.Vector4", "struct(Windows.Foundation.Numerics.Vector4;f4;f4;f4;f4)" },
    { L"Windows.Foundation.Numerics.Plane", "struct(Windows.Foundation.Numerics.Plane;f4;f4;f4;f4)" },
    { L"Windows.Foundation.Numerics.Quaternion", "struct(Windows.Foundation.Numerics.Quaternion;f4;f4;f4;f4)" },
    { L"Windows.Foundation.Numerics.Matrix3x2", "struct(Windows.Foundation.Numerics.Matrix3x2;f4;f4;f4;f4;f4;f4)" },
    { L"Windows.Foundation.Numerics.Matrix4x4",
      "struct(Windows.Foundation.Numerics.Matrix4x4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4)" },
    { L"Windows.UI.Color", "struct(Windows.UI.Color;u8;u8;u8;u8)" },
    { L"Windows.UI.Xaml.CornerRadius", "struct(Windows.UI.Xaml.CornerRadius;f4;f4)" },
    { L"Windows.UI.Xaml.Duration", "struct(Windows.UI.Xaml.Duration;i8;enum(Windows.UI.Xaml.DurationType;i4))" },
    { L"Windows.UI.Xaml.GridLength", "struct(Windows.UI.Xaml.GridLength;f4;enum(Windows.UI.Xaml.GridUnitType;i4))" },
    { L"Windows.UI.Xaml.Controls.Primitives.GeneratorPosition",
      "struct(Windows.UI.Xaml.Controls.Primitives.GeneratorPosition;i4;i4)" },
    { L"Windows.UI.Xaml.Media.Matrix", "struct(Windows.UI.Xaml.Media.Matrix;f4;f4;f4;f4;f4;f4)" },
    { L"Windows.UI.Xaml.Media.Media3D.Matrix3D",
      "struct(Windows.UI.Xaml.Media.Media3D.Matrix3D;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4;f4)" },
    { L"Windows.UI.Xaml.Media.Animation.KeyTime", "struct(Windows.UI.Xaml.Media.Animation.KeyTime;i8)" },
    { L"Windows.UI.Xaml.Media.Animation.RepeatBehavior",
      "struct(Windows.UI.Xaml.Media.Animation.RepeatBehavior;f4;f8;f8;enum(Windows.UI.Xaml.Media.Animation.RepeatBehaviorType;i4))" },
    { L"Windows.UI.Xaml.Interop.TypeName",
      "struct(Windows.UI.Xaml.Interop.TypeName;string;enum(Windows.UI.Xaml.Interop.TypeKind;i4))" },
};

static const struct named_sig enum_sigs[] =
{
    { L"Windows.UI.Xaml.DurationType", "enum(Windows.UI.Xaml.DurationType;i4)" },
    { L"Windows.UI.Xaml.GridUnitType", "enum(Windows.UI.Xaml.GridUnitType;i4)" },
    { L"Windows.UI.Xaml.Controls.Primitives.GeneratorDirection", "enum(Windows.UI.Xaml.Controls.Primitives.GeneratorDirection;i4)" },
    { L"Windows.UI.Xaml.Media.Animation.RepeatBehaviorType", "enum(Windows.UI.Xaml.Media.Animation.RepeatBehaviorType;i4)" },
    { L"Windows.UI.Xaml.Interop.TypeKind", "enum(Windows.UI.Xaml.Interop.TypeKind;i4)" },
    { L"Windows.Foundation.Metadata.AttributeTargets", "enum(Windows.Foundation.Metadata.AttributeTargets;u4)" },
    { L"Windows.Foundation.PropertyType", "enum(Windows.Foundation.PropertyType;i4)" },
    { L"Windows.Foundation.Collections.CollectionChange", "enum(Windows.Foundation.Collections.CollectionChange;i4)" },
};

static const struct named_sig rc_sigs[] =
{
    { L"Windows.Foundation.Uri", "rc(Windows.Foundation.Uri;{9e365e57-48b2-4160-956f-c7385120bbfc})" },
    { L"Windows.ApplicationModel.Resources.Core.ResourceMap",
      "rc(Windows.ApplicationModel.Resources.Core.ResourceMap;{72284824-db8c-42f8-b08c-53ff357dad82})" },
    { L"Windows.ApplicationModel.Resources.Core.ResourceCandidate",
      "rc(Windows.ApplicationModel.Resources.Core.ResourceCandidate;{af5207d9-c433-4764-b3fd-8fa6bfbcbadc})" },
    { L"Windows.ApplicationModel.Resources.Core.ResourceQualifier",
      "rc(Windows.ApplicationModel.Resources.Core.ResourceQualifier;{785da5b2-4afd-4376-a888-c5f9a6b7a05c})" },
    { L"Windows.ApplicationModel.Resources.Core.NamedResource",
      "rc(Windows.ApplicationModel.Resources.Core.NamedResource;{1c98c219-0b13-4240-89a5-d495dc189a00})" },
    { L"Windows.ApplicationModel.Resources.Core.ResourceManager",
      "rc(Windows.ApplicationModel.Resources.Core.ResourceManager;{f744d97b-9988-44fb-abd6-5378844cfa8b})" },
    { L"Windows.ApplicationModel.Resources.Core.ResourceContext",
      "rc(Windows.ApplicationModel.Resources.Core.ResourceContext;{2fa22f4b-707e-4b27-ad0d-d0d8cd468fd2})" },
};

static const char *const base_keywords[] = { "b1", "i1", "u1", "c2", "i2", "u2", "i4", "u4", "i8", "u8",
                                             "f4", "f8", "g16", "string", "cinterface(IInspectable)" };

static void fmt_guid_ascii( const GUID *guid, char *out )
{
    static const char hex[] = "0123456789abcdef";
    BYTE b[16];
    int i;

    b[0] = guid->Data1 >> 24; b[1] = guid->Data1 >> 16; b[2] = guid->Data1 >> 8; b[3] = guid->Data1;
    b[4] = guid->Data2 >> 8; b[5] = guid->Data2;
    b[6] = guid->Data3 >> 8; b[7] = guid->Data3;
    memcpy( &b[8], guid->Data4, 8 );
    for (i = 0; i < 16; i++)
    {
        *out++ = hex[b[i] >> 4];
        *out++ = hex[b[i] & 0x0f];
        if (i == 3 || i == 5 || i == 7 || i == 9) *out++ = '-';
    }
    *out = 0;
}

/*
 * Build one type signature element out of the flat name-element array that RoParseTypeName()
 * produces (plain PCWSTR, not HSTRING).  An element with a trailing "`N" is a parameterized
 * type carrying N following argument elements; the pinterface/pdelegate instance signature is
 * "pinterface({piid};arg1;...)" for both, per the WinRT type-system grammar.
 */
static HRESULT build_element_sig( struct str_buf *buf, const WCHAR **elements, UINT32 count, UINT32 *index )
{
    const WCHAR *name = elements[*index];
    UINT32 arity = 0, i;
    const WCHAR *tick = wcschr( name, L'`' );
    char ascii[48];

    (*index)++;

    if (tick)
    {
        const WCHAR *p = tick + 1;
        while (*p >= L'0' && *p <= L'9') arity = arity * 10 + (*p++ - L'0');
    }

    if (arity)
    {
        for (i = 0; i < ARRAY_SIZE( pinterface_names ); i++)
        {
            if (!lstrcmpW( name, pinterface_names[i].name )) break;
        }
        if (i == ARRAY_SIZE( pinterface_names ))
        {
            TRACE( "no PIID for %s\n", debugstr_w( name ) );
            return RO_E_METADATA_NAME_NOT_FOUND;
        }
        fmt_guid_ascii( &pinterface_names[i].guid, ascii );
        if (!str_buf_add( buf, "pinterface({", 12 )) return E_OUTOFMEMORY;
        if (!str_buf_add( buf, ascii, lstrlenA( ascii ) )) return E_OUTOFMEMORY;
        if (!str_buf_add( buf, "};", 2 )) return E_OUTOFMEMORY;
        for (i = 0; i < arity; i++)
        {
            HRESULT hr;

            if (i && !str_buf_add( buf, ";", 1 )) return E_OUTOFMEMORY;
            if (*index >= count) return RO_E_METADATA_INVALID_TYPE_FORMAT;
            if (FAILED( (hr = build_element_sig( buf, elements, count, index )) )) return hr;
        }
        if (!str_buf_add( buf, ")", 1 )) return E_OUTOFMEMORY;
        return S_OK;
    }

    for (i = 0; i < ARRAY_SIZE( base_sigs ); i++)
        if (!lstrcmpW( name, base_sigs[i].name ))
            return str_buf_add( buf, base_sigs[i].sig, lstrlenA( base_sigs[i].sig ) ) ? S_OK : E_OUTOFMEMORY;
    for (i = 0; i < ARRAY_SIZE( struct_sigs ); i++)
        if (!lstrcmpW( name, struct_sigs[i].name ))
            return str_buf_add( buf, struct_sigs[i].sig, lstrlenA( struct_sigs[i].sig ) ) ? S_OK : E_OUTOFMEMORY;
    for (i = 0; i < ARRAY_SIZE( enum_sigs ); i++)
        if (!lstrcmpW( name, enum_sigs[i].name ))
            return str_buf_add( buf, enum_sigs[i].sig, lstrlenA( enum_sigs[i].sig ) ) ? S_OK : E_OUTOFMEMORY;
    for (i = 0; i < ARRAY_SIZE( rc_sigs ); i++)
        if (!lstrcmpW( name, rc_sigs[i].name ))
            return str_buf_add( buf, rc_sigs[i].sig, lstrlenA( rc_sigs[i].sig ) ) ? S_OK : E_OUTOFMEMORY;
    for (i = 0; i < ARRAY_SIZE( base_keywords ); i++)
    {
        int j;
        for (j = 0; base_keywords[i][j]; j++)
            if (name[j] != (WCHAR)(unsigned char)base_keywords[i][j]) break;
        if (!base_keywords[i][j] && !name[j])
            return str_buf_add( buf, base_keywords[i], lstrlenA( base_keywords[i] ) ) ? S_OK : E_OUTOFMEMORY;
    }
    for (i = 0; i < ARRAY_SIZE( iid_names ); i++)
    {
        if (!lstrcmpW( name, iid_names[i].name ))
        {
            fmt_guid_ascii( &iid_names[i].guid, ascii );
            if (!str_buf_add( buf, "{", 1 )) return E_OUTOFMEMORY;
            if (!str_buf_add( buf, ascii, lstrlenA( ascii ) )) return E_OUTOFMEMORY;
            if (!str_buf_add( buf, "}", 1 )) return E_OUTOFMEMORY;
            return S_OK;
        }
    }

    TRACE( "unresolvable element %s\n", debugstr_w( name ) );
    return RO_E_METADATA_NAME_NOT_FOUND;
}

/***********************************************************************
 *      RoGetParameterizedTypeInstanceIID (combase.@)
 *
 * Computes the IID of a parameterized ("pinterface") type instance per the documented
 * Windows algorithm: build the MS-WDMDB type signature string from the flat name-element
 * array ("pinterface({piid};arg;...)"), take SHA-1 over the wrt_pinterface namespace octets
 * (11f47ad5-7b73-42c0-abae-878b1e16adee, mixed-endian) plus the UTF-8 signature, use the
 * leading 16 bytes as the GUID and stamp UUID version 5 (Data3 high nibble) and the RFC 4122
 * variant (Data4[0] high bits).  Unresolvable names return RO_E_METADATA_NAME_NOT_FOUND
 * instead of a fabricated IID.
 *
 * Not implemented: the IRoMetaDataLocator metadata-locator lookup (the parameter is ignored,
 * as in Wine's other Ro* entry points) and the ROPARAMIIDHANDLE cache handle, which is
 * reported as invalid.
 */
HRESULT WINAPI RoGetParameterizedTypeInstanceIID(UINT32 name_element_count, const WCHAR **name_elements,
                                                 const IRoMetaDataLocator *meta_data_locator, GUID *iid,
                                                 ROPARAMIIDHANDLE *hiid)
{
    static const BYTE namespace_octets[16] =
        {0x11, 0xf4, 0x7a, 0xd5, 0x7b, 0x73, 0x42, 0xc0, 0xab, 0xae, 0x87, 0x8b, 0x1e, 0x16, 0xad, 0xee};
    struct str_buf buf = { 0 };
    UINT32 index = 0;
    BYTE hash[20];
    GUID guid;
    HRESULT hr;

    TRACE( "%u %p %p %p %p\n", name_element_count, name_elements, meta_data_locator, iid, hiid );

    if (hiid) *hiid = INVALID_HANDLE_VALUE;
    if (!iid || !name_elements || !name_element_count) return E_INVALIDARG;
    *iid = GUID_NULL;

    if (!(buf.data = malloc( buf.size = 128 ))) return E_OUTOFMEMORY;

    /* A single non-generic element resolves to the element's own metadata IID. */
    if (!wcschr( name_elements[0], L'`' ))
    {
        UINT32 i;

        if (name_element_count != 1)
        {
            free( buf.data );
            return RO_E_METADATA_INVALID_TYPE_FORMAT;
        }
        for (i = 0; i < ARRAY_SIZE( iid_names ); i++)
        {
            if (!lstrcmpW( name_elements[0], iid_names[i].name ))
            {
                TRACE( "%s -> %s\n", debugstr_w( name_elements[0] ), debugstr_guid( &iid_names[i].guid ) );
                *iid = iid_names[i].guid;
                free( buf.data );
                return S_OK;
            }
        }
        TRACE( "unresolvable name %s\n", debugstr_w( name_elements[0] ) );
        free( buf.data );
        return RO_E_METADATA_NAME_NOT_FOUND;
    }

    hr = build_element_sig( &buf, name_elements, name_element_count, &index );
    if (SUCCEEDED( hr ) && index != name_element_count) hr = E_INVALIDARG;
    if (FAILED( hr ))
    {
        free( buf.data );
        return hr;
    }

    {
        /* SHA-1(namespace + signature): feed the signature after the namespace octets. */
        BYTE *input = HeapAlloc( GetProcessHeap(), 0, sizeof(namespace_octets) + buf.len );
        if (!input) { free( buf.data ); return E_OUTOFMEMORY; }
        memcpy( input, namespace_octets, sizeof(namespace_octets) );
        memcpy( input + sizeof(namespace_octets), buf.data, buf.len );
        sha1( input, sizeof(namespace_octets) + buf.len, hash );
        HeapFree( GetProcessHeap(), 0, input );
    }

    guid.Data1 = ((DWORD)hash[0] << 24) | ((DWORD)hash[1] << 16) | ((DWORD)hash[2] << 8) | hash[3];
    guid.Data2 = (WORD)(((DWORD)hash[4] << 8) | hash[5]);
    guid.Data3 = (WORD)((((hash[6] & 0x0f) | 0x50) << 8) | hash[7]);
    guid.Data4[0] = (BYTE)((hash[8] & 0x3f) | 0x80);
    memcpy( &guid.Data4[1], &hash[9], 7 );

    TRACE( "%s -> %s\n", buf.data, debugstr_guid( &guid ) );
    free( buf.data );
    *iid = guid;
    return S_OK;
}

/***********************************************************************
 *      RoActivateInstance (combase.@)
 */
HRESULT WINAPI RoActivateInstance(HSTRING classid, IInspectable **instance)
{
    IActivationFactory *factory;
    HRESULT hr;

    FIXME("(%s, %p): semi-stub\n", wine_dbgstr_hstring(classid), instance);

    hr = RoGetActivationFactory(classid, &IID_IActivationFactory, (void **)&factory);
    if (SUCCEEDED(hr))
    {
        hr = IActivationFactory_ActivateInstance(factory, instance);
        TRACE("RoActivateInstance (%s) returned hr %#lx, instance %p\n",
              wine_dbgstr_hstring(classid), hr, instance ? *instance : NULL);
        IActivationFactory_Release(factory);
    }
    else
    {
        WARN("RoActivateInstance (%s) RoGetActivationFactory failed hr %#lx\n",
             wine_dbgstr_hstring(classid), hr);
    }

    return hr;
}

struct agile_reference
{
    IAgileReference IAgileReference_iface;
    enum AgileReferenceOptions option;
    IStream *marshal_stream;
    CRITICAL_SECTION cs;
    IUnknown *obj;
    BOOLEAN is_agile;
    LONG ref;
};

static HRESULT marshal_object_in_agile_reference(struct agile_reference *ref, REFIID riid, IUnknown *obj)
{
    HRESULT hr;

    hr = CreateStreamOnHGlobal(0, TRUE, &ref->marshal_stream);
    if (FAILED(hr))
        return hr;

    hr = CoMarshalInterface(ref->marshal_stream, riid, obj, MSHCTX_INPROC, NULL, MSHLFLAGS_TABLESTRONG);
    if (FAILED(hr))
    {
        IStream_Release(ref->marshal_stream);
        ref->marshal_stream = NULL;
    }
    return hr;
}

static inline struct agile_reference *impl_from_IAgileReference(IAgileReference *iface)
{
    return CONTAINING_RECORD(iface, struct agile_reference, IAgileReference_iface);
}

static HRESULT WINAPI agile_ref_QueryInterface(IAgileReference *iface, REFIID riid, void **obj)
{
    TRACE("(%p, %s, %p)\n", iface, debugstr_guid(riid), obj);

    if (!riid || !obj) return E_INVALIDARG;

    if (IsEqualGUID(riid, &IID_IUnknown)
        || IsEqualGUID(riid, &IID_IAgileObject)
        || IsEqualGUID(riid, &IID_IAgileReference))
    {
        IUnknown_AddRef(iface);
        *obj = iface;
        return S_OK;
    }

    *obj = NULL;
    FIXME("interface %s is not implemented\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI agile_ref_AddRef(IAgileReference *iface)
{
    struct agile_reference *impl = impl_from_IAgileReference(iface);
    return InterlockedIncrement(&impl->ref);
}

static ULONG WINAPI agile_ref_Release(IAgileReference *iface)
{
    struct agile_reference *impl = impl_from_IAgileReference(iface);
    LONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        TRACE("destroying %p\n", iface);

        if (impl->obj)
            IUnknown_Release(impl->obj);

        if (impl->marshal_stream)
        {
            LARGE_INTEGER zero = {0};

            IStream_Seek(impl->marshal_stream, zero, STREAM_SEEK_SET, NULL);
            CoReleaseMarshalData(impl->marshal_stream);
            IStream_Release(impl->marshal_stream);
        }
        DeleteCriticalSection(&impl->cs);
        free(impl);
    }

    return ref;
}

static HRESULT WINAPI agile_ref_Resolve(IAgileReference *iface, REFIID riid, void **obj)
{
    struct agile_reference *impl = impl_from_IAgileReference(iface);
    LARGE_INTEGER zero = {0};
    HRESULT hr;

    TRACE("(%p, %s, %p)\n", iface, debugstr_guid(riid), obj);

    if (impl->is_agile)
        return IUnknown_QueryInterface(impl->obj, riid, obj);

    EnterCriticalSection(&impl->cs);
    if (impl->option == AGILEREFERENCE_DELAYEDMARSHAL && impl->marshal_stream == NULL)
    {
        if (FAILED(hr = marshal_object_in_agile_reference(impl, riid, impl->obj)))
        {
            LeaveCriticalSection(&impl->cs);
            return hr;
        }

        IUnknown_Release(impl->obj);
        impl->obj = NULL;
    }

    if (SUCCEEDED(hr = IStream_Seek(impl->marshal_stream, zero, STREAM_SEEK_SET, NULL)))
        hr = CoUnmarshalInterface(impl->marshal_stream, riid, obj);

    LeaveCriticalSection(&impl->cs);
    return hr;
}

static const IAgileReferenceVtbl agile_ref_vtbl =
{
    agile_ref_QueryInterface,
    agile_ref_AddRef,
    agile_ref_Release,
    agile_ref_Resolve,
};

static BOOL object_has_interface(IUnknown *obj, REFIID iid)
{
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface(obj, iid, (void **)&unk);
    if (SUCCEEDED(hr))
        IUnknown_Release(unk);
    return SUCCEEDED(hr);
}

/***********************************************************************
 *      RoGetAgileReference (combase.@)
 */
HRESULT WINAPI RoGetAgileReference(enum AgileReferenceOptions option, REFIID riid, IUnknown *obj,
                                   IAgileReference **agile_reference)
{
    struct agile_reference *impl;
    HRESULT hr;

    TRACE("(%d, %s, %p, %p).\n", option, debugstr_guid(riid), obj, agile_reference);

    if (option != AGILEREFERENCE_DEFAULT && option != AGILEREFERENCE_DELAYEDMARSHAL)
        return E_INVALIDARG;

    if (!InternalIsProcessInitialized())
    {
        ERR("Apartment not initialized\n");
        return CO_E_NOTINITIALIZED;
    }

    if (!object_has_interface(obj, riid))
        return E_NOINTERFACE;
    if (object_has_interface(obj, &IID_INoMarshal))
        return CO_E_NOT_SUPPORTED;

    impl = calloc(1, sizeof(*impl));
    if (!impl)
        return E_OUTOFMEMORY;

    impl->IAgileReference_iface.lpVtbl = &agile_ref_vtbl;
    impl->option = option;
    impl->is_agile = object_has_interface(obj, &IID_IAgileObject);
    impl->ref = 1;

    if (option == AGILEREFERENCE_DELAYEDMARSHAL || impl->is_agile)
    {
        impl->obj = obj;
        IUnknown_AddRef(impl->obj);
    }
    else if (option == AGILEREFERENCE_DEFAULT)
    {
        if (FAILED(hr = marshal_object_in_agile_reference(impl, riid, obj)))
        {
            free(impl);
            return hr;
        }
    }

    InitializeCriticalSection(&impl->cs);

    *agile_reference = &impl->IAgileReference_iface;
    return S_OK;
}

/***********************************************************************
 *      RoFailFastWithErrorContextInternal2 (combase.@)
 */
void WINAPI RoFailFastWithErrorContextInternal2(HRESULT error, ULONG exception_count, void *information)
{
    FIXME( "%#lx, %lu, %p: stowed exception information is not reported\n", error, exception_count, information );
    RaiseFailFastException( NULL, NULL, 0 );
}

/***********************************************************************
 *      RoGetApartmentIdentifier (combase.@)
 */
HRESULT WINAPI RoGetApartmentIdentifier(UINT64 *identifier)
{
    FIXME("(%p): stub\n", identifier);

    if (!identifier)
        return E_INVALIDARG;

    *identifier = 0xdeadbeef;
    return S_OK;
}

/***********************************************************************
 *      RoRegisterForApartmentShutdown (combase.@)
 */
HRESULT WINAPI RoRegisterForApartmentShutdown(IApartmentShutdown *callback,
        UINT64 *identifier, APARTMENT_SHUTDOWN_REGISTRATION_COOKIE *cookie)
{
    HRESULT hr;

    FIXME("(%p, %p, %p): stub\n", callback, identifier, cookie);

    hr = RoGetApartmentIdentifier(identifier);
    if (FAILED(hr))
        return hr;

    if (cookie)
        *cookie = (void *)0xcafecafe;
    return S_OK;
}

/***********************************************************************
 *      RoGetServerActivatableClasses (combase.@)
 */
HRESULT WINAPI RoGetServerActivatableClasses(HSTRING name, HSTRING **classes, DWORD *count)
{
    FIXME("(%p, %p, %p): stub\n", name, classes, count);

    if (count)
        *count = 0;
    return S_OK;
}

/***********************************************************************
 *      RoRegisterActivationFactories (combase.@)
 */
HRESULT WINAPI RoRegisterActivationFactories(HSTRING *classes, PFNGETACTIVATIONFACTORY *callbacks,
                                             UINT32 count, RO_REGISTRATION_COOKIE *cookie)
{
    FIXME("(%p, %p, %d, %p): stub\n", classes, callbacks, count, cookie);

    return S_OK;
}

/*
 * WinRT restricted error info: the per-thread error record that language exceptions
 * originate into, and that error propagation across WinRT boundaries reads back.
 */

/* IRestrictedErrorInfo v2 (GetErrorDetails, GetReference, ResolveReference). */
static const GUID IID_IRestrictedErrorInfo2 = {0x82ba70a5, 0x79a1, 0x4fe8, {0x8c, 0xe5, 0x13, 0xe5, 0x72, 0x33, 0x9f, 0x04}};

struct restricted_error_info
{
    IRestrictedErrorInfo IRestrictedErrorInfo_iface;
    LONG refcount;
    HRESULT error;
    BSTR description;
    BSTR restricted_description;
    BSTR capability;
    IUnknown *language_exception;
};

static inline struct restricted_error_info *impl_from_IRestrictedErrorInfo( IRestrictedErrorInfo *iface )
{
    return CONTAINING_RECORD( iface, struct restricted_error_info, IRestrictedErrorInfo_iface );
}

static HRESULT WINAPI restricted_error_info_QueryInterface( IRestrictedErrorInfo *iface, REFIID iid, void **out )
{
    struct restricted_error_info *impl = impl_from_IRestrictedErrorInfo( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( &IID_IUnknown, iid ) ||
        IsEqualGUID( &IID_IRestrictedErrorInfo, iid ) ||
        IsEqualGUID( &IID_IRestrictedErrorInfo2, iid ))
    {
        *out = &impl->IRestrictedErrorInfo_iface;
        IRestrictedErrorInfo_AddRef( *out );
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI restricted_error_info_AddRef( IRestrictedErrorInfo *iface )
{
    struct restricted_error_info *impl = impl_from_IRestrictedErrorInfo( iface );
    return InterlockedIncrement( &impl->refcount );
}

static ULONG WINAPI restricted_error_info_Release( IRestrictedErrorInfo *iface )
{
    struct restricted_error_info *impl = impl_from_IRestrictedErrorInfo( iface );
    ULONG ref = InterlockedDecrement( &impl->refcount );

    if (!ref)
    {
        if (impl->language_exception) IUnknown_Release( impl->language_exception );
        SysFreeString( impl->description );
        SysFreeString( impl->restricted_description );
        SysFreeString( impl->capability );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI restricted_error_info_GetErrorDetails( IRestrictedErrorInfo *iface, BSTR *desc,
                                                             HRESULT *error, BSTR *restricted, BSTR *sid )
{
    struct restricted_error_info *impl = impl_from_IRestrictedErrorInfo( iface );

    TRACE( "iface %p, desc %p, error %p, restricted %p, sid %p.\n", iface, desc, error, restricted, sid );

    if (!desc || !error || !restricted || !sid) return E_INVALIDARG;

    *desc       = impl->description           ? SysAllocString( impl->description )           : NULL;
    *restricted = impl->restricted_description ? SysAllocString( impl->restricted_description ) : NULL;
    *sid        = impl->capability            ? SysAllocString( impl->capability )            : NULL;
    *error      = impl->error;

    return (impl->description || impl->restricted_description) ? S_OK : S_FALSE;
}

static HRESULT WINAPI restricted_error_info_GetReference( IRestrictedErrorInfo *iface, BSTR *reference )
{
    TRACE( "iface %p, reference %p.\n", iface, reference );

    if (!reference) return E_INVALIDARG;
    *reference = NULL;
    return S_OK;
}

static HRESULT WINAPI restricted_error_info_ResolveReference( IRestrictedErrorInfo *iface, HSTRING reference,
                                                              BSTR *desc )
{
    TRACE( "iface %p, reference %s, desc %p.\n", iface, debugstr_hstring( reference ), desc );

    if (!desc) return E_INVALIDARG;
    *desc = NULL;
    return S_FALSE;
}

/*
 * The v2 interface extends v1 with ResolveReference; a single object answers both,
 * since the additional method is appended to the vtable.
 */
static const struct restricted_error_info_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IRestrictedErrorInfo *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IRestrictedErrorInfo *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IRestrictedErrorInfo *iface );
    HRESULT (STDMETHODCALLTYPE *GetErrorDetails)( IRestrictedErrorInfo *iface, BSTR *desc, HRESULT *error,
                                                  BSTR *restricted, BSTR *sid );
    HRESULT (STDMETHODCALLTYPE *GetReference)( IRestrictedErrorInfo *iface, BSTR *reference );
    HRESULT (STDMETHODCALLTYPE *ResolveReference)( IRestrictedErrorInfo *iface, HSTRING reference, BSTR *desc );
}
restricted_error_info_vtbl =
{
    restricted_error_info_QueryInterface,
    restricted_error_info_AddRef,
    restricted_error_info_Release,
    restricted_error_info_GetErrorDetails,
    restricted_error_info_GetReference,
    restricted_error_info_ResolveReference,
};

static HRESULT create_restricted_error_info( HRESULT error, const WCHAR *description, const WCHAR *restricted,
                                             IUnknown *language_exception, IRestrictedErrorInfo **out )
{
    struct restricted_error_info *impl;

    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IRestrictedErrorInfo_iface.lpVtbl = (const IRestrictedErrorInfoVtbl *)&restricted_error_info_vtbl;
    impl->refcount = 1;
    impl->error = error;
    if (description) impl->description = SysAllocString( description );
    if (restricted) impl->restricted_description = SysAllocString( restricted );
    if (language_exception)
    {
        impl->language_exception = language_exception;
        IUnknown_AddRef( language_exception );
    }

    *out = &impl->IRestrictedErrorInfo_iface;
    return S_OK;
}

/* The restricted error info is stored per thread; the store is consulted by the
 * propagation helpers and by COM proxies crossing apartment boundaries. */
static RTL_SRWLOCK error_info_lock = RTL_SRWLOCK_INIT;

struct error_info_entry
{
    ULONG tid;
    IRestrictedErrorInfo *info;
};

static struct error_info_entry *error_info_entries;
static ULONG error_info_count, error_info_alloc;

static void store_thread_error_info( IRestrictedErrorInfo *info )
{
    ULONG tid = HandleToUlong( NtCurrentTeb()->ClientId.UniqueThread );
    IRestrictedErrorInfo *old = NULL;
    ULONG i;

    RtlAcquireSRWLockExclusive( &error_info_lock );

    for (i = 0; i < error_info_count; i++)
    {
        if (error_info_entries[i].tid == tid)
        {
            old = error_info_entries[i].info;
            error_info_entries[i].info = info;
            RtlReleaseSRWLockExclusive( &error_info_lock );
            if (old) IRestrictedErrorInfo_Release( old );
            return;
        }
    }

    if (error_info_count == error_info_alloc)
    {
        ULONG alloc = error_info_alloc ? error_info_alloc * 2 : 8;
        struct error_info_entry *entries = realloc( error_info_entries, alloc * sizeof(*entries) );
        if (!entries)
        {
            RtlReleaseSRWLockExclusive( &error_info_lock );
            if (info) IRestrictedErrorInfo_Release( info );
            return;
        }
        error_info_entries = entries;
        error_info_alloc = alloc;
    }

    error_info_entries[error_info_count].tid  = tid;
    error_info_entries[error_info_count].info = info;
    error_info_count++;
    RtlReleaseSRWLockExclusive( &error_info_lock );
}

static IRestrictedErrorInfo *take_thread_error_info( void )
{
    ULONG tid = HandleToUlong( NtCurrentTeb()->ClientId.UniqueThread );
    IRestrictedErrorInfo *info = NULL;
    ULONG i;

    RtlAcquireSRWLockExclusive( &error_info_lock );

    for (i = 0; i < error_info_count; i++)
    {
        if (error_info_entries[i].tid == tid)
        {
            info = error_info_entries[i].info;
            error_info_entries[i].info = NULL;
            break;
        }
    }

    RtlReleaseSRWLockExclusive( &error_info_lock );
    return info;
}

/***********************************************************************
 *      GetRestrictedErrorInfo (combase.@)
 */
HRESULT WINAPI GetRestrictedErrorInfo(IRestrictedErrorInfo **info)
{
    TRACE( "(%p)\n", info );

    if (!info) return E_POINTER;
    if (!(*info = take_thread_error_info())) return S_FALSE;
    return S_OK;
}

/***********************************************************************
 *      SetRestrictedErrorInfo (combase.@)
 */
HRESULT WINAPI SetRestrictedErrorInfo(IRestrictedErrorInfo *info)
{
    TRACE( "(%p)\n", info );

    if (info) IRestrictedErrorInfo_AddRef( info );
    store_thread_error_info( info );
    return S_OK;
}

/***********************************************************************
 *      RoOriginateLanguageException (combase.@)
 */
BOOL WINAPI RoOriginateLanguageException(HRESULT error, HSTRING message, IUnknown *language_exception)
{
    IRestrictedErrorInfo *info;
    BOOL ret = FALSE;

    TRACE( "error %#lx, message %s, exception %p\n", error, debugstr_hstring( message ), language_exception );

    if (!language_exception) return FALSE;

    if (SUCCEEDED( create_restricted_error_info( error, WindowsGetStringRawBuffer( message, NULL ), NULL,
                                                 language_exception, &info ) ))
    {
        SetRestrictedErrorInfo( info );
        IRestrictedErrorInfo_Release( info );
        ret = TRUE;
    }
    return ret;
}

/***********************************************************************
 *      RoOriginateError (combase.@)
 */
BOOL WINAPI RoOriginateError(HRESULT error, HSTRING message)
{
    IRestrictedErrorInfo *info;
    BOOL ret = FALSE;

    TRACE( "%#lx, %s\n", error, debugstr_hstring( message ) );

    if (SUCCEEDED( create_restricted_error_info( error, WindowsGetStringRawBuffer( message, NULL ), NULL,
                                                 NULL, &info ) ))
    {
        SetRestrictedErrorInfo( info );
        IRestrictedErrorInfo_Release( info );
        ret = TRUE;
    }
    return ret;
}

/***********************************************************************
 *      RoOriginateErrorW (combase.@)
 */
BOOL WINAPI RoOriginateErrorW(HRESULT error, UINT max_len, const WCHAR *message)
{
    IRestrictedErrorInfo *info;
    BOOL ret = FALSE;

    TRACE( "error %#lx, max_len %u, message %s\n", error, max_len, wine_dbgstr_wn( message, max_len ) );

    if (!message) return FALSE;

    {
        BSTR str = SysAllocStringLen( message, max_len );
        if (!str) return FALSE;
        if (SUCCEEDED( create_restricted_error_info( error, str, NULL, NULL, &info ) ))
        {
            SetRestrictedErrorInfo( info );
            IRestrictedErrorInfo_Release( info );
            ret = TRUE;
        }
        SysFreeString( str );
    }
    return ret;
}

/***********************************************************************
 *      RoReportUnhandledError (combase.@)
 */
HRESULT WINAPI RoReportUnhandledError(IRestrictedErrorInfo *info)
{
    TRACE( "(%p)\n", info );
    return S_OK;
}

static UINT32 error_reporting_flags = RO_ERROR_REPORTING_USESETERRORINFO;

/***********************************************************************
 *      RoSetErrorReportingFlags (combase.@)
 */
HRESULT WINAPI RoSetErrorReportingFlags(UINT32 flags)
{
    TRACE("(%#x)\n", flags);

    error_reporting_flags = flags;
    return S_OK;
}

/***********************************************************************
 *      RoGetErrorReportingFlags (combase.@)
 */
HRESULT WINAPI RoGetErrorReportingFlags(UINT32 *flags)
{
    FIXME("(%p): stub\n", flags);

    if (!flags)
        return E_POINTER;

    *flags = error_reporting_flags;
    return S_OK;
}


/***********************************************************************
 *      CleanupTlsOleState (combase.@)
 */
void WINAPI CleanupTlsOleState(void *unknown)
{
    FIXME("(%p): stub\n", unknown);
}

/***********************************************************************
 *      DllGetActivationFactory (combase.@)
 */
HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    FIXME("(%s, %p): stub\n", debugstr_hstring(classid), factory);

    return REGDB_E_CLASSNOTREG;
}

/***********************************************************************
 *      RoFailFastWithErrorContext (combase.@)
 */
void WINAPI RoFailFastWithErrorContext(HRESULT hr)
{
    FIXME("(0x%08lx)\n", hr);
    RaiseFailFastException(NULL, NULL, 0);
}
