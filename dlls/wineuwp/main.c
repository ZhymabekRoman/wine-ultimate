/*
 * wineuwp: in-process stand-ins for the Windows shell brokers a packaged (UWP) app talks to.
 *
 * ActivatableApplicationRegistrar: on Windows the app's CoreApplication registers an activation object
 * with the shell's activation manager (out of process), which then calls back Activate() with the launch
 * arguments. Here the registrar lives in the app process and runs the activation on the thread that
 * registered, which is the apartment CoreApplication::Run owns, like the shell's callback does.
 *
 * CoreWindowBroker: CoreApplication::GetWindowFactory CoCreates this CLSID as a local server (Windows
 * hosts it in the shell's process) and asks it for the window factory that makes the view's CoreWindow.
 * PsmBroker: RegisterWithPSM CoCreates this in-process; the process state manager is a service no
 * prefix has.
 *
 * Interface shapes were recovered from twinapi.appcore.dll (CoreApplication::RegisterActivatableApplication,
 * ApplicationActivationFactory::Activate, CoreApplication::GetWindowFactory, RegisterWithPSM) with
 * Microsoft's public symbols; the navigation client and window client adapter shapes come from
 * CoreUIComponents' shipping vtables (see navigation.c and clientadapter.c).
 *
 * The window factory calls windows.ui.dll ordinal 1500 (PrivateCreateCoreWindow) to make the window,
 * which is Wine's builtin, so no Microsoft binary has to be patched to get a view its own CoreWindow.
 *
 * Copyright 2026 the Wine project authors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS
#include "windows.h"
#include "initguid.h"
#include "objbase.h"
#include "roapi.h"
#include "winstring.h"
#include "inspectable.h"
#include "wine/debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "wineuwp.h"

WINE_DEFAULT_DEBUG_CHANNEL(uwp);

/* ---------------------------------------------------------------------------------------------
 * The brokers' class and interface ids
 */
DEFINE_GUID(CLSID_ActivatableApplicationRegistrar, 0xdea794e0, 0x1c1d, 0x4363, 0xb1, 0x71, 0x98, 0xd0, 0xb1, 0x70, 0x35, 0x86);
DEFINE_GUID(IID_IActivatableApplicationRegistrar,  0x036c57fc, 0xe73d, 0x4f3a, 0xb0, 0x95, 0x8b, 0x81, 0x3f, 0xa2, 0xf7, 0x64);
/* Cloaked IActivatableApplication on ApplicationActivationFactory (QI first dword 0x92696c00).
 * IInspectable is the default vtable; its slot 6 is ActivateInstance, not Activate. */
