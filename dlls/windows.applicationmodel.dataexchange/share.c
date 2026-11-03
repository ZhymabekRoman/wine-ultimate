/* WinRT DataTransferManager, HtmlFormatHelper, ShareProvider, share options
 *
 * Copyright 2026 Wine Developers
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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dataexchange);

/*
 * The share-UI surface of the namespace, which the RoGetActivationFactory
 * route in combase sends here together with the DataPackage/Clipboard
 * classes.  There is no share broker in a prefix, so the manager hands out
 * a manager object whose event points accept registrations, the statics
 * answer S_OK (the share click completes instead of throwing), HtmlFormatHelper
 * round-trips the html-fragment format, and the small activatable option/
 * provider classes keep their fields in memory.
 */

static LONG next_share_token;

static HRESULT share_add_event( EventRegistrationToken *token )
{
    if (!token) return E_POINTER;
    token->value = InterlockedIncrement( &next_share_token );
    return S_OK;
}

/* ------------------------------------------------ DataTransferManager -- */

struct dt_manager
{
    const void *manager_vtbl;   /* IDataTransferManager at offset 0 */
    const void *manager2_vtbl;  /* IDataTransferManager2 at offset 1 pointer */
    LONG ref;
};

static const void *dt_manager_vtbl[];
static const void *dt_manager2_vtbl[];

/* Every interface group of the object shares the IUnknown/entry-point
 * implementations; resolve the object from the group's vtable pointer so one
 * set of method bodies serves every group at its own field offset. */
static struct dt_manager *impl_from_manager_any( const void *iface )
{
    static const void *groups[] = { dt_manager_vtbl, dt_manager2_vtbl };
    static const int offsets[] = { 0, 1 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct dt_manager *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct dt_manager *)iface;
}

static const WCHAR dt_manager_class[] = L"Windows.ApplicationModel.DataTransfer.DataTransferManager";

