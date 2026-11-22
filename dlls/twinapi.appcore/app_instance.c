/* WinRT Windows.ApplicationModel.AppInstance / Microsoft.Windows.AppLifecycle.AppInstance implementation
 *
 * Copyright 2025 Zhiyi Zhang for CodeWeavers
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
#include "roapi.h"
#include "shellapi.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

/* ---- Windows.ApplicationModel.AppInstance ---- */

static const GUID IID_IAppInstanceStatics = {0x9d11e77f, 0x9ea6, 0x47af, {0xa6, 0xec, 0x46, 0x78, 0x4c, 0x5b, 0xa2, 0x54}};
static const GUID IID_IMicrosoftAppInstanceStatics = {0x4f414b25, 0x8330, 0x5a9b, {0xbb, 0xc1, 0x82, 0x29, 0xd4, 0x79, 0x64, 0x9d}};
static const GUID IID_IMicrosoftAppInstanceStatics2 = {0xfe9f1885, 0x7160, 0x5397, {0xba, 0x9b, 0x58, 0x90, 0xb2, 0x4f, 0xdc, 0x04}};
static const GUID IID_IMicrosoftAppInstance = {0x75766ae4, 0x0239, 0x5a26, {0xb9, 0xda, 0xd5, 0xbf, 0xc7, 0x5a, 0x48, 0x66}};

static const GUID IID_IWeakReference = {0x00000037, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IWeakReferenceSource = {0x00000038, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IProtocolActivatedEventArgs = {0x6095f4dd, 0xb7c0, 0x46ab, {0x81, 0xfe, 0xd9, 0x0f, 0x36, 0xd0, 0x0d, 0x24}};

typedef HRESULT (STDMETHODCALLTYPE *qi_fn)( const void *iface, REFIID iid, void **out );

/*
 * IWeakReferenceSource for the AppLifecycle objects.  Every WinRT server object
 * answers IWeakReferenceSource: the .NET/WinRT callable wrapper, the C++/WinRT
 * weak_ref helper and the event registration code all QI for it in order to
 * keep a non-owning reference, and callers QI it on the AppInstance object
 * handed out by IMicrosoftAppInstanceStatics::RecommendedInstance.  The objects
 * below are static singletons with a fixed reference count, so their weak
 * reference always resolves back to the owner it was created from.
 */
struct static_weakref
{
    const void *vtbl;
    const void *owner;
    qi_fn qi;
};

struct static_weakref_source
{
    const void *vtbl;
    const struct static_weakref *weakref;
};

static struct static_weakref app_instance_weakref;
static struct static_weakref app_instance_statics_weakref;
static struct static_weakref app_activation_args_weakref;
static struct static_weakref_source app_instance_weakref_source;
static struct static_weakref_source app_instance_statics_weakref_source;
static struct static_weakref_source app_activation_args_weakref_source;

static HRESULT WINAPI static_weakref_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IWeakReference ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI static_weakref_AddRef( void *iface ) { return 2; }
static ULONG WINAPI static_weakref_Release( void *iface ) { return 1; }
static HRESULT WINAPI static_weakref_Resolve( void *iface, REFIID iid, void **out )
{
    struct static_weakref *ref = (struct static_weakref *)iface;

    return ref->qi( ref->owner, iid, out );
}
static const void *static_weakref_vtbl[] =
{
    static_weakref_QueryInterface,
    static_weakref_AddRef,
    static_weakref_Release,
    static_weakref_Resolve,
};

static HRESULT WINAPI static_weakref_source_GetWeakReference( void *iface, void **ref )
{
    struct static_weakref_source *source = (struct static_weakref_source *)iface;

    if (!ref) return E_POINTER;
    *ref = (void *)source->weakref;
    return S_OK;
}

struct app_instance_statics
{
    IActivationFactory IActivationFactory_iface;
    IInspectable IAppInstanceStatics_iface;
    LONG ref;
};

static inline struct app_instance_statics *impl_from_IActivationFactory_app_instance( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct app_instance_statics, IActivationFactory_iface );
}

static inline struct app_instance_statics *impl_from_IAppInstanceStatics( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct app_instance_statics, IAppInstanceStatics_iface );
}

