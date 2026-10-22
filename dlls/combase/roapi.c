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

    if (!wcscmp(classid, L"Windows.ApplicationModel.Background.BackgroundExecutionManager") ||
        !wcsncmp(classid, L"Windows.Networking.PushNotifications.", 37) ||
        !wcscmp(classid, L"Windows.ApplicationModel.AppInstance") ||
        !wcscmp(classid, L"Microsoft.Windows.AppLifecycle.AppInstance") ||
        !wcscmp(classid, L"Windows.ApplicationModel.StartupTask") ||
        (!wcsncmp(classid, L"Windows.ApplicationModel.DataTransfer.", 38) &&
         wcsncmp(classid, L"Windows.ApplicationModel.DataTransfer.DragDrop.", 47)))
    {
        *out = wcsdup(L"twinapi.appcore.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Windows.Security.Cryptography.", 30))
    {
        *out = wcsdup(L"cryptowinrt.dll");
        return S_OK;
    }

    if (!wcsncmp(classid, L"Microsoft.Web.WebView2.Core.", 28))
    {
        *out = wcsdup(L"Microsoft.Web.WebView2.Core.dll");
        return S_OK;
    }

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

/***********************************************************************
 *      RoGetParameterizedTypeInstanceIID (combase.@)
 */
HRESULT WINAPI RoGetParameterizedTypeInstanceIID(UINT32 name_element_count, const WCHAR **name_elements,
                                                 const IRoMetaDataLocator *meta_data_locator, GUID *iid,
                                                 ROPARAMIIDHANDLE *hiid)
{
    FIXME("stub: %d %p %p %p %p\n", name_element_count, name_elements, meta_data_locator, iid, hiid);
    if (iid) *iid = GUID_NULL;
    if (hiid) *hiid = INVALID_HANDLE_VALUE;
    return E_NOTIMPL;
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
        ERR("RoActivateInstance (%s) returned hr 0x%08lx, instance %p\n",
            wine_dbgstr_hstring(classid), hr, instance ? *instance : NULL);
        IActivationFactory_Release(factory);
    }
    else
    {
        ERR("RoActivateInstance (%s) RoGetActivationFactory failed hr 0x%08lx\n",
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
    void *frames[32];
    USHORT n_frames, k;
    ULONG i, j;
    FIXME("%#lx, %lu, %p stub.\n", error, exception_count, information);
    n_frames = RtlCaptureStackBackTrace(0, 32, frames, NULL);
    for (k = 0; k < n_frames; k++)
        FIXME("  backtrace[%u] = %p\n", k, frames[k]);
    if (information && exception_count)
    {
        void **ptrs = (void **)information;
        for (i = 0; i < exception_count; i++)
        {
            void *p = ptrs[i];
            if (p)
            {
                ULONG64 *q = (ULONG64 *)p;
                FIXME("  stowed[%lu] at %p:\n", i, p);
                for (j = 0; j < 8; j++)
                    FIXME("    [%02lu] %016llx\n", j, q[j]);
                if (q[4])
                {
                    ULONG64 *bt = (ULONG64 *)q[4];
                    DWORD count = (DWORD)(q[3] & 0xffffffff);
                    DWORD s_idx;
                    ERR("  stowed stack trace (count %lu):\n", count);
                    for (s_idx = 0; s_idx < count && s_idx < 16; s_idx++)
                        ERR("    stowed_bt[%lu] = %016llx\n", s_idx, bt[s_idx]);
                }
            }
        }
    }
    RaiseFailFastException(NULL, NULL, 0);
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

/***********************************************************************
 *      GetRestrictedErrorInfo (combase.@)
 */
HRESULT WINAPI GetRestrictedErrorInfo(IRestrictedErrorInfo **info)
{
    TRACE( "(%p)\n", info );
    if (info) *info = NULL;
    return S_FALSE;
}

/***********************************************************************
 *      SetRestrictedErrorInfo (combase.@)
 */
HRESULT WINAPI SetRestrictedErrorInfo(IRestrictedErrorInfo *info)
{
    TRACE( "(%p)\n", info );
    return S_OK;
}

/***********************************************************************
 *      RoOriginateLanguageException (combase.@)
 */
BOOL WINAPI RoOriginateLanguageException(HRESULT error, HSTRING message, IUnknown *language_exception)
{
    TRACE("error %#lx, message %s, exception %p\n", error, debugstr_hstring(message), language_exception);
    if (language_exception)
    {
        IRestrictedErrorInfo *restricted;
        IErrorInfo *error_info;
        static const GUID IID_IRestrictedErrorInfo = {0x82ba70a5, 0x79a1, 0x4fe8, {0x8c, 0xe5, 0x13, 0xe5, 0x72, 0x33, 0x9f, 0x04}};
        static const GUID IID_IErrorInfo = {0x1cf2b43f, 0x97b2, 0x11cf, {0xb8, 0xf2, 0x00, 0xaa, 0x00, 0x6e, 0x34, 0xdb}};

        if (SUCCEEDED(IUnknown_QueryInterface(language_exception, &IID_IRestrictedErrorInfo, (void **)&restricted)))
        {
            BSTR desc = NULL, rest = NULL, cap = NULL;
            HRESULT code = 0;
            if (SUCCEEDED(IRestrictedErrorInfo_GetErrorDetails(restricted, &desc, &code, &rest, &cap)))
            {
                TRACE("  RestrictedError: desc=%s, code=%#lx, restricted=%s\n",
                      wine_dbgstr_w(desc), code, wine_dbgstr_w(rest));
                SysFreeString(desc);
                SysFreeString(rest);
                SysFreeString(cap);
            }
            IRestrictedErrorInfo_Release(restricted);
        }
        else if (SUCCEEDED(IUnknown_QueryInterface(language_exception, &IID_IErrorInfo, (void **)&error_info)))
        {
            BSTR desc = NULL;
            if (SUCCEEDED(IErrorInfo_GetDescription(error_info, &desc)))
            {
                TRACE("  ErrorInfo: desc=%s\n", wine_dbgstr_w(desc));
                SysFreeString(desc);
            }
            IErrorInfo_Release(error_info);
        }
    }
    return FALSE;
}

/***********************************************************************
 *      RoOriginateError (combase.@)
 */
BOOL WINAPI RoOriginateError(HRESULT error, HSTRING message)
{
    TRACE("%#lx, %s\n", error, debugstr_hstring(message));
    return FALSE;
}

/***********************************************************************
 *      RoOriginateErrorW (combase.@)
 */
BOOL WINAPI RoOriginateErrorW(HRESULT error, UINT max_len, const WCHAR *message)
{
    TRACE("error %#lx, max_len %u, message %s\n", error, max_len, wine_dbgstr_wn(message, max_len));
    return FALSE;
}

/***********************************************************************
 *      RoReportUnhandledError (combase.@)
 */
HRESULT WINAPI RoReportUnhandledError(IRestrictedErrorInfo *info)
{
    FIXME("(%p): stub\n", info);
    return S_OK;
}

/***********************************************************************
 *      RoSetErrorReportingFlags (combase.@)
 */
HRESULT WINAPI RoSetErrorReportingFlags(UINT32 flags)
{
    FIXME("(%08x): stub\n", flags);
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

    *flags = RO_ERROR_REPORTING_USESETERRORINFO;
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