static HRESULT WINAPI manager_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct dt_manager *impl = impl_from_manager_any( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDataTransferManager ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataTransferManager2 ))
    {
        *out = (void *)&impl->manager2_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI manager_AddRef( const void *iface )
{
    struct dt_manager *impl = impl_from_manager_any( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI manager_Release( const void *iface )
{
    struct dt_manager *impl = impl_from_manager_any( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI manager_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI manager_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( dt_manager_class, lstrlenW( dt_manager_class ), class_name );
}

static HRESULT WINAPI manager_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

#define MANAGER_EVENT( pfx, name )                                                                   \
    static HRESULT WINAPI pfx##_add_##name( const void *iface, void *handler, EventRegistrationToken *token ) \
    {                                                                                               \
        TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );                        \
        return share_add_event( token );                                                            \
    }                                                                                               \
    static HRESULT WINAPI pfx##_remove_##name( const void *iface, EventRegistrationToken token )    \
    {                                                                                               \
        TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );               \
        return S_OK;                                                                                \
    }

MANAGER_EVENT( manager, DataRequested )
MANAGER_EVENT( manager, TargetApplicationChosen )
MANAGER_EVENT( manager2, ShareProvidersRequested )

static const void *dt_manager_vtbl[] =
{
    manager_QueryInterface,
    manager_AddRef,
    manager_Release,
    manager_GetIids,
    manager_GetRuntimeClassName,
    manager_GetTrustLevel,
    manager_add_DataRequested,
    manager_remove_DataRequested,
    manager_add_TargetApplicationChosen,
    manager_remove_TargetApplicationChosen,
};

static HRESULT WINAPI manager2_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return manager_QueryInterface( (void *)impl_from_manager_any( iface ), iid, out );
}

static ULONG WINAPI manager2_AddRef( const void *iface )
{
    return manager_AddRef( (void *)impl_from_manager_any( iface ) );
}

static ULONG WINAPI manager2_Release( const void *iface )
{
    return manager_Release( (void *)impl_from_manager_any( iface ) );
}

static const void *dt_manager2_vtbl[] =
{
    manager2_QueryInterface,
    manager2_AddRef,
    manager2_Release,
    manager_GetIids,
    manager_GetRuntimeClassName,
    manager_GetTrustLevel,
    manager2_add_ShareProvidersRequested,
    manager2_remove_ShareProvidersRequested,
};

static struct dt_manager *create_dt_manager( void )
{
    struct dt_manager *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->manager_vtbl = dt_manager_vtbl;
    impl->manager2_vtbl = dt_manager2_vtbl;
    impl->ref = 1;
    return impl;
}

struct dt_manager_statics
{
    const void *factory_vtbl;   /* IActivationFactory at offset 0 */
    const void *statics_vtbl;   /* IDataTransferManagerStatics at offset 1 pointer */
    const void *statics2_vtbl;  /* IDataTransferManagerStatics2 at offset 2 pointers */
    const void *statics3_vtbl;  /* IDataTransferManagerStatics3 at offset 3 pointers */
    const void *interop_vtbl;   /* IDataTransferManagerInterop at offset 4 pointers */
    LONG ref;
};

static const void *dt_manager_factory_vtbl[];
static const void *dt_manager_statics_vtbl[];
static const void *dt_manager_statics2_vtbl[];
static const void *dt_manager_statics3_vtbl[];
static const void *dt_manager_interop_vtbl[];

/* Every interface group of the object shares the IUnknown/entry-point
 * implementations; resolve the object from the group's vtable pointer so one
 * set of method bodies serves every group at its own field offset. */
static struct dt_manager_statics *impl_from_dtstatics_any( const void *iface )
{
    static const void *groups[] = { dt_manager_factory_vtbl, dt_manager_statics_vtbl, dt_manager_statics2_vtbl, dt_manager_statics3_vtbl, dt_manager_interop_vtbl };
    static const int offsets[] = { 0, 1, 2, 3, 4 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct dt_manager_statics *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct dt_manager_statics *)iface;
}

static HRESULT WINAPI dtstatics_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct dt_manager_statics *impl = impl_from_dtstatics_any( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataTransferManagerStatics ))
    {
        *out = (void *)&impl->statics_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataTransferManagerStatics2 ))
    {
        *out = (void *)&impl->statics2_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataTransferManagerStatics3 ))
    {
        *out = (void *)&impl->statics3_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataTransferManagerInterop ))
    {
        *out = (void *)&impl->interop_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI dtstatics_AddRef( const void *iface )
{
    struct dt_manager_statics *impl = impl_from_dtstatics_any( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI dtstatics_Release( const void *iface )
{
    struct dt_manager_statics *impl = impl_from_dtstatics_any( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI dtstatics_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI dtstatics_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( dt_manager_class, lstrlenW( dt_manager_class ), class_name );
}

static HRESULT WINAPI dtstatics_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI dtstatics_ActivateInstance( const void *iface, void **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );
    return E_FAIL;
}

static const void *dt_manager_factory_vtbl[] =
{
    dtstatics_QueryInterface,
    dtstatics_AddRef,
    dtstatics_Release,
    dtstatics_GetIids,
    dtstatics_GetRuntimeClassName,
    dtstatics_GetTrustLevel,
    dtstatics_ActivateInstance,
};

static HRESULT WINAPI dts1_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return dtstatics_QueryInterface( (void *)impl_from_dtstatics_any( iface ), iid, out );
}

static ULONG WINAPI dts1_AddRef( const void *iface )
{
    return dtstatics_AddRef( (void *)impl_from_dtstatics_any( iface ) );
}

static ULONG WINAPI dts1_Release( const void *iface )
{
    return dtstatics_Release( (void *)impl_from_dtstatics_any( iface ) );
}

static HRESULT WINAPI dts2_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return dtstatics_QueryInterface( (void *)impl_from_dtstatics_any( iface ), iid, out );
}

static ULONG WINAPI dts2_AddRef( const void *iface )
{
    return dtstatics_AddRef( (void *)impl_from_dtstatics_any( iface ) );
}

static ULONG WINAPI dts2_Release( const void *iface )
{
    return dtstatics_Release( (void *)impl_from_dtstatics_any( iface ) );
}

static HRESULT WINAPI dts3_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return dtstatics_QueryInterface( (void *)impl_from_dtstatics_any( iface ), iid, out );
}