DEFINE_GUID(IID_IActivatableApplication,           0x92696c00, 0x7578, 0x48e1, 0xac, 0x1a, 0x2c, 0xa9, 0x09, 0xe2, 0xc8, 0xcf);
/* Windows.ApplicationModel.Activation */
DEFINE_GUID(IID_IActivatedEventArgs,                0xcf651713, 0xcd08, 0x4fd8, 0xb6, 0x97, 0xa2, 0x81, 0xb6, 0x54, 0x4e, 0x2e);
DEFINE_GUID(IID_IActivatedEventArgsWithUser,        0x1cf09b9e, 0x9962, 0x4936, 0x80, 0xff, 0xaf, 0xc8, 0xe8, 0xae, 0x5c, 0x8c);
DEFINE_GUID(IID_IApplicationViewActivatedEventArgs, 0x930cef4b, 0xb829, 0x40fc, 0x88, 0xf4, 0x85, 0x13, 0xe8, 0xa6, 0x47, 0x38);
DEFINE_GUID(IID_ILaunchActivatedEventArgs,          0xfbc93e26, 0xa14a, 0x4b4f, 0x82, 0xb0, 0x33, 0xbe, 0xd9, 0x20, 0xaf, 0x52);
DEFINE_GUID(IID_ILaunchActivatedEventArgs2,         0x0fd37ebc, 0x9dc9, 0x46b5, 0x9a, 0xce, 0xbd, 0x95, 0xd4, 0x56, 0x53, 0x45);
DEFINE_GUID(IID_IPrelaunchActivatedEventArgs,       0x0c44717b, 0x19f7, 0x48d6, 0xb0, 0x46, 0xcf, 0x22, 0x82, 0x6e, 0xaa, 0x74);
DEFINE_GUID(IID_IViewSwitcherProvider,              0x33f288a6, 0x5c2c, 0x4d27, 0xba, 0xc7, 0x75, 0x36, 0x08, 0x8f, 0x12, 0x19);
DEFINE_GUID(IID_ISplashScreen,                      0xca4d975c, 0xd4d6, 0x43f0, 0x97, 0xc0, 0x08, 0x33, 0xc6, 0x39, 0x1c, 0x24);
/* CoreApplication::GetWindowFactory CoCreates this local-server CLSID (the Windows broker). */
DEFINE_GUID(CLSID_CoreWindowBroker,                 0x3480a401, 0xbde9, 0x4407, 0xbc, 0x02, 0x79, 0x8a, 0x86, 0x6a, 0xc0, 0x51);
/* RegisterWithPSM CoCreates this in-proc after incrementing gPsmRegistered. */
DEFINE_GUID(CLSID_PsmBroker,                        0x02844640, 0xe37c, 0x4322, 0xa3, 0xb8, 0x4c, 0x61, 0xa2, 0xe5, 0x88, 0x79);
DEFINE_GUID(IID_IPsmBroker,                         0x33c255c3, 0x1a86, 0x45fc, 0x94, 0x98, 0xf6, 0x0d, 0x8f, 0x56, 0xb9, 0xd3);
DEFINE_GUID(IID_ICoreWindowBroker,                  0x0f4accb1, 0xd8f9, 0x4011, 0xba, 0x37, 0x25, 0x57, 0x92, 0x5a, 0x78, 0xcf);
DEFINE_GUID(IID_ICoreWindowFactory,                 0xcd292360, 0x2763, 0x4085, 0x8a, 0x9f, 0x74, 0xb2, 0x24, 0xa2, 0x91, 0x75);
DEFINE_GUID(IID_IConfigureWindowFactory,            0x601d51e3, 0x801e, 0x49c9, 0xbb, 0xfa, 0xfe, 0x29, 0xa6, 0x62, 0xae, 0xad);
DEFINE_GUID(IID_ICoreWindow,                        0x79b9d5f2, 0x879e, 0x4b89, 0xb7, 0x98, 0x79, 0xe4, 0x75, 0x98, 0x03, 0x0c);
/* WindowServer's navigation client property and PrepareToActivate, used by navigation.c. */
DEFINE_GUID(IID_ICoreWindowNavigation,              0xbb4cbbe9, 0x65af, 0x4730, 0x9e, 0xd2, 0xab, 0xbd, 0xb5, 0xcc, 0x15, 0xb9);
DEFINE_GUID(IID_INavigationClientPrepareToActivate, 0x075be329, 0x825a, 0x4f2a, 0xa8, 0xaf, 0xcd, 0x2f, 0x9a, 0x81, 0x02, 0xec);
DEFINE_GUID(IID_IAsyncActionStandIn,                0x5a648006, 0x843a, 0x4da9, 0x86, 0x5b, 0x9d, 0x26, 0xe5, 0xdf, 0xad, 0x7b);
DEFINE_GUID(IID_IAsyncInfoStandIn,                  0x00000036, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

/* ---------------------------------------------------------------------------------------------
 * Interface layouts (C; no headers exist for the private ones)
 */
typedef struct IActivatableApplication IActivatableApplication;
struct IActivatableApplicationVtbl
{
    HRESULT (WINAPI *QueryInterface)( IActivatableApplication *, REFIID, void ** );
    ULONG   (WINAPI *AddRef)( IActivatableApplication * );
    ULONG   (WINAPI *Release)( IActivatableApplication * );
    HRESULT (WINAPI *GetIids)( IActivatableApplication *, ULONG *, IID ** );
    HRESULT (WINAPI *GetRuntimeClassName)( IActivatableApplication *, HSTRING * );
    HRESULT (WINAPI *GetTrustLevel)( IActivatableApplication *, TrustLevel * );
    /* ApplicationActivationFactory::Activate(HSTRING, HSTRING, HSTRING, HSTRING, IActivatedEventArgs*, UINT64, UINT32)
     * Four HSTRINGs: aumid, contract, activation-id, extra. Missing the fourth shifts IActivatedEventArgs
     * onto a stack 0, so ActivateForeground never sees launch args and skips GetWindowFactory. */
    HRESULT (WINAPI *Activate)( IActivatableApplication *, HSTRING aumid, HSTRING contract, HSTRING activation_id,
                                HSTRING extra, IInspectable *args, UINT64 cookie, UINT32 options );
};
struct IActivatableApplication { const struct IActivatableApplicationVtbl *lpVtbl; };

/* ---------------------------------------------------------------------------------------------
 * LaunchActivatedEventArgs
 *
 * The shell hands the app a launch activation's arguments on activation; a managed OnLaunched reads
 * them before it looks at anything else. The object is one C object with one vtable per interface it
 * answers, all of them views of the same reference count.
 */
struct launch_args
{
    const void *vtbl_activated;   /* IActivatedEventArgs (IInspectable-derived) */
    const void *vtbl_launch;      /* ILaunchActivatedEventArgs */
    const void *vtbl_launch2;     /* ILaunchActivatedEventArgs2 */
    const void *vtbl_appview;     /* IApplicationViewActivatedEventArgs */
    const void *vtbl_prelaunch;   /* IPrelaunchActivatedEventArgs */
    const void *vtbl_viewswitch;  /* IViewSwitcherProvider */
    const void *vtbl_withuser;    /* IActivatedEventArgsWithUser */
    IUnknown *splash;             /* ISplashScreen, a separate object (see args_get_splash) */
    IUnknown *ftm;
    LONG refcount;
    HSTRING arguments;
    HSTRING tile_id;
};

#define ARGS_FROM(iface, field) ((struct launch_args *)((char *)(iface) - offsetof( struct launch_args, field )))

static HRESULT WINAPI args_QueryInterface( struct launch_args *args, REFIID iid, void **out )
{
    TRACE( "%p, %s, %p\n", args, debugstr_guid( iid ), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivatedEventArgs ))
        *out = &args->vtbl_activated;
    else if (IsEqualGUID( iid, &IID_ILaunchActivatedEventArgs )) *out = &args->vtbl_launch;
    else if (IsEqualGUID( iid, &IID_ILaunchActivatedEventArgs2 )) *out = &args->vtbl_launch2;
    else if (IsEqualGUID( iid, &IID_IApplicationViewActivatedEventArgs )) *out = &args->vtbl_appview;
    else if (IsEqualGUID( iid, &IID_IPrelaunchActivatedEventArgs )) *out = &args->vtbl_prelaunch;
    else if (IsEqualGUID( iid, &IID_IViewSwitcherProvider )) *out = &args->vtbl_viewswitch;
    else if (IsEqualGUID( iid, &IID_IActivatedEventArgsWithUser )) *out = &args->vtbl_withuser;
    else if (IsEqualGUID( iid, &IID_ISplashScreen ))
    {
        /* Not a view of this object: see args_get_splash. */
        IUnknown_AddRef( args->splash );
        *out = args->splash;
        return S_OK;
    }
    /* These args have no thread affinity; being proxied into the app's apartment only turns
     * every QI the app makes into a marshalling failure. */
    else if (IsEqualGUID( iid, &IID_IMarshal ) || IsEqualGUID( iid, &IID_IAgileObject ))
        return IUnknown_QueryInterface( args->ftm, iid, out );
    else
    {
        FIXME( "LaunchActivatedEventArgs: unsupported interface %s\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &args->refcount );
    return S_OK;
}
static ULONG WINAPI args_AddRef( struct launch_args *args ) { return InterlockedIncrement( &args->refcount ); }
static ULONG WINAPI args_Release( struct launch_args *args )
{
    ULONG ref = InterlockedDecrement( &args->refcount );
    if (!ref)
    {
        WindowsDeleteString( args->arguments );
        WindowsDeleteString( args->tile_id );
        if (args->splash) IUnknown_Release( args->splash );
        if (args->ftm) IUnknown_Release( args->ftm );
        free( args );
    }
    return ref;
}
static HRESULT WINAPI args_GetIids( void *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; }
/* The class name is how .NET types the managed wrapper. */
static HRESULT WINAPI args_GetRuntimeClassName( void *iface, HSTRING *name )
{
    static const WCHAR cls[] = L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs";
    return WindowsCreateString( cls, ARRAY_SIZE( cls ) - 1, name );
}
static HRESULT WINAPI args_GetTrustLevel( void *iface, TrustLevel *level ) { *level = BaseTrust; return S_OK; }

#define ARGS_IUNKNOWN(field) \
    static HRESULT WINAPI field##_QI( void *iface, REFIID iid, void **out ) { return args_QueryInterface( ARGS_FROM( iface, field ), iid, out ); } \
    static ULONG WINAPI field##_AddRef( void *iface ) { return args_AddRef( ARGS_FROM( iface, field )); } \
    static ULONG WINAPI field##_Release( void *iface ) { return args_Release( ARGS_FROM( iface, field )); }
ARGS_IUNKNOWN( vtbl_activated ) ARGS_IUNKNOWN( vtbl_launch ) ARGS_IUNKNOWN( vtbl_launch2 ) ARGS_IUNKNOWN( vtbl_appview )
ARGS_IUNKNOWN( vtbl_prelaunch ) ARGS_IUNKNOWN( vtbl_viewswitch ) ARGS_IUNKNOWN( vtbl_withuser )

/* IActivatedEventArgs */
/* ActivationKind: Launch 0, Search 1, ShareTarget 2, File 3, Protocol 4. An app that owns a file
 * type or a scheme branches on this before it looks at the args at all. */
static HRESULT WINAPI activated_get_Kind( void *iface, INT32 *kind )
{
    TRACE( "\n" );
    *kind = 0; /* Launch */
    return S_OK;
}
static HRESULT WINAPI activated_get_PreviousExecutionState( void *iface, INT32 *state )
{
    TRACE( "\n" );
    *state = 0; /* NotRunning */
    return S_OK;
}
/* The args object is its own splash screen: a managed OnLaunched dereferences
 * LaunchActivatedEventArgs.SplashScreen without checking it. */
static HRESULT args_get_splash( struct launch_args *args, void **screen )
{
    TRACE( "%p\n", args );
    if (!screen) return E_POINTER;
    /* A separate object on purpose. The projection resolves an interface pointer to its canonical
     * IUnknown and builds the managed wrapper from that object's IInspectable::GetRuntimeClassName,
     * so a SplashScreen sharing identity with the args would be cast to LaunchActivatedEventArgs
     * and the app's `args.SplashScreen` would throw. */
    IUnknown_AddRef( args->splash );
    *screen = args->splash;
    return S_OK;
}
static HRESULT WINAPI vtbl_activated_get_SplashScreen( void *iface, void **screen )
{
    return args_get_splash( ARGS_FROM( iface, vtbl_activated ), screen );
}
static const void *activated_vtbl[] =
{
    vtbl_activated_QI, vtbl_activated_AddRef, vtbl_activated_Release, args_GetIids, args_GetRuntimeClassName, args_GetTrustLevel,
    activated_get_Kind, activated_get_PreviousExecutionState, vtbl_activated_get_SplashScreen,
};
/* ILaunchActivatedEventArgs */
static HRESULT WINAPI launch_get_Arguments( void *iface, HSTRING *value ) { TRACE( "\n" ); return WindowsDuplicateString( ARGS_FROM( iface, vtbl_launch )->arguments, value ); }
static HRESULT WINAPI launch_get_TileId( void *iface, HSTRING *value ) { TRACE( "\n" ); return WindowsDuplicateString( ARGS_FROM( iface, vtbl_launch )->tile_id, value ); }
/* ILaunchActivatedEventArgs derives from IInspectable, not from IActivatedEventArgs: Kind,
 * PreviousExecutionState and SplashScreen are only on IActivatedEventArgs {cf651713}. Repeating
 * them here put get_Arguments at slot 9, so a caller asking for the launch arguments (slot 6)
 * got get_Kind and an ActivationKind written through its HSTRING *. */
static const void *launch_vtbl[] =
{
    vtbl_launch_QI, vtbl_launch_AddRef, vtbl_launch_Release, args_GetIids, args_GetRuntimeClassName, args_GetTrustLevel,
    launch_get_Arguments, launch_get_TileId,
};
/* ILaunchActivatedEventArgs2 */
static HRESULT WINAPI launch2_get_TileActivatedInfo( void *iface, void **info ) { TRACE( "\n" ); *info = NULL; return S_OK; }
static const void *launch2_vtbl[] =
{
    vtbl_launch2_QI, vtbl_launch2_AddRef, vtbl_launch2_Release, args_GetIids, args_GetRuntimeClassName, args_GetTrustLevel,
    launch2_get_TileActivatedInfo,
};
/* IApplicationViewActivatedEventArgs */
static HRESULT WINAPI appview_get_CurrentlyShownApplicationViewId( void *iface, INT32 *id ) { TRACE( "\n" ); *id = 0; return S_OK; }
static const void *appview_vtbl[] =
{
    vtbl_appview_QI, vtbl_appview_AddRef, vtbl_appview_Release, args_GetIids, args_GetRuntimeClassName, args_GetTrustLevel,
    appview_get_CurrentlyShownApplicationViewId,
};
/* IPrelaunchActivatedEventArgs */
static HRESULT WINAPI prelaunch_get_PrelaunchActivated( void *iface, boolean *value ) { TRACE( "\n" ); *value = FALSE; return S_OK; }
static const void *prelaunch_vtbl[] =
{
    vtbl_prelaunch_QI, vtbl_prelaunch_AddRef, vtbl_prelaunch_Release, args_GetIids, args_GetRuntimeClassName, args_GetTrustLevel,
    prelaunch_get_PrelaunchActivated,
};
/* IViewSwitcherProvider */
static HRESULT WINAPI viewswitch_get_ViewSwitcher( void *iface, void **switcher ) { FIXME( "no view switcher\n" ); *switcher = NULL; return S_OK; }
static const void *viewswitch_vtbl[] =
{
    vtbl_viewswitch_QI, vtbl_viewswitch_AddRef, vtbl_viewswitch_Release, args_GetIids, args_GetRuntimeClassName, args_GetTrustLevel,
    viewswitch_get_ViewSwitcher,
};
/* IActivatedEventArgsWithUser */
/* Windows.System.User: a packaged app reads args.User.NonRoamableId to key its per-user state,
 * so this has to be a real object. Wine has a single local user. */
DEFINE_GUID(IID_IUserStandIn, 0xdf9a26c6, 0xe746, 0x4bcd, 0xb5, 0xd4, 0x12, 0x01, 0x03, 0xc4, 0x20, 0x9b);

struct sys_user
{
    const void **vtbl;
    IUnknown *ftm;
    LONG refcount;
};

static HRESULT WINAPI user_QueryInterface( struct sys_user *user, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IUserStandIn ))
        *out = &user->vtbl;
    else if (IsEqualGUID( iid, &IID_IMarshal ) || IsEqualGUID( iid, &IID_IAgileObject ))
        return IUnknown_QueryInterface( user->ftm, iid, out );
    else
    {
        FIXME( "Windows.System.User: unsupported interface %s\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &user->refcount );
    return S_OK;
}
static ULONG WINAPI user_AddRef( struct sys_user *user ) { return InterlockedIncrement( &user->refcount ); }
static ULONG WINAPI user_Release( struct sys_user *user )
{
    ULONG ref = InterlockedDecrement( &user->refcount );
    if (!ref)
    {
        if (user->ftm) IUnknown_Release( user->ftm );
        free( user );
    }
    return ref;
}
static HRESULT WINAPI user_GetIids( void *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI user_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.System.User", 19, name );
}
static HRESULT WINAPI user_GetTrustLevel( void *iface, TrustLevel *level ) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI user_get_NonRoamableId( void *iface, HSTRING *value )
{
    /* Stable for the lifetime of the prefix; apps only use it as a key. */
    return WindowsCreateString( L"wine-uwp-local-user", 19, value );
}
static HRESULT WINAPI user_get_AuthenticationStatus( void *iface, INT32 *value )
{
    *value = 1 /* LocallyAuthenticated */;
    return S_OK;
}
static HRESULT WINAPI user_get_Type( void *iface, INT32 *value ) { *value = 0 /* LocalUser */; return S_OK; }
static HRESULT WINAPI user_not_impl( void *iface ) { FIXME( "stub\n" ); return E_NOTIMPL; }
static const void *user_vtbl[] =
{
    user_QueryInterface, user_AddRef, user_Release,
    user_GetIids, user_GetRuntimeClassName, user_GetTrustLevel,
    user_get_NonRoamableId, user_get_AuthenticationStatus, user_get_Type,
    user_not_impl, user_not_impl, user_not_impl,
};

static HRESULT WINAPI withuser_get_User( void *iface, void **out )
{
    struct sys_user *user;

    TRACE( "%p\n", out );
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(user = calloc( 1, sizeof(*user) ))) return E_OUTOFMEMORY;
    user->vtbl = user_vtbl;
    user->refcount = 1;
    if (FAILED( CoCreateFreeThreadedMarshaler( (IUnknown *)&user->vtbl, &user->ftm )))
    {
        free( user );
        return E_OUTOFMEMORY;
    }
    *out = &user->vtbl;
    return S_OK;
}
static const void *withuser_vtbl[] =
{
    vtbl_withuser_QI, vtbl_withuser_AddRef, vtbl_withuser_Release, args_GetIids, args_GetRuntimeClassName, args_GetTrustLevel,
    withuser_get_User,
};

/* Windows.Foundation.Rect: four floats, not a RECT. */
struct foundation_rect { FLOAT x, y, width, height; };

/* The hosted CoreWindow, once shown. The view bounds reported to windows.ui and to the navigation
 * client follow its client area, so the window can be resized through the normal frame it is given. */
static HWND hosted_core_window;

/* WINE_UWP_WINDOW_SIZE=WxH overrides the launch size (a window that covers the whole screen is not
 * resizable through the frame, which matters on small test displays). */
static void launch_window_size( int *width, int *height )
{
    WCHAR env[32];
    int w, h;

    if (GetEnvironmentVariableW( L"WINE_UWP_WINDOW_SIZE", env, ARRAY_SIZE( env ) ) &&
        swscanf( env, L"%dx%d", &w, &h ) == 2 && w > 100 && h > 100)
    {
        *width = w;
        *height = h;
        return;
    }
    *width = CORE_WINDOW_WIDTH;
    *height = CORE_WINDOW_HEIGHT;
}

void wineuwp_view_size( FLOAT *width, FLOAT *height )
{
    RECT rc;
    int w, h;

    if (hosted_core_window && GetClientRect( hosted_core_window, &rc ) && rc.right > 0 && rc.bottom > 0)
    {
        *width = (FLOAT)rc.right;
        *height = (FLOAT)rc.bottom;
        return;
    }
    launch_window_size( &w, &h );
    *width = (FLOAT)w;
    *height = (FLOAT)h;
}

HWND wineuwp_hosted_core_window( void )
{
    return hosted_core_window;
}

/* A WinRT delegate is IUnknown + Invoke: three slots then Invoke, not IInspectable's six. */
struct splash_handler;
struct splash_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)( struct splash_handler *, REFIID, void ** );
    ULONG (WINAPI *AddRef)( struct splash_handler * );
    ULONG (WINAPI *Release)( struct splash_handler * );
    HRESULT (WINAPI *Invoke)( struct splash_handler *, void *sender, void *args );
};
struct splash_handler { const struct splash_handler_vtbl *lpVtbl; };