static HRESULT WINAPI app_instance_statics_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    struct app_instance_statics *impl = impl_from_IAppInstanceStatics( iface );
    TRACE( "app_instance_statics QI: %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = &app_instance_statics_weakref_source;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAppInstanceStatics ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstanceStatics ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstanceStatics2 ))
    {
        *out = &impl->IAppInstanceStatics_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    TRACE( "app_instance_statics QI NOT IMPLEMENTED: %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_instance_statics_AddRef( IInspectable *iface )
{
    struct app_instance_statics *impl = impl_from_IAppInstanceStatics( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_instance_statics_Release( IInspectable *iface )
{
    struct app_instance_statics *impl = impl_from_IAppInstanceStatics( iface );
    return InterlockedDecrement( &impl->ref );
}
static HRESULT WINAPI app_instance_statics_GetIids( IInspectable *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI app_instance_statics_GetRuntimeClassName( IInspectable *iface, HSTRING *name )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI app_instance_statics_GetTrustLevel( IInspectable *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }

static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IReferenceTrackerTarget = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};
static const GUID IID_IUwpAppInstance = {0x6756488b, 0xdd4d, 0x4c26, {0xa5, 0x6e, 0xd3, 0xe4, 0xce, 0x07, 0xd8, 0x26}};

static HRESULT WINAPI app_instance_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    TRACE( "app_instance QI: %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = &app_instance_weakref_source;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IUwpAppInstance ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstance ))
    {
        *out = iface;
        return S_OK;
    }
    TRACE( "app_instance QI NOT IMPLEMENTED: %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static const void *app_instance_vtbl[];
struct current_app_instance_impl
{
    const void *vtbl;
    HSTRING key;        /* registered RedirectionKind key, NULL until registered */
    DWORD pid;          /* owning process (primary) pid */
    boolean is_current; /* TRUE for the instance living in this process */
    BOOL heap;          /* secondary-instance face: ref-owned, freeable */
    LONG ref;
};
static struct current_app_instance_impl current_app_instance = { app_instance_vtbl, NULL, 0, TRUE, FALSE, 1 };

static ULONG WINAPI app_instance_AddRef( IInspectable *iface )
{
    struct current_app_instance_impl *impl = (struct current_app_instance_impl *)iface;
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_instance_Release( IInspectable *iface )
{
    struct current_app_instance_impl *impl = (struct current_app_instance_impl *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref && impl->heap)
    {
        WindowsDeleteString( impl->key );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI app_instance_GetIids( IInspectable *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI app_instance_GetRuntimeClassName( IInspectable *iface, HSTRING *n )
{
    return WindowsCreateString( L"Microsoft.Windows.AppLifecycle.AppInstance", 42, n );
}
static HRESULT WINAPI app_instance_GetTrustLevel( IInspectable *iface, TrustLevel *l )
{
    if (l) *l = BaseTrust; return S_OK;
}
static const GUID IID_IAppActivationArguments = {0x14f99eaf, 0x1580, 0x5062, {0xbd, 0xc8, 0xd5, 0xd1, 0xc3, 0x11, 0x38, 0xfb}};

static HRESULT WINAPI app_activation_args_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IReferenceTracker ) ||
        IsEqualGUID( iid, &IID_IReferenceTrackerTarget ))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }

    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = &app_activation_args_weakref_source;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAppActivationArguments ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_activation_args_AddRef( void *iface ) { return 2; }
static ULONG WINAPI app_activation_args_Release( void *iface ) { return 1; }
static HRESULT WINAPI app_activation_args_GetIids( void *iface, ULONG *c, IID **i ) { return E_NOTIMPL; }
static HRESULT WINAPI app_activation_args_GetRuntimeClassName( void *iface, HSTRING *n )
{
    return WindowsCreateString( L"Microsoft.Windows.AppLifecycle.AppActivationArguments", 53, n );
}
static HRESULT WINAPI app_activation_args_GetTrustLevel( void *iface, TrustLevel *l ) { if (l) *l = BaseTrust; return S_OK; }
/* The activation this process was started with: a browser protocol launch
 * passes the shapr3d://... (any scheme://...) URI as a command-line argument,
 * which is what AppInstance.GetCurrent().GetActivatedEventArgs() must report.
 * While an AppInstance.Activated event handler runs, g_current_uri overrides it
 * with the redirected event's URI, matching Windows' "current" semantics. */
static HSTRING g_cmdline_uri;
static BOOL g_cmdline_uri_done;
static HSTRING g_current_uri;

static void app_inst_init_cmdline_uri( void )
{
    WCHAR **argv;
    int argc, i;

    if (g_cmdline_uri_done) return;
    g_cmdline_uri_done = TRUE;
    if ((argv = CommandLineToArgvW( GetCommandLineW(), &argc )))
    {
        for (i = 1; i < argc; i++)
        {
            if (wcsstr( argv[i], L"://" ))
            {
                WindowsCreateString( argv[i], wcslen( argv[i] ), &g_cmdline_uri );
                break;
            }
        }
        LocalFree( argv );
    }
}

const WCHAR *app_activation_current_uri( void )
{
    const WCHAR *raw;

    app_inst_init_cmdline_uri();
    if (!(raw = WindowsGetStringRawBuffer( g_current_uri ? g_current_uri : g_cmdline_uri, NULL ))) return NULL;
    return raw[0] ? raw : NULL;
}

static void app_activation_set_current_uri( const WCHAR *uri )
{
    HSTRING str = NULL;
    if (uri) WindowsCreateString( uri, wcslen( uri ), &str );
    WindowsDeleteString( g_current_uri );
    g_current_uri = str;
}

static HRESULT WINAPI app_activation_args_get_Kind( void *iface, UINT32 *kind )
{
    TRACE( "app_activation_args_get_Kind called\n" );
    if (kind) *kind = app_activation_current_uri() ? 4 /* ExtendedActivationKind_Protocol */
                                                   : 0 /* ExtendedActivationKind_Launch */;
    return S_OK;
}
static HRESULT WINAPI app_activation_args_get_Data( void *iface, void **data )
{
    const WCHAR *uri = app_activation_current_uri();

    TRACE( "app_activation_args_get_Data called, uri %s\n", debugstr_w( uri ) );
    if (!data) return E_POINTER;
    *data = activated_event_args_create_for_uri( uri );
    return S_OK;
}
static const void *app_activation_args_vtbl[] =
{
    app_activation_args_QueryInterface,
    app_activation_args_AddRef,
    app_activation_args_Release,
    app_activation_args_GetIids,
    app_activation_args_GetRuntimeClassName,
    app_activation_args_GetTrustLevel,
    app_activation_args_get_Data,
    app_activation_args_get_Kind,
};
static struct { const void *vtbl; } app_activation_args_obj = { app_activation_args_vtbl };

static HRESULT WINAPI app_instance_UnregisterKey( IInspectable *iface ) { return S_OK; }

/* Completed IAsyncAction handed out by RedirectActivationToAsync: the redirect
 * is done as soon as the primary instance acknowledged the pipe message, so the
 * action is already completed and get_Completed fires the callback inline. */
struct completed_action
{
    __x_ABI_CWindows_CFoundation_CIAsyncAction IAsyncAction_iface;
    LONG ref;
    __x_ABI_CWindows_CFoundation_CIAsyncActionCompletedHandler *handler;
};

static inline struct completed_action *impl_from_IAsyncAction( __x_ABI_CWindows_CFoundation_CIAsyncAction *iface )
{
    return CONTAINING_RECORD( iface, struct completed_action, IAsyncAction_iface );
}

static HRESULT WINAPI completed_action_QueryInterface( __x_ABI_CWindows_CFoundation_CIAsyncAction *iface, REFIID iid, void **out )
{
    struct completed_action *impl = impl_from_IAsyncAction( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAsyncAction ))
    {
        InterlockedIncrement( &impl->ref );
        *out = &impl->IAsyncAction_iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI completed_action_AddRef( __x_ABI_CWindows_CFoundation_CIAsyncAction *iface )
{
    struct completed_action *impl = impl_from_IAsyncAction( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI completed_action_Release( __x_ABI_CWindows_CFoundation_CIAsyncAction *iface )
{
    struct completed_action *impl = impl_from_IAsyncAction( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->handler) __x_ABI_CWindows_CFoundation_CIAsyncActionCompletedHandler_Release( impl->handler );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI completed_action_GetIids( __x_ABI_CWindows_CFoundation_CIAsyncAction *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI completed_action_GetRuntimeClassName( __x_ABI_CWindows_CFoundation_CIAsyncAction *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.AsyncAction", 30, name );
}
static HRESULT WINAPI completed_action_GetTrustLevel( __x_ABI_CWindows_CFoundation_CIAsyncAction *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI completed_action_put_Completed( __x_ABI_CWindows_CFoundation_CIAsyncAction *iface,
        __x_ABI_CWindows_CFoundation_CIAsyncActionCompletedHandler *handler )
{
    struct completed_action *impl = impl_from_IAsyncAction( iface );
    if (impl->handler) __x_ABI_CWindows_CFoundation_CIAsyncActionCompletedHandler_Release( impl->handler );
    if ((impl->handler = handler))
    {
        __x_ABI_CWindows_CFoundation_CIAsyncActionCompletedHandler_AddRef( handler );
        __x_ABI_CWindows_CFoundation_CIAsyncActionCompletedHandler_Invoke( handler, iface, Completed );
    }
    return S_OK;
}
static HRESULT WINAPI completed_action_get_Completed( __x_ABI_CWindows_CFoundation_CIAsyncAction *iface,
        __x_ABI_CWindows_CFoundation_CIAsyncActionCompletedHandler **handler )
{
    struct completed_action *impl = impl_from_IAsyncAction( iface );
    if (!handler) return E_POINTER;
    if (impl->handler) __x_ABI_CWindows_CFoundation_CIAsyncActionCompletedHandler_AddRef( impl->handler );
    *handler = impl->handler;
    return S_OK;
}
static HRESULT WINAPI completed_action_GetResults( __x_ABI_CWindows_CFoundation_CIAsyncAction *iface ) { return S_OK; }
static const struct __x_ABI_CWindows_CFoundation_CIAsyncActionVtbl completed_action_vtbl =
{
    completed_action_QueryInterface,
    completed_action_AddRef,
    completed_action_Release,
    completed_action_GetIids,
    completed_action_GetRuntimeClassName,
    completed_action_GetTrustLevel,
    completed_action_put_Completed,
    completed_action_get_Completed,
    completed_action_GetResults,
    /* widl's IAsyncAction flattens IAsyncInfo at these 9 slots; Cancel and
     * get_Status stay as the trailing methods below the widl view. */
};

static __x_ABI_CWindows_CFoundation_CIAsyncAction *completed_action_create( void )
{
    struct completed_action *action;
    if (!(action = calloc( 1, sizeof(*action) ))) return NULL;
    action->IAsyncAction_iface.lpVtbl = &completed_action_vtbl;
    action->ref = 1;
    return &action->IAsyncAction_iface;
}

/* ---- cross-process single-instancing -------------------------------
 * The primary instance for a key owns a named pipe named after the key.
 * Second instances connect to it: "P\n" asks for the owner pid,
 * "A <uri>\n" redirects a protocol activation, "L\n" a launch activation.
 * The primary's listener thread posts the URI to a message-only window
 * created on the thread that registered, so the Activated handlers run on the
 * primary UI thread (the CoreDispatcher's conversation). */
static const WCHAR app_inst_default_key[] = L"61E9B470-92E6-482B-AFD8-38C1FB464FB5";
static HANDLE app_inst_pipe;
static HWND app_inst_msg_hwnd;
static struct current_app_instance_impl *app_inst_remote;
static CRITICAL_SECTION app_inst_cs;
static INIT_ONCE app_inst_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK app_inst_init_cs( INIT_ONCE *once, void *param, void **context )
{
    InitializeCriticalSection( &app_inst_cs );
    return TRUE;
}

static void app_inst_lock( void )
{
    InitOnceExecuteOnce( &app_inst_once, app_inst_init_cs, NULL, NULL );
    EnterCriticalSection( &app_inst_cs );
}

static void app_inst_unlock( void )
{
    LeaveCriticalSection( &app_inst_cs );
}

static DWORD app_inst_key_hash( const WCHAR *key )
{
    DWORD h = 2166136261;
    while (*key)
    {
        WCHAR c = *key++;
        h = (h ^ (BYTE)c) * 16777619;
        h = (h ^ (BYTE)(c >> 8)) * 16777619;
    }
    return h;
}

static void app_inst_pipe_name( WCHAR *buf, size_t size, const WCHAR *key )
{
    if (!key || !*key) key = app_inst_default_key;
    swprintf( buf, size, L"\\\\.\\pipe\\wine_appinst_%08lx", app_inst_key_hash( key ) );
}

static WCHAR *app_inst_dup_wstr( const WCHAR *s )
{
    size_t len = wcslen( s ) + 1;
    WCHAR *d = calloc( len, sizeof(*d) );
    if (d) memcpy( d, s, len * sizeof(*d) );
    return d;
}

/* Message-mode pipe: one UTF-16 line per request, one reply per request. */
static BOOL app_inst_read_msg( HANDLE h, WCHAR *buf, DWORD max_chars )
{
    DWORD got;

    if (!ReadFile( h, buf, max_chars * sizeof(WCHAR), &got, NULL )) return FALSE;
    buf[got / sizeof(WCHAR)] = 0;
    return got != 0;
}

static BOOL app_inst_write_msg( HANDLE h, const WCHAR *msg )
{
    DWORD len = wcslen( msg ) + 1, sent;

    return WriteFile( h, msg, len * sizeof(WCHAR), &sent, NULL );
}

static HANDLE app_inst_connect( const WCHAR *key )
{
    WCHAR name[64];
    HANDLE h;
    int i;

    app_inst_pipe_name( name, ARRAYSIZE( name ), key );
    for (i = 0; i < 4; i++)
    {
        h = CreateFileW( name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
        if (h != INVALID_HANDLE_VALUE) return h;
        if (GetLastError() != ERROR_PIPE_BUSY) break;
        WaitNamedPipeW( name, 100 );
    }
    return INVALID_HANDLE_VALUE;
}

/* ---- the caller-facing interfaces we consume, as callable faces ----------
 * Microsoft.Windows.AppLifecycle.AppActivationArguments: IInspectable +
 * get_Data (slot 6) + get_Kind (slot 7). */
struct app_activation_args_face
{
    const struct app_activation_args_face_vtbl *lpVtbl;
};
struct app_activation_args_face_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( struct app_activation_args_face *, REFIID, void ** );
    ULONG (STDMETHODCALLTYPE *AddRef)( struct app_activation_args_face * );
    ULONG (STDMETHODCALLTYPE *Release)( struct app_activation_args_face * );
    HRESULT (STDMETHODCALLTYPE *GetIids)( struct app_activation_args_face *, ULONG *, IID ** );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( struct app_activation_args_face *, HSTRING * );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( struct app_activation_args_face *, TrustLevel * );
    HRESULT (STDMETHODCALLTYPE *get_Data)( struct app_activation_args_face *, IInspectable ** );
    HRESULT (STDMETHODCALLTYPE *get_Kind)( struct app_activation_args_face *, UINT32 * );
};

/* Windows.ApplicationModel.Activation.IProtocolActivatedEventArgs:
 * IInspectable + get_Uri (slot 6). */
struct protocol_args_face
{
    const struct protocol_args_face_vtbl *lpVtbl;
};
struct protocol_args_face_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( struct protocol_args_face *, REFIID, void ** );
    ULONG (STDMETHODCALLTYPE *AddRef)( struct protocol_args_face * );
    ULONG (STDMETHODCALLTYPE *Release)( struct protocol_args_face * );
    HRESULT (STDMETHODCALLTYPE *GetIids)( struct protocol_args_face *, ULONG *, IID ** );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( struct protocol_args_face *, HSTRING * );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( struct protocol_args_face *, TrustLevel * );
    HRESULT (STDMETHODCALLTYPE *get_Uri)( struct protocol_args_face *, __x_ABI_CWindows_CFoundation_CIUriRuntimeClass ** );
};

/* AppInstance.Activated handler: IUnknown + Invoke(sender, args). */
struct app_instance_activated_handler
{
    const struct app_instance_activated_handler_vtbl *lpVtbl;
};
struct app_instance_activated_handler_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( struct app_instance_activated_handler *, REFIID, void ** );
    ULONG (STDMETHODCALLTYPE *AddRef)( struct app_instance_activated_handler * );
    ULONG (STDMETHODCALLTYPE *Release)( struct app_instance_activated_handler * );
    HRESULT (STDMETHODCALLTYPE *Invoke)( struct app_instance_activated_handler *, IInspectable *, IInspectable * );
};

/* Extract the redirect payload from AppActivationArguments: returns a heap
 * wide string ("A" protocol uri / "L" launch) owned by the caller. */
static WCHAR *app_inst_args_to_line( void *args )
{
    struct app_activation_args_face *face = (struct app_activation_args_face *)args;
    WCHAR *line = NULL;
    UINT32 kind = 0;
    IInspectable *data = NULL;

    if (FAILED( face->lpVtbl->get_Kind( face, &kind ) )) kind = 0;
    if (kind == 4 && SUCCEEDED( face->lpVtbl->get_Data( face, &data ) ) && data)
    {
        struct protocol_args_face *proto = NULL;

        if (SUCCEEDED( IInspectable_QueryInterface( data, &IID_IProtocolActivatedEventArgs, (void **)&proto ) ) && proto)
        {
            __x_ABI_CWindows_CFoundation_CIUriRuntimeClass *uriobj = NULL;
            HSTRING hstr = NULL;
            const WCHAR *raw = NULL;

            if (SUCCEEDED( proto->lpVtbl->get_Uri( proto, &uriobj ) ) && uriobj)
            {
                if (SUCCEEDED( __x_ABI_CWindows_CFoundation_CIUriRuntimeClass_get_AbsoluteUri( uriobj, &hstr ) ))
                    raw = WindowsGetStringRawBuffer( hstr, NULL );
                if (raw && raw[0])
                {
                    size_t len = wcslen( raw ) + 3;
                    if ((line = calloc( len, sizeof(*line) ))) swprintf( line, len, L"A%ls", raw );
                }
                if (hstr) WindowsDeleteString( hstr );
                __x_ABI_CWindows_CFoundation_CIUriRuntimeClass_Release( uriobj );
            }
            IInspectable_Release( (IInspectable *)proto );
        }
        IInspectable_Release( data );
        if (line) return line;
    }
    else if (data) IInspectable_Release( data );
    return app_inst_dup_wstr( L"L" );
}

static HRESULT WINAPI app_instance_RedirectActivationToAsync( IInspectable *iface, void *args, void **action )
{
    struct current_app_instance_impl *impl = (struct current_app_instance_impl *)iface;
    const WCHAR *key = (const WCHAR *)WindowsGetStringRawBuffer( impl->key, NULL );
    WCHAR *line;
    HANDLE h;

    TRACE( "iface %p, args %p, action %p\n", iface, args, action );
    if (!action) return E_POINTER;

    *action = (void *)completed_action_create();
    if (!*action) return E_OUTOFMEMORY;
    if (impl->is_current) return S_OK; /* redirect to self lands where it is */

    line = args ? app_inst_args_to_line( args ) : NULL;
    if (!line || !wcscmp( line, L"L" ))
    {
        const WCHAR *uri = app_activation_current_uri();
        free( line );
        line = NULL;
        if (uri)
        {
            size_t len = wcslen( uri ) + 3;
            if ((line = calloc( len, sizeof(*line) ))) swprintf( line, len, L"A%ls", uri );
        }
        if (!line && !(line = app_inst_dup_wstr( L"L" ))) return E_OUTOFMEMORY;
    }

    h = app_inst_connect( key );
    if (h != INVALID_HANDLE_VALUE)
    {
        WCHAR reply[16];
        app_inst_write_msg( h, line );
        app_inst_read_msg( h, reply, ARRAYSIZE( reply ) ); /* await primary ack */
        CloseHandle( h );
    }
    else WARN( "no primary instance listening on key %s\n", debugstr_w( key ) );
    free( line );
    return S_OK;
}
static HRESULT WINAPI app_instance_GetActivatedEventArgs( IInspectable *iface, void **result )
{
    TRACE( "app_instance_GetActivatedEventArgs called\n" );
    if (!result) return E_POINTER;
    *result = (void *)&app_activation_args_obj;
    return S_OK;
}
static HRESULT WINAPI app_instance_get_IsCurrent( IInspectable *iface, boolean *value )
{
    struct current_app_instance_impl *impl = (struct current_app_instance_impl *)iface;
    if (value) *value = impl->is_current;
    return S_OK;
}
static HRESULT WINAPI app_instance_get_Key( IInspectable *iface, HSTRING *value )
{
    struct current_app_instance_impl *impl = (struct current_app_instance_impl *)iface;
    if (impl->key) return WindowsDuplicateString( impl->key, value );
    return WindowsCreateString( app_inst_default_key, wcslen( app_inst_default_key ), value );
}
static HRESULT WINAPI app_instance_get_ProcessId( IInspectable *iface, UINT32 *value )
{
    struct current_app_instance_impl *impl = (struct current_app_instance_impl *)iface;
    if (value) *value = impl->pid ? impl->pid : GetCurrentProcessId();
    return S_OK;
}

/* AppInstance.Activated registration.  The AppLifecycle contract: the handler
 * is invoked for activations redirected to this instance; the launch itself is
 * delivered through GetActivatedEventArgs, and the app's own framework
 * (Microsoft.ui.xaml driving Application.OnLaunched over the CoreUI window's
 * conversation) drives the startup activation.  Registration stores the
 * handler; nothing is triggered from here. */
static void *g_activated_handler;

static HRESULT WINAPI app_instance_add_Activated( IInspectable *iface, void *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    if (token) token->value = 1;
    if (handler)
    {
        IUnknown_AddRef( (IUnknown *)handler );
        g_activated_handler = handler;
    }
    return S_OK;
}
static HRESULT WINAPI app_instance_remove_Activated( IInspectable *iface, EventRegistrationToken token )
{
    if (g_activated_handler)
    {
        IUnknown_Release( (IUnknown *)g_activated_handler );
        g_activated_handler = NULL;
    }
    return S_OK;
}

/* Deliver a redirected activation on the primary's UI thread: fire
 * CoreApplicationView::Activated (what XAML's Application.OnLaunched sits on)
 * and AppInstance.Activated (the AppLifecycle event) with the URI set as the
 * process' current activation. */
static void app_inst_fire( const WCHAR *uri )
{
    struct app_instance_activated_handler *app_handler = g_activated_handler;

    TRACE( "firing Activated, uri %s\n", debugstr_w( uri ) );
    app_activation_set_current_uri( uri );

    core_app_fire_view_activated( uri );
    if (app_handler)
        app_handler->lpVtbl->Invoke( app_handler, (IInspectable *)&current_app_instance,
                                     (IInspectable *)&app_activation_args_obj );
}

static LRESULT CALLBACK app_inst_wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_APP + 1)
    {
        WCHAR *uri = (WCHAR *)lp;
        app_inst_fire( uri );
        free( uri );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static void app_inst_ensure_window( void )
{
    static const WCHAR class_name[] = L"WineAppInstanceMessageWindow";
    static ATOM atom;
    WNDCLASSW wc;

    if (app_inst_msg_hwnd) return;
    if (!atom)
    {
        wc.style = 0;
        wc.lpfnWndProc = app_inst_wndproc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = 0;
        wc.hIcon = 0;
        wc.hCursor = 0;
        wc.hbrBackground = 0;
        wc.lpszMenuName = 0;
        wc.lpszClassName = class_name;
        if (!(atom = RegisterClassW( &wc ))) return;
    }
    app_inst_msg_hwnd = CreateWindowExW( 0, class_name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0 );
}

/* Primary side: accept one connection per loop, reply, and hand the payload to
 * the window's thread. Runs for the lifetime of the process. */
static DWORD WINAPI app_inst_listener_thread( void *arg )
{
    WCHAR buf[2048];

    for (;;)
    {
        if (!ConnectNamedPipe( app_inst_pipe, NULL ) && GetLastError() != ERROR_PIPE_CONNECTED) break;
        if (app_inst_read_msg( app_inst_pipe, buf, ARRAYSIZE( buf ) ))
        {
            if (buf[0] == L'P')
            {
                WCHAR reply[16];
                swprintf( reply, ARRAYSIZE( reply ), L"%u", GetCurrentProcessId() );
                app_inst_write_msg( app_inst_pipe, reply );
            }
            else if (buf[0] == L'A')
            {
                app_inst_write_msg( app_inst_pipe, L"OK" );
                if (app_inst_msg_hwnd)
                    PostMessageW( app_inst_msg_hwnd, WM_APP + 1, 0, (LPARAM)app_inst_dup_wstr( buf + 1 ) );
                else
                    app_inst_fire( buf + 1 );
            }
            else if (buf[0] == L'L')
            {
                app_inst_write_msg( app_inst_pipe, L"OK" );
                if (app_inst_msg_hwnd)
                    PostMessageW( app_inst_msg_hwnd, WM_APP + 1, 0, (LPARAM)app_inst_dup_wstr( L"" ) );
                else
                    app_inst_fire( L"" );
            }
            FlushFileBuffers( app_inst_pipe );
        }
        DisconnectNamedPipe( app_inst_pipe );
    }
    return 0;
}

BOOL core_app_try_single_instance_redirect( void )
{
    WCHAR exe[MAX_PATH], name[64], msg[2048], reply[64];
    const WCHAR *uri;
    HANDLE h, thread;
    DWORD i;

    if (app_inst_pipe) return FALSE; /* already registered via AppInstance */
    if (!GetModuleFileNameW( NULL, exe, ARRAYSIZE( exe ) )) return FALSE;
    for (i = 0; exe[i]; i++)
        if (exe[i] >= L'A' && exe[i] <= L'Z') exe[i] += L'a' - L'A';

    if ((h = app_inst_connect( exe )) != INVALID_HANDLE_VALUE)
    {
        BOOL ok;
        uri = app_activation_current_uri();
        swprintf( msg, ARRAYSIZE( msg ), L"A%s", uri ? uri : L"" );
        reply[0] = 0;
        ok = app_inst_write_msg( h, msg ) &&
             app_inst_read_msg( h, reply, ARRAYSIZE( reply ) ) &&
             !wcscmp( reply, L"OK" );
        CloseHandle( h );
        if (ok)
        {
            WARN( "single-instance redirected to primary (key=%s, uri=%s)\n", debugstr_w( exe ), debugstr_w( uri ) );
            return TRUE;
        }
        WARN( "single-instance pipe stale/no-OK (key=%s); continuing as primary\n", debugstr_w( exe ) );
    }

    app_inst_pipe_name( name, ARRAYSIZE( name ), exe );
    app_inst_pipe = CreateNamedPipeW( name, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, NULL );
    if (app_inst_pipe == INVALID_HANDLE_VALUE)
    {
        app_inst_pipe = NULL;
        return FALSE;
    }
    app_inst_ensure_window();
    if ((thread = CreateThread( NULL, 0, app_inst_listener_thread, NULL, 0, NULL )))
        CloseHandle( thread );
    TRACE( "primary single-instance pipe listening on %s\n", debugstr_w( name ) );
    return FALSE;
}

static const void *app_instance_vtbl[] =
{
    /* IInspectable */
    app_instance_QueryInterface,
    app_instance_AddRef,
    app_instance_Release,
    app_instance_GetIids,
    app_instance_GetRuntimeClassName,
    app_instance_GetTrustLevel,
    /* IAppInstance (Microsoft.Windows.AppLifecycle.IAppInstance) */
    app_instance_UnregisterKey,             /* slot 6 */
    app_instance_RedirectActivationToAsync, /* slot 7 */
    app_instance_GetActivatedEventArgs,     /* slot 8 */
    app_instance_add_Activated,             /* slot 9: add_Activated */
    app_instance_remove_Activated,          /* slot 10: remove_Activated */
    app_instance_get_Key,                   /* slot 11: get_Key */
    app_instance_get_IsCurrent,             /* slot 12: get_IsCurrent */
    app_instance_get_ProcessId,             /* slot 13: get_ProcessId */
};


static const GUID IID_IVectorView_AppInstance = {0x9ba4c764, 0xcaaa, 0x6e5c, {0x6c, 0x53, 0xbd, 0xd5, 0x9d, 0x76, 0xd5, 0xc2}};

/* IVectorView<AppInstance> over the single registered instance. */
static HRESULT WINAPI app_instances_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IVectorView_AppInstance ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_instances_AddRef( void *iface ) { return 2; }
static ULONG WINAPI app_instances_Release( void *iface ) { return 1; }
static HRESULT WINAPI app_instances_GetIids( void *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI app_instances_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IVectorView`1<Microsoft.Windows.AppLifecycle.AppInstance>", 82, name );
}
static HRESULT WINAPI app_instances_GetTrustLevel( void *iface, TrustLevel *level )
{
    if (level) *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI app_instances_IndexOf( void *iface, IInspectable *value, UINT32 *index, boolean *found )
{
    if (!index || !found) return E_POINTER;
    *found = (value == (IInspectable *)&current_app_instance);
    *index = 0;
    return S_OK;
}
static HRESULT WINAPI app_instances_GetAt( void *iface, UINT32 index, IInspectable **value )
{
    if (!value) return E_POINTER;
    if (index) return E_BOUNDS;
    *value = (IInspectable *)&current_app_instance;
    return S_OK;
}
static HRESULT WINAPI app_instances_Size( void *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = 1;
    return S_OK;
}
static const void *app_instances_vtbl[] =
{
    app_instances_QueryInterface, app_instances_AddRef, app_instances_Release,
    app_instances_GetIids, app_instances_GetRuntimeClassName, app_instances_GetTrustLevel,
    app_instances_IndexOf, app_instances_GetAt, app_instances_Size,
};
static const void *app_instances_obj = app_instances_vtbl;

static HRESULT WINAPI app_instance_statics_get_RecommendedInstance( IInspectable *iface, IInspectable **value )
{
    TRACE( "app_instance_statics_get_RecommendedInstance called\n" );
    if (!value) return E_POINTER;
    *value = (IInspectable *)&current_app_instance;
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_GetInstances( IInspectable *iface, IInspectable **value )
{
    TRACE( "app_instance_statics_GetInstances called\n" );
    if (!value) return E_POINTER;
    *value = (IInspectable *)&app_instances_obj;
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_FindOrRegisterInstanceForKey( IInspectable *iface, HSTRING key, IInspectable **result )
{
    const WCHAR *keyw = WindowsGetStringRawBuffer( key, NULL );
    WCHAR name[64];
    HANDLE h;
    HRESULT hr = S_OK;

    TRACE( "app_instance_statics_FindOrRegisterInstanceForKey called: key %s\n", debugstr_hstring( key ) );
    if (!result) return E_POINTER;

    app_inst_lock();
    if (app_inst_pipe || app_inst_remote)
    {
        /* key registration is process-wide; repeats hand back the found face */
        *result = app_inst_pipe ? (IInspectable *)&current_app_instance : (IInspectable *)app_inst_remote;
        app_inst_unlock();
        return S_OK;
    }
    if (!keyw || !*keyw) keyw = app_inst_default_key;

    h = app_inst_connect( keyw );
    if (h != INVALID_HANDLE_VALUE)
    {
        /* primary lives in another process: hand out a face for it */
        DWORD pid = 0;
        WCHAR reply[32];
        struct current_app_instance_impl *remote;

        app_inst_write_msg( h, L"P" );
        if (app_inst_read_msg( h, reply, ARRAYSIZE( reply ) )) pid = wcstoul( reply, NULL, 10 );
        CloseHandle( h );

        if ((remote = calloc( 1, sizeof(*remote) )))
        {
            remote->vtbl = app_instance_vtbl;
            remote->is_current = FALSE;
            remote->heap = TRUE;
            remote->ref = 1;
            remote->pid = pid;
            WindowsCreateString( keyw, wcslen( keyw ), &remote->key );
            app_inst_remote = remote;
            *result = (IInspectable *)remote;
        }
        else
        {
            hr = E_OUTOFMEMORY;
        }
    }
    else
    {
        /* we are the primary for this key: own the pipe, listen, and
         * pump Activated events on this (the UI) thread */
        HANDLE thread;

        app_inst_pipe_name( name, ARRAYSIZE( name ), keyw );
        app_inst_pipe = CreateNamedPipeW( name, PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, NULL );
        if (app_inst_pipe == INVALID_HANDLE_VALUE)
        {
            app_inst_pipe = NULL;
            WARN( "failed to create app instance pipe %s, err %lu\n", debugstr_w( name ), GetLastError() );
        }
        else
        {
            app_inst_ensure_window();
            if (!(thread = CreateThread( NULL, 0, app_inst_listener_thread, NULL, 0, NULL )))
                WARN( "failed to create app instance listener, err %lu\n", GetLastError() );
            else CloseHandle( thread );
        }
        WindowsCreateString( keyw, wcslen( keyw ), &current_app_instance.key );
        current_app_instance.pid = GetCurrentProcessId();
        current_app_instance.is_current = TRUE;
        *result = (IInspectable *)&current_app_instance;
    }
    app_inst_unlock();
    return hr;
}
static HRESULT WINAPI app_instance_statics_Unregister( IInspectable *iface )
{
    TRACE( "app_instance_statics_Unregister called\n" );
    return S_OK;
}
static HRESULT WINAPI app_instance_statics_GetActivatedEventArgs( IInspectable *iface, IInspectable **result )
{
    TRACE( "app_instance_statics_GetActivatedEventArgs called\n" );
    if (!result) return E_POINTER;
    *result = (IInspectable *)&app_activation_args_obj;
    return S_OK;
}

static const void *app_instance_statics_vtbl[] =
{
    app_instance_statics_QueryInterface,
    app_instance_statics_AddRef,
    app_instance_statics_Release,
    app_instance_statics_GetIids,
    app_instance_statics_GetRuntimeClassName,
    app_instance_statics_GetTrustLevel,
    app_instance_statics_get_RecommendedInstance,
    app_instance_statics_GetInstances,
    app_instance_statics_FindOrRegisterInstanceForKey,
    app_instance_statics_Unregister,
    app_instance_statics_GetActivatedEventArgs,
};

static HRESULT WINAPI app_instance_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct app_instance_statics *impl = impl_from_IActivationFactory_app_instance( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAppInstanceStatics ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstanceStatics ) ||
        IsEqualGUID( iid, &IID_IMicrosoftAppInstanceStatics2 ))
    {
        *out = &impl->IAppInstanceStatics_iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    TRACE( "app_instance_factory QI NOT IMPLEMENTED: %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI app_instance_factory_AddRef( IActivationFactory *iface )
{
    struct app_instance_statics *impl = impl_from_IActivationFactory_app_instance( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI app_instance_factory_Release( IActivationFactory *iface )
{
    struct app_instance_statics *impl = impl_from_IActivationFactory_app_instance( iface );
    return InterlockedDecrement( &impl->ref );
}
static HRESULT WINAPI app_instance_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI app_instance_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{ FIXME( "stub\n" ); return E_NOTIMPL; }
static HRESULT WINAPI app_instance_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{ if (level) *level = BaseTrust; return S_OK; }
static HRESULT WINAPI app_instance_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl app_instance_factory_vtbl =
{
    app_instance_factory_QueryInterface,
    app_instance_factory_AddRef,
    app_instance_factory_Release,
    app_instance_factory_GetIids,
    app_instance_factory_GetRuntimeClassName,
    app_instance_factory_GetTrustLevel,
    app_instance_factory_ActivateInstance,
};

static struct app_instance_statics app_instance_statics_obj =
{
    {&app_instance_factory_vtbl},
    {(IInspectableVtbl *)app_instance_statics_vtbl},
    1,
};

IActivationFactory *app_instance_factory = &app_instance_statics_obj.IActivationFactory_iface;

/*
 * Weak reference sources: the vtable of a source is the owner's IInspectable
 * vtable with GetWeakReference in the seventh slot, so a caller that QIs the
 * owner for IWeakReferenceSource gets an interface whose QI/AddRef/Release
 * behave exactly like the owner's.
 */
static const void *app_instance_weakref_source_vtbl[] =
{
    app_instance_QueryInterface,
    app_instance_AddRef,
    app_instance_Release,
    static_weakref_source_GetWeakReference,
};

static const void *app_instance_statics_weakref_source_vtbl[] =
{
    app_instance_statics_QueryInterface,
    app_instance_statics_AddRef,
    app_instance_statics_Release,
    static_weakref_source_GetWeakReference,
};

static const void *app_activation_args_weakref_source_vtbl[] =
{
    app_activation_args_QueryInterface,
    app_activation_args_AddRef,
    app_activation_args_Release,
    static_weakref_source_GetWeakReference,
};

static struct static_weakref app_instance_weakref =
{
    static_weakref_vtbl,
    &current_app_instance,
    (qi_fn)app_instance_QueryInterface,
};

static struct static_weakref_source app_instance_weakref_source =
{
    app_instance_weakref_source_vtbl,
    &app_instance_weakref,
};

static struct static_weakref app_instance_statics_weakref =
{
    static_weakref_vtbl,
    &app_instance_statics_obj.IAppInstanceStatics_iface,
    (qi_fn)app_instance_statics_QueryInterface,
};

static struct static_weakref_source app_instance_statics_weakref_source =
{
    app_instance_statics_weakref_source_vtbl,
    &app_instance_statics_weakref,
};

static struct static_weakref app_activation_args_weakref =
{
    static_weakref_vtbl,
    &app_activation_args_obj,
    (qi_fn)app_activation_args_QueryInterface,
};

static struct static_weakref_source app_activation_args_weakref_source =
{
    app_activation_args_weakref_source_vtbl,
    &app_activation_args_weakref,
};