static ULONG WINAPI dts3_AddRef( const void *iface )
{
    return dtstatics_AddRef( (void *)impl_from_dtstatics_any( iface ) );
}

static ULONG WINAPI dts3_Release( const void *iface )
{
    return dtstatics_Release( (void *)impl_from_dtstatics_any( iface ) );
}

static HRESULT WINAPI dti_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return dtstatics_QueryInterface( (void *)impl_from_dtstatics_any( iface ), iid, out );
}

static ULONG WINAPI dti_AddRef( const void *iface )
{
    return dtstatics_AddRef( (void *)impl_from_dtstatics_any( iface ) );
}

static ULONG WINAPI dti_Release( const void *iface )
{
    return dtstatics_Release( (void *)impl_from_dtstatics_any( iface ) );
}

static HRESULT WINAPI dtstatics_ShowShareUI( const void *iface )
{
    /* No share broker: the click completes without a flyout, so the app's
     * share path returns from ShowShareUI normally instead of throwing. */
    TRACE( "iface %p.\n", iface );
    return S_OK;
}

static HRESULT WINAPI dtstatics_GetForCurrentView( const void *iface, void **result )
{
    struct dt_manager *manager;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(manager = create_dt_manager() )) return E_OUTOFMEMORY;
    *result = (void *)manager;
    return S_OK;
}

static const void *dt_manager_statics_vtbl[] =
{
    dts1_QueryInterface,
    dts1_AddRef,
    dts1_Release,
    dtstatics_GetIids,
    dtstatics_GetRuntimeClassName,
    dtstatics_GetTrustLevel,
    dtstatics_ShowShareUI,
    dtstatics_GetForCurrentView,
};

static HRESULT WINAPI dtstatics2_IsSupported( const void *iface, BOOLEAN *result )
{
    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    *result = TRUE;
    return S_OK;
}

static const void *dt_manager_statics2_vtbl[] =
{
    dts2_QueryInterface,
    dts2_AddRef,
    dts2_Release,
    dtstatics_GetIids,
    dtstatics_GetRuntimeClassName,
    dtstatics_GetTrustLevel,
    dtstatics2_IsSupported,
};

static HRESULT WINAPI dtstatics3_ShowShareUIWithOptions( const void *iface, void *options )
{
    TRACE( "iface %p, options %p.\n", iface, options );
    return S_OK;
}

static const void *dt_manager_statics3_vtbl[] =
{
    dts3_QueryInterface,
    dts3_AddRef,
    dts3_Release,
    dtstatics_GetIids,
    dtstatics_GetRuntimeClassName,
    dtstatics_GetTrustLevel,
    dtstatics3_ShowShareUIWithOptions,
};

static HRESULT WINAPI dtinterop_GetForWindow( const void *iface, HWND app_window, REFIID iid, void **result )
{
    struct dt_manager *manager;
    HRESULT hr;

    TRACE( "iface %p, app_window %p, iid %s, result %p.\n", iface, app_window, debugstr_guid( iid ), result );

    if (!result) return E_POINTER;
    if (!(manager = create_dt_manager() )) return E_OUTOFMEMORY;
    hr = manager_QueryInterface( (void *)manager, iid, result );
    manager_Release( (void *)manager );
    return hr;
}

static HRESULT WINAPI dtinterop_ShowShareUIForWindow( const void *iface, HWND app_window )
{
    TRACE( "iface %p, app_window %p.\n", iface, app_window );
    return S_OK;
}

static const void *dt_manager_interop_vtbl[] =
{
    dti_QueryInterface,
    dti_AddRef,
    dti_Release,
    dtinterop_GetForWindow,
    dtinterop_ShowShareUIForWindow,
};

static struct dt_manager_statics dt_manager_statics =
{
    dt_manager_factory_vtbl,
    dt_manager_statics_vtbl,
    dt_manager_statics2_vtbl,
    dt_manager_statics3_vtbl,
    dt_manager_interop_vtbl,
    1,
};