struct splash_screen
{
    const void **vtbl;
    IUnknown *ftm;
    LONG refcount;
    struct splash_handler *dismissed;   /* the app's Dismissed handler, if it registered one */
    LONG fired;
};

static HRESULT WINAPI splash_QueryInterface( struct splash_screen *splash, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_ISplashScreen ))
        *out = &splash->vtbl;
    else if (IsEqualGUID( iid, &IID_IMarshal ) || IsEqualGUID( iid, &IID_IAgileObject ))
        return IUnknown_QueryInterface( splash->ftm, iid, out );
    else
    {
        FIXME( "SplashScreen: unsupported interface %s\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &splash->refcount );
    return S_OK;
}
static ULONG WINAPI splash_AddRef( struct splash_screen *splash ) { return InterlockedIncrement( &splash->refcount ); }
static ULONG WINAPI splash_Release( struct splash_screen *splash )
{
    ULONG ref = InterlockedDecrement( &splash->refcount );
    if (!ref)
    {
        if (splash->dismissed) splash->dismissed->lpVtbl->Release( splash->dismissed );
        if (splash->ftm) IUnknown_Release( splash->ftm );
        free( splash );
    }
    return ref;
}
static HRESULT WINAPI splash_GetIids( void *iface, ULONG *count, IID **iids ) { *count = 0; *iids = NULL; return S_OK; }
static HRESULT WINAPI splash_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Activation.SplashScreen", 47, name );
}
static HRESULT WINAPI splash_GetTrustLevel( void *iface, TrustLevel *level ) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI splash_get_ImageLocation( void *iface, struct foundation_rect *rect )
{
    TRACE( "%p\n", rect );
    if (!rect) return E_POINTER;
    rect->x = rect->y = 0.0f;
    rect->width = CORE_WINDOW_WIDTH;
    rect->height = CORE_WINDOW_HEIGHT;
    return S_OK;
}
/* The extended-splash pattern: an app registers Dismissed and only then sets Window.Content, so a
 * registration that is accepted and never fired leaves the app running with no UI forever.
 *
 * Windows raises Dismissed on a background thread once the splash is taken down, and apps are
 * expected to marshal to the dispatcher themselves, so firing from a worker here matches the real
 * contract. The delay gives the view thread time to finish activating first. */