IActivationFactory *data_transfer_manager_factory = (IActivationFactory *)&dt_manager_statics;

/* -------------------------------------------------- HtmlFormatHelper --- */

/*
 * CreateHtmlFormat wraps the fragment into the canonical html-format string
 * (the Location/Version/offset header plus the html body, with the
 * StartFragment/EndFragment offsets pointing at the fragment text itself);
 * GetStaticFragment reads those offsets back, so the pair round-trips.
 */

static const WCHAR html_format_prefix[] =
    L"Location: data:text/html,<span id=\"selection\">&#x000A;</span><br>\r\n"
    L"Version:1.0\r\nStartHTML:0000000070\r\nEndHTML:%010u\r\nStartFragment:%010u\r\nEndFragment:%010u\r\n";
static const WCHAR html_body_prefix[] = L"<html><body><!--StartFragment-->";
static const WCHAR html_body_suffix[] = L"<!--EndFragment--></body></html>";

struct html_helper_statics
{
    const void *factory_vtbl;
    const void *statics_vtbl;
    LONG ref;
};

static inline struct html_helper_statics *impl_from_html_factory( const void *iface )
{
    return (struct html_helper_statics *)iface;
}

static const WCHAR html_helper_class[] = L"Windows.ApplicationModel.DataTransfer.HtmlFormatHelper";

static HRESULT WINAPI htmlstatics_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct html_helper_statics *impl = impl_from_html_factory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IHtmlFormatHelperStatics ))
    {
        *out = (void *)&impl->statics_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI htmlstatics_AddRef( const void *iface )
{
    struct html_helper_statics *impl = impl_from_html_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI htmlstatics_Release( const void *iface )
{
    struct html_helper_statics *impl = impl_from_html_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI htmlstatics_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI htmlstatics_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( html_helper_class, lstrlenW( html_helper_class ), class_name );
}

static HRESULT WINAPI htmlstatics_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI htmlstatics_ActivateInstance( const void *iface, void **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );
    return E_FAIL;
}

static const void *html_helper_factory_vtbl[] =
{
    htmlstatics_QueryInterface,
    htmlstatics_AddRef,
    htmlstatics_Release,
    htmlstatics_GetIids,
    htmlstatics_GetRuntimeClassName,
    htmlstatics_GetTrustLevel,
    htmlstatics_ActivateInstance,
};

static HRESULT WINAPI htmls_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return htmlstatics_QueryInterface( (void *)((const BYTE *)iface - sizeof(void *)), iid, out );
}

static ULONG WINAPI htmls_AddRef( const void *iface )
{
    return htmlstatics_AddRef( (void *)((const BYTE *)iface - sizeof(void *)) );
}

static ULONG WINAPI htmls_Release( const void *iface )
{
    return htmlstatics_Release( (void *)((const BYTE *)iface - sizeof(void *)) );
}

static HRESULT WINAPI htmlstatics_GetStaticFragment( const void *iface, HSTRING html_format,
                                                     HSTRING *html_fragment )
{
    static const WCHAR start_marker[] = L"StartFragment:";
    static const WCHAR end_marker[] = L"EndFragment:";
    const WCHAR *raw = WindowsGetStringRawBuffer( html_format, NULL );
    const WCHAR *start, *end;
    UINT32 start_offset = 0, end_offset = 0, len;
    ULONG i;

    TRACE( "iface %p, html_format %s, html_fragment %p.\n", iface, debugstr_hstring( html_format ), html_fragment );

    if (!html_fragment) return E_POINTER;
    if (!raw) return E_INVALIDARG;

    len = lstrlenW( raw );

    if ((start = wcsstr( raw, start_marker )) && (end = wcsstr( raw, end_marker )) && start < end)
    {
        start += ARRAY_SIZE( start_marker ) - 1;
        end += ARRAY_SIZE( end_marker ) - 1;
        for (i = 0; i < 10 && start[i] >= L'0' && start[i] <= L'9'; i++)
            start_offset = start_offset * 10 + (start[i] - L'0');
        for (i = 0; i < 10 && end[i] >= L'0' && end[i] <= L'9'; i++)
            end_offset = end_offset * 10 + (end[i] - L'0');
    }

    if (!end_offset || start_offset >= end_offset || end_offset > len)
        return WindowsCreateString( raw, len, html_fragment ); /* foreign input: hand it back */

    return WindowsCreateString( raw + start_offset, end_offset - start_offset, html_fragment );
}

static HRESULT WINAPI htmlstatics_CreateHtmlFormat( const void *iface, HSTRING html_fragment,
                                                    HSTRING *html_format )
{
    const WCHAR *fragment = WindowsGetStringRawBuffer( html_fragment, NULL );
    WCHAR header[512], *buffer;
    UINT32 header_len, fragment_len, start_fragment, end_fragment, end_html;
    HRESULT hr;

    TRACE( "iface %p, html_fragment %s, html_format %p.\n", iface, debugstr_hstring( html_fragment ), html_format );

    if (!html_format) return E_POINTER;
    *html_format = NULL;
    if (!fragment) return E_INVALIDARG;

    /* Format once with zero offsets: every placeholder renders ten digits,
     * so the header length is stable and the real offsets can be computed. */
    swprintf( header, ARRAY_SIZE( header ), html_format_prefix, 0u, 0u, 0u );
    header_len = lstrlenW( header );
    fragment_len = lstrlenW( fragment );
    start_fragment = header_len + (UINT32)lstrlenW( html_body_prefix );
    end_fragment = start_fragment + fragment_len;
    end_html = end_fragment + (UINT32)lstrlenW( html_body_suffix );

    swprintf( header, ARRAY_SIZE( header ), html_format_prefix, end_html, start_fragment, end_fragment );
    if (lstrlenW( header ) != header_len) return E_UNEXPECTED;

    if (!(buffer = calloc( header_len + end_html + 1, sizeof(WCHAR) ))) return E_OUTOFMEMORY;

    wcscpy( buffer, header );
    wcscpy( buffer + header_len, html_body_prefix );
    wcscpy( buffer + header_len + (UINT32)lstrlenW( html_body_prefix ), fragment );
    wcscpy( buffer + end_fragment, html_body_suffix );

    hr = WindowsCreateString( buffer, end_html, html_format );
    free( buffer );
    return hr;
}

static const void *html_helper_statics_vtbl[] =
{
    htmls_QueryInterface,
    htmls_AddRef,
    htmls_Release,
    htmlstatics_GetIids,
    htmlstatics_GetRuntimeClassName,
    htmlstatics_GetTrustLevel,
    htmlstatics_GetStaticFragment,
    htmlstatics_CreateHtmlFormat,
};

static struct html_helper_statics html_helper_statics =
{
    html_helper_factory_vtbl,
    html_helper_statics_vtbl,
    1,
};

IActivationFactory *html_format_helper_factory = (IActivationFactory *)&html_helper_statics;

/* ------------------------------------------------------ ShareProvider -- */

struct share_provider
{
    const void *provider_vtbl;
    LONG ref;
    HSTRING title;
    void *display_icon;
    __x_ABI_CWindows_CUI_CColor background_color;
    IInspectable *tag;
};

static inline struct share_provider *impl_from_provider( const void *iface )
{
    return (struct share_provider *)iface;
}

static const WCHAR share_provider_class[] = L"Windows.ApplicationModel.DataTransfer.ShareProvider";