static DWORD WINAPI splash_dismiss_thread( void *arg )
{
    struct splash_screen *splash = arg;
    struct splash_handler *handler;

    Sleep( 1500 );

    handler = splash->dismissed;
    if (handler && !InterlockedExchange( &splash->fired, 1 ))
    {
        HRESULT hr;
        TRACE( "raising SplashScreen.Dismissed\n" );
        hr = handler->lpVtbl->Invoke( handler, &splash->vtbl, NULL );
        if (FAILED( hr )) WARN( "Dismissed handler returned %#lx\n", hr );
    }
    splash_Release( splash );
    return 0;
}

static HRESULT WINAPI splash_add_Dismissed( void *iface, void *handler, UINT64 *token )
{
    struct splash_screen *splash = iface;
    HANDLE thread;

    TRACE( "%p\n", handler );

    if (splash->dismissed) splash->dismissed->lpVtbl->Release( splash->dismissed );
    if ((splash->dismissed = handler)) splash->dismissed->lpVtbl->AddRef( splash->dismissed );

    if (splash->dismissed)
    {
        InterlockedIncrement( &splash->refcount );
        if ((thread = CreateThread( NULL, 0, splash_dismiss_thread, splash, 0, NULL )))
            CloseHandle( thread );
        else
            splash_Release( splash );
    }
    if (token) *token = 1;
    return S_OK;
}
static HRESULT WINAPI splash_remove_Dismissed( void *iface, UINT64 token )
{
    struct splash_screen *splash = iface;

    if (splash->dismissed) splash->dismissed->lpVtbl->Release( splash->dismissed );
    splash->dismissed = NULL;
    return S_OK;
}
static const void *splash_vtbl[] =
{
    splash_QueryInterface, splash_AddRef, splash_Release,
    splash_GetIids, splash_GetRuntimeClassName, splash_GetTrustLevel,
    splash_get_ImageLocation, splash_add_Dismissed, splash_remove_Dismissed,
};

static IUnknown *create_splash_screen( void )
{
    struct splash_screen *splash;

    if (!(splash = calloc( 1, sizeof(*splash) ))) return NULL;
    splash->vtbl = splash_vtbl;
    splash->refcount = 1;
    if (FAILED( CoCreateFreeThreadedMarshaler( (IUnknown *)&splash->vtbl, &splash->ftm )))
    {
        free( splash );
        return NULL;
    }
    return (IUnknown *)&splash->vtbl;
}

static IInspectable *create_launch_args( const WCHAR *arguments )
{
    struct launch_args *args;

    if (!(args = calloc( 1, sizeof(*args) ))) return NULL;
    args->vtbl_activated = activated_vtbl;
    args->vtbl_launch = launch_vtbl;
    args->vtbl_launch2 = launch2_vtbl;
    args->vtbl_appview = appview_vtbl;
    args->vtbl_prelaunch = prelaunch_vtbl;
    args->vtbl_viewswitch = viewswitch_vtbl;
    args->vtbl_withuser = withuser_vtbl;
    if (!(args->splash = create_splash_screen()))
    {
        free( args );
        return NULL;
    }
    args->refcount = 1;
    if (FAILED( CoCreateFreeThreadedMarshaler( (IUnknown *)&args->vtbl_activated, &args->ftm )))
    {
        free( args );
        return NULL;
    }
    WindowsCreateString( arguments, lstrlenW( arguments ), &args->arguments );
    WindowsCreateString( L"", 0, &args->tile_id );
    return (IInspectable *)&args->vtbl_activated;
}

/* ---------------------------------------------------------------------------------------------
 * The shell shows an activated app's CoreWindow
 *
 * On Windows the shell shows the window: the app itself only calls Window.Current.Activate(), which
 * tells the window server the view is ready. Nothing plays that role here, so show the view's window
 * once activation has created it.
 */