static HRESULT WINAPI provider_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct share_provider *impl = impl_from_provider( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IShareProvider ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI provider_AddRef( const void *iface )
{
    struct share_provider *impl = impl_from_provider( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI provider_Release( const void *iface )
{
    struct share_provider *impl = impl_from_provider( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->title );
        if (impl->display_icon) IUnknown_Release( (IUnknown *)impl->display_icon );
        if (impl->tag) IInspectable_Release( impl->tag );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI provider_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI provider_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( share_provider_class, lstrlenW( share_provider_class ), class_name );
}

static HRESULT WINAPI provider_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI provider_get_Title( const void *iface, HSTRING *value )
{
    struct share_provider *impl = impl_from_provider( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->title, value );
}

static HRESULT WINAPI provider_get_DisplayIcon( const void *iface, void **value )
{
    struct share_provider *impl = impl_from_provider( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->display_icon;
    if (*value) IUnknown_AddRef( (IUnknown *)*value );
    return S_OK;
}

static HRESULT WINAPI provider_get_BackgroundColor( const void *iface, __x_ABI_CWindows_CUI_CColor *value )
{
    struct share_provider *impl = impl_from_provider( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->background_color;
    return S_OK;
}

static HRESULT WINAPI provider_get_Tag( const void *iface, void **value )
{
    struct share_provider *impl = impl_from_provider( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->tag;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI provider_put_Tag( const void *iface, void *value )
{
    struct share_provider *impl = impl_from_provider( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) IInspectable_AddRef( (IInspectable *)value );
    if (impl->tag) IInspectable_Release( impl->tag );
    impl->tag = (IInspectable *)value;
    return S_OK;
}

static const void *share_provider_vtbl[] =
{
    provider_QueryInterface,
    provider_AddRef,
    provider_Release,
    provider_GetIids,
    provider_GetRuntimeClassName,
    provider_GetTrustLevel,
    provider_get_Title,
    provider_get_DisplayIcon,
    provider_get_BackgroundColor,
    provider_get_Tag,
    provider_put_Tag,
};

struct share_provider_factory
{
    const void *factory_vtbl;
    const void *provider_factory_vtbl;   /* IShareProviderFactory at offset 1 pointer */
    LONG ref;
};

static inline struct share_provider_factory *impl_from_sp_factory( const void *iface )
{
    return (struct share_provider_factory *)iface;
}

static HRESULT WINAPI spfactory_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct share_provider_factory *impl = impl_from_sp_factory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IShareProviderFactory ))
    {
        *out = (void *)&impl->provider_factory_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI spfactory_AddRef( const void *iface )
{
    struct share_provider_factory *impl = impl_from_sp_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI spfactory_Release( const void *iface )
{
    struct share_provider_factory *impl = impl_from_sp_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI spfactory_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI spfactory_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( share_provider_class, lstrlenW( share_provider_class ), class_name );
}

static HRESULT WINAPI spfactory_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI spfactory_ActivateInstance( const void *iface, void **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );
    return E_FAIL;
}

static const void *share_provider_factory_vtbl[] =
{
    spfactory_QueryInterface,
    spfactory_AddRef,
    spfactory_Release,
    spfactory_GetIids,
    spfactory_GetRuntimeClassName,
    spfactory_GetTrustLevel,
    spfactory_ActivateInstance,
};

static HRESULT WINAPI spf_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return spfactory_QueryInterface( (void *)((const BYTE *)iface - sizeof(void *)), iid, out );
}

static ULONG WINAPI spf_AddRef( const void *iface )
{
    return spfactory_AddRef( (void *)((const BYTE *)iface - sizeof(void *)) );
}

static ULONG WINAPI spf_Release( const void *iface )
{
    return spfactory_Release( (void *)((const BYTE *)iface - sizeof(void *)) );
}

static HRESULT WINAPI spfactory_Create( const void *iface, HSTRING title, void *display_icon,
                                        __x_ABI_CWindows_CUI_CColor background_color, void *handler, void **result )
{
    struct share_provider *impl;

    TRACE( "iface %p, title %s, display_icon %p, handler %p, result %p.\n", iface, debugstr_hstring( title ),
           display_icon, handler, result );

    if (!result) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->provider_vtbl = share_provider_vtbl;
    impl->ref = 1;
    impl->background_color = background_color;
    if (title) WindowsDuplicateString( title, &impl->title );
    if (display_icon) IUnknown_AddRef( (IUnknown *)display_icon );
    impl->display_icon = display_icon;

    *result = (void *)impl;
    return S_OK;
}

static const void *share_provider_factory_provider_vtbl[] =
{
    spf_QueryInterface,
    spf_AddRef,
    spf_Release,
    spfactory_Create,
};

static struct share_provider_factory the_share_provider_factory =
{
    share_provider_factory_vtbl,
    share_provider_factory_provider_vtbl,
    1,
};

IActivationFactory *share_provider_factory = (IActivationFactory *)&the_share_provider_factory;

/* ------------------------------------------------------ ShareUIOptions,
 * --------------------------------------------------- ClipboardContentOptions */

struct simple_options
{
    const void *vtable;
    LONG ref;
    const WCHAR *class_name;
    BOOL is_share_ui;
    union
    {
        struct
        {
            ShareUITheme theme;
        } share_ui;
        struct
        {
            BOOLEAN roamable, history;
            struct winrt_vector *roaming_formats, *history_formats;
        } clipboard;
    } u;
};

static inline struct simple_options *impl_from_options( const void *iface )
{
    return (struct simple_options *)iface;
}

static const WCHAR share_ui_options_class[] = L"Windows.ApplicationModel.DataTransfer.ShareUIOptions";
static const WCHAR clipboard_options_class[] = L"Windows.ApplicationModel.DataTransfer.ClipboardContentOptions";

static HRESULT WINAPI options_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct simple_options *impl = impl_from_options( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        (impl->is_share_ui ? IsEqualGUID( iid, &IID_IShareUIOptions )
                           : IsEqualGUID( iid, &IID_IClipboardContentOptions )))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI options_AddRef( const void *iface )
{
    struct simple_options *impl = impl_from_options( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI options_Release( const void *iface )
{
    struct simple_options *impl = impl_from_options( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->u.clipboard.roaming_formats) vector_release( impl->u.clipboard.roaming_formats );
        if (impl->u.clipboard.history_formats) vector_release( impl->u.clipboard.history_formats );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI options_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI options_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    struct simple_options *impl = impl_from_options( iface );

    if (!class_name) return E_POINTER;
    return WindowsCreateString( impl->class_name, lstrlenW( impl->class_name ), class_name );
}

static HRESULT WINAPI options_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI options_get_Theme( const void *iface, ShareUITheme *value )
{
    struct simple_options *impl = impl_from_options( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->u.share_ui.theme;
    return S_OK;
}

static HRESULT WINAPI options_put_Theme( const void *iface, ShareUITheme value )
{
    struct simple_options *impl = impl_from_options( iface );

    TRACE( "iface %p, value %u.\n", iface, value );
    impl->u.share_ui.theme = value;
    return S_OK;
}

static const void *share_ui_options_vtbl[] =
{
    options_QueryInterface,
    options_AddRef,
    options_Release,
    options_GetIids,
    options_GetRuntimeClassName,
    options_GetTrustLevel,
    options_get_Theme,
    options_put_Theme,
};

static HRESULT WINAPI coptions_get_IsRoamable( const void *iface, BOOLEAN *value )
{
    struct simple_options *impl = impl_from_options( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->u.clipboard.roamable;
    return S_OK;
}

static HRESULT WINAPI coptions_put_IsRoamable( const void *iface, BOOLEAN value )
{
    struct simple_options *impl = impl_from_options( iface );

    TRACE( "iface %p, value %d.\n", iface, value );
    impl->u.clipboard.roamable = value;
    return S_OK;
}

static HRESULT WINAPI coptions_get_IsAllowedInHistory( const void *iface, BOOLEAN *value )
{
    struct simple_options *impl = impl_from_options( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = impl->u.clipboard.history;
    return S_OK;
}

static HRESULT WINAPI coptions_put_IsAllowedInHistory( const void *iface, BOOLEAN value )
{
    struct simple_options *impl = impl_from_options( iface );

    TRACE( "iface %p, value %d.\n", iface, value );
    impl->u.clipboard.history = value;
    return S_OK;
}

static HRESULT WINAPI coptions_get_RoamingFormats( const void *iface, void **value )
{
    struct simple_options *impl = impl_from_options( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = vector_iface( impl->u.clipboard.roaming_formats );
    vector_add_ref( impl->u.clipboard.roaming_formats );
    return S_OK;
}

static HRESULT WINAPI coptions_get_HistoryFormats( const void *iface, void **value )
{
    struct simple_options *impl = impl_from_options( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = vector_iface( impl->u.clipboard.history_formats );
    vector_add_ref( impl->u.clipboard.history_formats );
    return S_OK;
}

static const void *clipboard_content_options_vtbl[] =
{
    options_QueryInterface,
    options_AddRef,
    options_Release,
    options_GetIids,
    options_GetRuntimeClassName,
    options_GetTrustLevel,
    coptions_get_IsRoamable,
    coptions_put_IsRoamable,
    coptions_get_IsAllowedInHistory,
    coptions_put_IsAllowedInHistory,
    coptions_get_RoamingFormats,
    coptions_get_HistoryFormats,
};


/* CoreDragDropManager: the client activates it during CoreWindow drag-drop
 * wiring; without the class RoActivateInstance answers REGDB_E_CLASSNOTREG
 * and the .NET projection RaiseFailFastException (param 0x80040154) kills
 * the process (gate64).  Stand-in object answering the IInspectable surface;
 * specific interfaces answer E_NOINTERFACE until a caller demands one. */
struct core_ddm
{
    const void *vtable;
    LONG ref;
};

static inline struct core_ddm *impl_from_core_ddm( const void *iface ) { return (struct core_ddm *)iface; }

static HRESULT WINAPI cddm_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct core_ddm *impl = impl_from_core_ddm( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI cddm_AddRef( const void *iface ) { return InterlockedIncrement( &impl_from_core_ddm( iface )->ref ); }
static ULONG WINAPI cddm_Release( const void *iface )
{
    struct core_ddm *impl = impl_from_core_ddm( iface );
    LONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI cddm_GetIids( const void *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI cddm_GetRuntimeClassName( const void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.DataTransfer.DragDrop.Core.CoreDragDropManager",
                                73, name );
}

static HRESULT WINAPI cddm_GetTrustLevel( const void *iface, TrustLevel *trust )
{
    if (!trust) return E_POINTER;
    *trust = 0; /* TrustLevel_Base */
    return S_OK;
}

static const void *core_ddm_vtbl[] =
{
    cddm_QueryInterface, cddm_AddRef, cddm_Release, cddm_GetIids, cddm_GetRuntimeClassName, cddm_GetTrustLevel,
};

IInspectable *create_core_drag_drop_manager( void )
{
    struct core_ddm *impl;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->vtable = core_ddm_vtbl;
    impl->ref = 1;
    return (IInspectable *)impl;
}

IInspectable *create_share_ui_options( void )
{
    struct simple_options *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->vtable = share_ui_options_vtbl;
    impl->ref = 1;
    impl->class_name = share_ui_options_class;
    impl->is_share_ui = TRUE;
    impl->u.share_ui.theme = ShareUITheme_Default;
    return (IInspectable *)impl;
}

IInspectable *create_clipboard_content_options( void )
{
    struct simple_options *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->vtable = clipboard_content_options_vtbl;
    impl->ref = 1;
    impl->class_name = clipboard_options_class;
    impl->is_share_ui = FALSE;
    impl->u.clipboard.roamable = TRUE;
    impl->u.clipboard.history = TRUE;
    impl->u.clipboard.roaming_formats = create_vector( &IID___FIVector_1_HSTRING, &IID___FIVectorView_1_HSTRING,
                                                       &IID___FIIterable_1_HSTRING, &IID___FIIterator_1_HSTRING,
                                                       ELEM_STRING,
                                                       L"Windows.Foundation.Collections.IVector`1<HSTRING>" );
    impl->u.clipboard.history_formats = create_vector( &IID___FIVector_1_HSTRING, &IID___FIVectorView_1_HSTRING,
                                                       &IID___FIIterable_1_HSTRING, &IID___FIIterator_1_HSTRING,
                                                       ELEM_STRING,
                                                       L"Windows.Foundation.Collections.IVector`1<HSTRING>" );
    if (!impl->u.clipboard.roaming_formats || !impl->u.clipboard.history_formats)
    {
        options_Release( (void *)impl );
        return NULL;
    }
    return (IInspectable *)impl;
}