static BOOL CALLBACK show_core_window( HWND hwnd, LPARAM param )
{
    WCHAR class_name[64];
    DWORD pid = 0;

    GetWindowThreadProcessId( hwnd, &pid );
    if (pid == GetCurrentProcessId() &&
        GetClassNameW( hwnd, class_name, ARRAY_SIZE( class_name ) ) &&
        !wcscmp( class_name, L"Windows.UI.Core.CoreWindow" ))
    {
        LONG_PTR style = GetWindowLongPtrW( hwnd, GWL_STYLE );
        RECT rc = { 0, 0, 0, 0 };
        int w, h;

        launch_window_size( &w, &h );
        rc.right = w;
        rc.bottom = h;

        TRACE( "showing core window %p\n", hwnd );
        /* Windows draws no frame around a CoreWindow (the shell does). Here the window manager
         * needs a normal frame to let the user move and resize it. */
        style = (style & ~(WS_POPUP | WS_CHILD)) | WS_OVERLAPPEDWINDOW;
        SetWindowLongPtrW( hwnd, GWL_STYLE, style );
        AdjustWindowRectEx( &rc, style, FALSE, GetWindowLongPtrW( hwnd, GWL_EXSTYLE ) );
        SetWindowPos( hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
        hosted_core_window = hwnd;
        {
            /* the CoreWindow's text is the app model entry id ("App"); the frame deserves a name */
            WCHAR title[128];
            if (GetEnvironmentVariableW( L"WINE_UWP_WINDOW_TITLE", title, ARRAY_SIZE( title ) ) && title[0])
                SetWindowTextW( hwnd, title );
        }
        ShowWindow( hwnd, SW_SHOW );
        SetForegroundWindow( hwnd );

        *(HWND *)param = hwnd;
        return FALSE;
    }
    return TRUE;
}

/* Tell the view's CoreApplicationViewTitleBar how much of its top edge the system uses, as Windows' shell does.
 * twinapi.appcore answers SystemOverlayLeftInset/RightInset/Height from values pushed through
 * ICoreApplicationViewTitleBarInternal::OnLayoutMetricsChanged( left, right, height ) (slot 7), and otherwise asks an
 * ITitleBarClientAdapter we do not have (QI {ba5e7927-...}: refused, as the reference host refuses it),
 * which leaves all three 0. Apps that draw into the title bar lay their right-hand controls out against
 * RightInset. The window manager's frame holds the caption buttons here, so there is no overlay to keep
 * clear of; the right inset is the margin that gives (WINE_UWP_TITLEBAR_RIGHT_INSET, in view pixels).
 * OnLayoutMetricsChanged raises LayoutMetricsChanged, so an app that read the insets before this still
 * gets the values.
 *
 * A builtin twinapi.appcore has no ICoreApplicationView3 and no title bar object, so on that path these
 * QueryInterfaces simply fail and the push is a no-op; the sequence is the packaged-native one. */
static const GUID IID_ICoreApplication_local = { 0x0aacf7a4, 0x5e1d, 0x49df, { 0x80, 0x34, 0xfb, 0x6a, 0x68, 0xbc, 0x5e, 0xd1 }};
static const GUID IID_ICoreApplicationView3_local = { 0x07ebe1b3, 0xa4cf, 0x4550, { 0xab, 0x70, 0xb0, 0x7e, 0x85, 0x33, 0x0b, 0xc8 }};
static const GUID IID_ICoreApplicationViewTitleBarInternal_local = { 0x6e470d39, 0xe79f, 0x453c, { 0x92, 0xef, 0x63, 0x10, 0xed, 0xef, 0xd4, 0x40 }};

static BOOL push_title_bar_metrics( void )
{
    static const WCHAR class_name[] = L"Windows.ApplicationModel.Core.CoreApplication";
    HRESULT (WINAPI **vtbl)( IUnknown *, ... );
    IUnknown *app = NULL, *view = NULL, *view3 = NULL, *title_bar = NULL, *internal = NULL;
    HSTRING_HEADER header;
    double right = 10.0;   /* lines the right edge up with the panels below, which keep 10 px from the window edge */
    WCHAR buf[16];
    HSTRING str;
    HRESULT hr = E_FAIL;
    int step = 0;

    if (GetEnvironmentVariableW( L"WINE_UWP_TITLEBAR_RIGHT_INSET", buf, ARRAY_SIZE( buf ) )) right = wcstod( buf, NULL );
    if (FAILED( WindowsCreateStringReference( class_name, ARRAY_SIZE( class_name ) - 1, &header, &str ) )) return TRUE;
    step++;
    if (FAILED( hr = RoGetActivationFactory( str, &IID_ICoreApplication_local, (void **)&app ) )) goto done;
    vtbl = *(void **)app;
    step++;
    if (FAILED( hr = vtbl[12]( app, &view ) ) || (!view && (hr = E_POINTER))) goto done;   /* GetCurrentView */
    step++;
    if (FAILED( hr = IUnknown_QueryInterface( view, &IID_ICoreApplicationView3_local, (void **)&view3 ) )) goto done;
    vtbl = *(void **)view3;
    step++;
    if (FAILED( hr = vtbl[7]( view3, &title_bar ) ) || (!title_bar && (hr = E_POINTER))) goto done;   /* get_TitleBar */
    step++;
    if (FAILED( hr = IUnknown_QueryInterface( title_bar, &IID_ICoreApplicationViewTitleBarInternal_local, (void **)&internal ) )) goto done;
    step++;
    vtbl = *(void **)internal;
    hr = ((HRESULT (WINAPI *)( IUnknown *, double, double, double ))vtbl[7])( internal, 0.0, right, 32.0 );
done:
    TRACE( "right inset %g, step %d, hr %#lx\n", right, step, hr );
    if (internal) IUnknown_Release( internal );
    if (title_bar) IUnknown_Release( title_bar );
    if (view3) IUnknown_Release( view3 );
    if (view) IUnknown_Release( view );
    if (app) IUnknown_Release( app );
    if (step == 4 && hr == E_POINTER) return FALSE;   /* the view has no title bar yet */
    if (FAILED( hr )) TRACE( "no title bar metrics to set (step %d), hr %#lx\n", step, hr );
    return TRUE;
}

/* The view creates its title bar after its CoreWindow, so keep trying on the view thread for a while. */
static void CALLBACK title_bar_metrics_timer( HWND hwnd, UINT msg, UINT_PTR id, DWORD time )
{
    static unsigned int tries;

    if (push_title_bar_metrics() || ++tries >= 200) KillTimer( NULL, id );
}

static void push_title_bar_metrics_later( void )
{
    /* This is the packaged run's shell: the push walks ICoreApplication -> GetCurrentView ->
     * ICoreApplicationView3 {07ebe1b3} -> get_TitleBar -> ICoreApplicationViewTitleBarInternal
     * {6e470d39} slot 7, all private vtables of Windows' twinapi.appcore. Wine's builtin
     * twinapi.appcore answers none of them, and its objects are laid out differently, so indexing
     * those slots lands on other interfaces and faults. WINE_UWP_TITLEBAR_PUSH=1 turns the push on
     * for a run with the Windows twinapi.appcore in the prefix. */
    if (!GetEnvironmentVariableW( L"WINE_UWP_TITLEBAR_PUSH", NULL, 0 )) return;
    if (!push_title_bar_metrics()) SetTimer( NULL, 0, 25, title_bar_metrics_timer );
}

/* ---------------------------------------------------------------------------------------------
 * ActivatableApplicationRegistrar
 */
struct registrar
{
    const void *vtbl;
    LONG refcount;
    IUnknown *app;
    /* CoreApplication::RegisterActivatableApplication runs on the thread that called
     * CoreApplication::Run, which is the apartment the CoreApplicationView belongs to. The
     * activation is run there, because that is where the view's CoreWindow and DXamlCore are. */
    DWORD owner_tid;
};
static ULONG WINAPI registrar_Release( struct registrar *reg );

static HRESULT do_activate( struct registrar *reg )
{
    IActivatableApplication *app;
    IInspectable *args;
    HSTRING aumid, contract, activation_id, extra;
    WCHAR aumid_buf[512];
    UINT32 len = ARRAY_SIZE( aumid_buf );
    HRESULT hr;

    if (!GetEnvironmentVariableW( L"WINE_UWP_AUMID", aumid_buf, len )) lstrcpyW( aumid_buf, L"App" );
    hr = IUnknown_QueryInterface( reg->app, &IID_IActivatableApplication, (void **)&app );
    if (FAILED( hr ))
    {
        ERR( "app object does not expose IActivatableApplication, hr %#lx\n", hr );
        return hr;
    }
    WindowsCreateString( aumid_buf, lstrlenW( aumid_buf ), &aumid );
    WindowsCreateString( L"Windows.Launch", 14, &contract );
    WindowsCreateString( L"", 0, &activation_id );
    WindowsCreateString( L"", 0, &extra );
    if (!(args = create_launch_args( L"" )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }

    TRACE( "activating %s with contract Windows.Launch on tid %lu\n", debugstr_w( aumid_buf ), GetCurrentThreadId() );
    hr = app->lpVtbl->Activate( app, aumid, contract, activation_id, extra, args, 0, 0 );
    if (FAILED( hr )) ERR( "Activate failed, hr %#lx\n", hr );
    else
    {
        HWND core_window = NULL;

        TRACE( "Activate returned %#lx\n", hr );
        EnumWindows( show_core_window, (LPARAM)&core_window );
        if (!core_window) WARN( "no core window in this process to show\n" );
        /* The rest of the shell's view startup, SetWindowAndGetDispatcher -> Load ->
         * PrepareToActivate -> Activate, runs on the view thread inside the app's Activate: that is
         * the apartment this call arrived in, so it needs no marshalling here. */
        push_title_bar_metrics_later();
    }

    IInspectable_Release( args );
done:
    WindowsDeleteString( aumid );
    WindowsDeleteString( contract );
    WindowsDeleteString( activation_id );
    WindowsDeleteString( extra );
    app->lpVtbl->Release( app );
    return hr;
}

static HRESULT WINAPI registrar_QueryInterface( struct registrar *reg, REFIID iid, void **out )
{
    TRACE( "%p, %s, %p\n", reg, debugstr_guid( iid ), out );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IActivatableApplicationRegistrar ))
    {
        *out = reg;
        InterlockedIncrement( &reg->refcount );
        return S_OK;
    }
    FIXME( "unsupported interface %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI registrar_AddRef( struct registrar *reg ) { return InterlockedIncrement( &reg->refcount ); }
static ULONG WINAPI registrar_Release( struct registrar *reg )
{
    ULONG ref = InterlockedDecrement( &reg->refcount );
    if (!ref)
    {
        if (reg->app) IUnknown_Release( reg->app );
        free( reg );
    }
    return ref;
}

/* slot 3: called by CoreApplication::RegisterActivatableApplication with the app's activation object */
static HRESULT WINAPI registrar_RegisterActivatableApplication( struct registrar *reg, IUnknown *app )
{
    TRACE( "%p, %p\n", reg, app );
    if (!app) return E_INVALIDARG;
    if (reg->app) IUnknown_Release( reg->app );
    IUnknown_AddRef( app );
    reg->app = app;
    reg->owner_tid = GetCurrentThreadId();

    /* The reference host runs this on a worker of its own and marshals the activation onto the view
     * thread with the CoreApplication's dispatcher, because there the registrar is CoCreated by a
     * packaged twinapi.appcore on a thread that is not the view's. Here the registration arrives on
     * the thread CoreApplication::Run owns, so running the activation inline is the marshalled
     * activation: it is the same thread the shell's callback would have been delivered on. */
    if (FAILED( do_activate( reg ) )) TRACE( "activation did not complete\n" );
    return S_OK;
}
static HRESULT WINAPI registrar_unknown_method( struct registrar *reg )
{
    FIXME( "unexpected IActivatableApplicationRegistrar method called\n" );
    return E_NOTIMPL;
}
static const void *registrar_vtbl[] =
{
    registrar_QueryInterface, registrar_AddRef, registrar_Release,
    registrar_RegisterActivatableApplication, registrar_unknown_method, registrar_unknown_method, registrar_unknown_method,
};

/* ---------------------------------------------------------------------------------------------
 * ICoreWindowFactory stand-in (Windows hosts this as a local-server broker)
 *
 * GetWindowFactory CoCreates CLSID_CoreWindowBroker as CLSCTX_LOCAL_SERVER with IID
 * ICoreWindowBroker, QueryService-likes it, then QIs ICoreWindowFactory. CreateCoreWindow
 * calls windows.ui.dll ordinal 1500 (PrivateCreateCoreWindow).
 */
typedef HRESULT (WINAPI *PrivateCreateCoreWindow_t)( DWORD type, const WCHAR *title, INT x, INT y,
        UINT width, UINT height, DWORD flags, HWND parent, const GUID *iid, void **out );

struct window_factory
{
    const void *vtbl_broker;
    const void *vtbl_factory;
    const void *vtbl_configure;
    IUnknown *ftm;
    LONG refcount;
};

static struct window_factory *factory_from_factory( void *iface )
{
    return (struct window_factory *)((char *)iface - offsetof( struct window_factory, vtbl_factory ));
}
static struct window_factory *factory_from_configure( void *iface )
{
    return (struct window_factory *)((char *)iface - offsetof( struct window_factory, vtbl_configure ));
}

static HRESULT WINAPI wf_QueryInterface( struct window_factory *wf, REFIID iid, void **out );

static ULONG WINAPI wf_AddRef( struct window_factory *wf )
{
    return InterlockedIncrement( &wf->refcount );
}

static ULONG WINAPI wf_Release( struct window_factory *wf )
{
    ULONG ref = InterlockedDecrement( &wf->refcount );
    if (!ref)
    {
        if (wf->ftm) IUnknown_Release( wf->ftm );
        free( wf );
    }
    return ref;
}

static HRESULT WINAPI wf_GetIids( struct window_factory *wf, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}
static HRESULT WINAPI wf_GetRuntimeClassName( struct window_factory *wf, HSTRING *name )
{
    return WindowsCreateString( L"Windows.UI.Core.UAPCoreWindowFactory", 37, name );
}
static HRESULT WINAPI wf_GetTrustLevel( struct window_factory *wf, TrustLevel *level )
{
    *level = BaseTrust; return S_OK;
}

static HRESULT WINAPI wf_CreateCoreWindow( struct window_factory *wf, HSTRING title, void **window )
{
    static PrivateCreateCoreWindow_t pcreate;
    const WCHAR *str;
    UINT32 len = 0;
    HWND core_window = NULL;
    int width, height;
    HRESULT hr;

    TRACE( "CreateCoreWindow %s on thread %lu\n", debugstr_hstring( title ), GetCurrentThreadId() );
    if (!window) return E_POINTER;
    *window = NULL;
    if (!pcreate)
    {
        HMODULE ui = LoadLibraryW( L"windows.ui.dll" );
        if (!ui) return HRESULT_FROM_WIN32( GetLastError() );
        pcreate = (PrivateCreateCoreWindow_t)GetProcAddress( ui, (const char *)(ULONG_PTR)1500 );
        if (!pcreate) return E_FAIL;
    }
    str = WindowsGetStringRawBuffer( title, &len );
    if (!str || !len) str = L"App";
    launch_window_size( &width, &height );
    /* WINDOW_TYPE 1 = main UAP window (PrivateCreateCoreWindow switch). */
    hr = pcreate( 1, str, 0, 0, width, height, 0, NULL, &IID_ICoreWindow, window );
    if (FAILED( hr ))
    {
        ERR( "PrivateCreateCoreWindow failed, hr %#lx\n", hr );
        return hr;
    }
    TRACE( "PrivateCreateCoreWindow -> %p hr %#lx\n", *window, hr );
    EnumWindows( show_core_window, (LPARAM)&core_window );
    push_title_bar_metrics_later();
    /* CoreApplicationView::PrepareToActivateAsync fails the whole activation with E_NOTIMPL when the
     * window has no navigation client property, so it has to have one before it is used. */
    if (FAILED( hr = attach_navigation_client( (IUnknown *)*window ) ))
    {
        ERR( "could not attach a navigation client, hr %#lx\n", hr );
        IUnknown_Release( (IUnknown *)*window );
        *window = NULL;
        return hr;
    }
    /* the shell's window client adapter (see clientadapter.c); WINE_UWP_NO_WINDOW_CLIENT_ADAPTER leaves it out */
    if (!GetEnvironmentVariableW( L"WINE_UWP_NO_WINDOW_CLIENT_ADAPTER", NULL, 0 ))
        attach_window_client_adapter( (IUnknown *)*window, core_window );
    return hr;
}

static HRESULT WINAPI wf_get_WindowReuseAllowed( struct window_factory *wf, boolean *value )
{
    TRACE( "%p\n", value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI wf_QueryService( struct window_factory *wf, const GUID *service, const GUID *iid, void **out )
{
    /* the private service GetWindowFactory asks for, and then calls slot 5 on */
    static const GUID SID_window_factory = { 0xd98fd14a, 0x522a, 0x4d59, { 0xb8, 0x75, 0x81, 0x1e, 0x83, 0x91, 0x9a, 0x9e } };

    TRACE( "service %s iid %s\n", debugstr_guid( service ), debugstr_guid( iid ) );
    if (service && IsEqualGUID( service, &SID_window_factory )) return wf_QueryInterface( wf, &IID_IUnknown, out );
    /* Anything else is a broker service this process does not have, e.g. the share UI's
     * IApplicationInvocableFlow {f230b678}: handing back the window factory made twinapi.appcore
     * treat it as one and fault. An ITitleBarClientAdapter {ba5e7927-...} is another: the shell has
     * one, this host does not, and refusing is what the window client adapter answers too. */
    if (out) *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI wf_CreateFromCookie( struct window_factory *wf, UINT64 cookie, void *opts, void **out )
{
    TRACE( "cookie %#I64x opts %p\n", cookie, opts );
    return wf_QueryInterface( wf, &IID_IUnknown, out );
}

static HRESULT WINAPI wf_QueryInterface( struct window_factory *wf, REFIID iid, void **out )
{
    TRACE( "%s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_ICoreWindowBroker ))
        *out = &wf->vtbl_broker;
    else if (IsEqualGUID( iid, &IID_IInspectable ) || IsEqualGUID( iid, &IID_ICoreWindowFactory ))
        *out = &wf->vtbl_factory;
    else if (IsEqualGUID( iid, &IID_IConfigureWindowFactory ))
        *out = &wf->vtbl_configure;
    /* The window factory has no thread affinity of its own and has to make the CoreWindow on the
     * calling (view) thread, so it must never be proxied into another apartment. */
    else if (IsEqualGUID( iid, &IID_IMarshal ) || IsEqualGUID( iid, &IID_IAgileObject ))
        return IUnknown_QueryInterface( wf->ftm, iid, out );
    else
    {
        FIXME( "unsupported %s\n", debugstr_guid( iid ) );
        *out = NULL;
        return E_NOINTERFACE;
    }
    wf_AddRef( wf );
    return S_OK;
}

static const void *wf_broker_vtbl[] =
{
    wf_QueryInterface, wf_AddRef, wf_Release,
    wf_QueryService, wf_GetIids, wf_CreateFromCookie,
};
static HRESULT WINAPI fac_QueryInterface( void *iface, REFIID iid, void **out )
{
    return wf_QueryInterface( factory_from_factory( iface ), iid, out );
}
static ULONG WINAPI fac_AddRef( void *iface )
{
    return wf_AddRef( factory_from_factory( iface ) );
}
static ULONG WINAPI fac_Release( void *iface )
{
    return wf_Release( factory_from_factory( iface ) );
}
static HRESULT WINAPI fac_GetIids( void *iface, ULONG *count, IID **iids )
{
    return wf_GetIids( factory_from_factory( iface ), count, iids );
}
static HRESULT WINAPI fac_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return wf_GetRuntimeClassName( factory_from_factory( iface ), name );
}
static HRESULT WINAPI fac_GetTrustLevel( void *iface, TrustLevel *level )
{
    return wf_GetTrustLevel( factory_from_factory( iface ), level );
}
static HRESULT WINAPI fac_CreateCoreWindow( void *iface, HSTRING title, void **window )
{
    return wf_CreateCoreWindow( factory_from_factory( iface ), title, window );
}
static HRESULT WINAPI fac_get_WindowReuseAllowed( void *iface, boolean *value )
{
    return wf_get_WindowReuseAllowed( factory_from_factory( iface ), value );
}
static const void *wf_factory_vtbl[] =
{
    fac_QueryInterface, fac_AddRef, fac_Release,
    fac_GetIids, fac_GetRuntimeClassName, fac_GetTrustLevel,
    fac_CreateCoreWindow, fac_get_WindowReuseAllowed,
};

static HRESULT WINAPI cfg_QueryInterface( void *iface, REFIID iid, void **out )
{
    return wf_QueryInterface( factory_from_configure( iface ), iid, out );
}
static ULONG WINAPI cfg_AddRef( void *iface )
{
    return wf_AddRef( factory_from_configure( iface ) );
}
static ULONG WINAPI cfg_Release( void *iface )
{
    return wf_Release( factory_from_configure( iface ) );
}
static HRESULT WINAPI cfg_Initialize( void *iface, void *monitor, const WCHAR *title )
{
    TRACE( "monitor %p title %p\n", monitor, title );
    return S_OK;
}
static HRESULT WINAPI cfg_InitializeWithPosition( void *iface, void *monitor, const WCHAR *title, RECT *rect )
{
    TRACE( "monitor %p title %p rect %p\n", monitor, title, rect );
    return S_OK;
}
static HRESULT WINAPI cfg_CreateSplashScreen( void *iface, DWORD flags, void **splash )
{
    TRACE( "flags %#lx\n", flags );
    /* Activate QIs ISplashScreen on the launch args; a NULL here is fine if that QI succeeds. */
    if (splash) *splash = NULL;
    return S_OK;
}
static HRESULT WINAPI cfg_GetAppWindow( void *iface, HWND *hwnd )
{
    TRACE( "\n" );
    if (hwnd) *hwnd = hosted_core_window;
    return S_OK;
}
static HRESULT WINAPI cfg_HasExistingSplashScreen( void *iface, BOOL *has )
{
    if (has) *has = FALSE;
    return S_OK;
}
static HRESULT WINAPI cfg_SetViewId( void *iface, ULONG id )
{
    TRACE( "id %lu\n", id );
    return S_OK;
}
static const void *wf_configure_vtbl[] =
{
    cfg_QueryInterface, cfg_AddRef, cfg_Release,
    cfg_Initialize, cfg_InitializeWithPosition, cfg_CreateSplashScreen,
    cfg_GetAppWindow, cfg_HasExistingSplashScreen, cfg_SetViewId,
};

/* ---------------------------------------------------------------------------------------------
 * PsmBroker: RegisterWithPSM CoCreates this in-process (Windows runs it from psm.dll). Nothing here
 * tracks process state, so every method answers success and the app stops waiting on the service.
 */
struct psm_broker
{
    const void *vtbl;
    LONG refcount;
};

static HRESULT WINAPI psm_QueryInterface( struct psm_broker *psm, REFIID iid, void **out )
{
    TRACE( "%s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IPsmBroker ))
    {
        *out = psm;
        InterlockedIncrement( &psm->refcount );
        return S_OK;
    }
    FIXME( "psm unsupported %s\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI psm_AddRef( struct psm_broker *psm ) { return InterlockedIncrement( &psm->refcount ); }
static ULONG WINAPI psm_Release( struct psm_broker *psm )
{
    ULONG ref = InterlockedDecrement( &psm->refcount );
    if (!ref) free( psm );
    return ref;
}
static HRESULT WINAPI psm_stub( struct psm_broker *psm )
{
    FIXME( "psm broker method stub\n" );
    return S_OK;
}
static const void *psm_vtbl[] =
{
    psm_QueryInterface, psm_AddRef, psm_Release,
    psm_stub, psm_stub, psm_stub, psm_stub, psm_stub, psm_stub, psm_stub, psm_stub,
};

/* ---------------------------------------------------------------------------------------------
 * Class factories / exports
 */
static HRESULT WINAPI factory_QueryInterface( IClassFactory *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IClassFactory ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI factory_AddRef( IClassFactory *iface ) { return 2; }
static ULONG WINAPI factory_Release( IClassFactory *iface ) { return 1; }
static HRESULT WINAPI factory_LockServer( IClassFactory *iface, BOOL lock ) { return S_OK; }

static HRESULT WINAPI registrar_factory_CreateInstance( IClassFactory *iface, IUnknown *outer, REFIID iid, void **out )
{
    struct registrar *reg;
    HRESULT hr;

    TRACE( "%p, %s, %p\n", outer, debugstr_guid( iid ), out );
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(reg = calloc( 1, sizeof(*reg) ))) return E_OUTOFMEMORY;
    reg->vtbl = registrar_vtbl;
    reg->refcount = 1;
    hr = registrar_QueryInterface( reg, iid, out );
    registrar_Release( reg );
    return hr;
}
static const IClassFactoryVtbl registrar_factory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release, registrar_factory_CreateInstance, factory_LockServer,
};
static IClassFactory registrar_factory = { &registrar_factory_vtbl };

static HRESULT WINAPI wffactory_CreateInstance( IClassFactory *iface, IUnknown *outer, REFIID iid, void **out )
{
    struct window_factory *wf;
    HRESULT hr;

    TRACE( "%p, %s, %p\n", outer, debugstr_guid( iid ), out );
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(wf = calloc( 1, sizeof(*wf) ))) return E_OUTOFMEMORY;
    wf->vtbl_broker = wf_broker_vtbl;
    wf->vtbl_factory = wf_factory_vtbl;
    wf->vtbl_configure = wf_configure_vtbl;
    wf->refcount = 1;
    if (FAILED( hr = CoCreateFreeThreadedMarshaler( (IUnknown *)&wf->vtbl_broker, &wf->ftm )))
    {
        free( wf );
        return hr;
    }
    hr = wf_QueryInterface( wf, iid, out );
    wf_Release( wf );
    return hr;
}
static const IClassFactoryVtbl wffactory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release, wffactory_CreateInstance, factory_LockServer,
};
static IClassFactory window_factory_factory = { &wffactory_vtbl };

static HRESULT WINAPI psmfactory_CreateInstance( IClassFactory *iface, IUnknown *outer, REFIID iid, void **out )
{
    struct psm_broker *psm;
    HRESULT hr;

    TRACE( "%p, %s, %p\n", outer, debugstr_guid( iid ), out );
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(psm = calloc( 1, sizeof(*psm) ))) return E_OUTOFMEMORY;
    psm->vtbl = psm_vtbl;
    psm->refcount = 1;
    hr = psm_QueryInterface( psm, iid, out );
    psm_Release( psm );
    return hr;
}
static const IClassFactoryVtbl psmfactory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release, psmfactory_CreateInstance, factory_LockServer,
};
static IClassFactory psm_broker_factory = { &psmfactory_vtbl };

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID iid, void **out )
{
    TRACE( "%s, %s, %p\n", debugstr_guid( clsid ), debugstr_guid( iid ), out );
    if (IsEqualGUID( clsid, &CLSID_ActivatableApplicationRegistrar ))
        return IClassFactory_QueryInterface( &registrar_factory, iid, out );
    if (IsEqualGUID( clsid, &CLSID_CoreWindowBroker ))
        return IClassFactory_QueryInterface( &window_factory_factory, iid, out );
    if (IsEqualGUID( clsid, &CLSID_PsmBroker ))
        return IClassFactory_QueryInterface( &psm_broker_factory, iid, out );
    FIXME( "class %s not provided\n", debugstr_guid( clsid ) );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow( void ) { return S_FALSE; }

/* ---------------------------------------------------------------------------------------------
 * ext-ms-win-sysmain-pfsapi-l1-1-0: SysMain's "offerable heap", which Windows.UI.Xaml's OfferableHeap
 * delay-loads (a failed delay load aborted the process). Signatures from XAML's wrappers: Initialize
 * and Start take the heap and return a Win32 status; Alloc(heap, size) returns the block or NULL;
 * Free/Offer/Reclaim take (heap, block); Reclaim returns 0 when the block's contents survived. Nothing
 * is ever offered to the system here, so every block survives.
 */
DWORD WINAPI PfsOfferableHeapInitialize( void *heap, void *a, void *b, void *c )
{
    TRACE( "%p %p %p %p\n", heap, a, b, c );
    return ERROR_SUCCESS;
}
DWORD WINAPI PfsOfferableHeapStart( void *heap, void *a, void *b, void *c )
{
    TRACE( "%p\n", heap );
    return ERROR_SUCCESS;
}
void * WINAPI PfsOfferableHeapAlloc( void *heap, SIZE_T size, void *a, void *b )
{
    void *block = HeapAlloc( GetProcessHeap(), 0, size );
    TRACE( "%p size %Iu -> %p\n", heap, size, block );
    return block;
}
DWORD WINAPI PfsOfferableHeapFree( void *heap, void *block, void *a, void *b )
{
    TRACE( "%p %p\n", heap, block );
    if (block) HeapFree( GetProcessHeap(), 0, block );
    return ERROR_SUCCESS;
}
DWORD WINAPI PfsOfferableHeapOffer( void *heap, void *block, void *a, void *b )
{
    TRACE( "%p %p\n", heap, block );
    return ERROR_SUCCESS;
}
DWORD WINAPI PfsOfferableHeapReclaim( void *heap, void *block, void *a, void *b )
{
    TRACE( "%p %p\n", heap, block );
    return ERROR_SUCCESS;
}
DWORD WINAPI PfsOfferableHeapCleanup( void *heap, void *a, void *b, void *c )
{
    TRACE( "%p\n", heap );
    return ERROR_SUCCESS;
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls( inst );
    }
    return TRUE;
}
