/*
 * Synthetic local COM server class objects.
 *
 * Some applications activate a local COM server class that, on Windows, is
 * registered in the running class object table by a long lived process which
 * already owns the server's profile (e.g. the WebView2 runtime's extension
 * provider, which the running browser process registers).  When that class
 * object is not present, COM falls back to launching the server from the
 * LocalServer32 registry value; the launch collides with the profile lock of
 * the running instance, the launched process exits right away, and the
 * activation parks the calling async flow for the full activation timeout.
 *
 * For classes listed in the "StubLocalServerClsids" multi-string value under
 * HKLM/HKCU "Software\Wine\Com", hand out a synthetic class factory instead:
 * a plain refcounted object which answers QueryInterface for any interface
 * with itself and returns S_OK from its methods.  This lets the caller's
 * activation complete and its async flow proceed; the real work (e.g. the
 * browser side of an extension install) continues over the transports the
 * process already has.
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
#include <string.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "objbase.h"
#include "comsvcs.h"

#include "combase_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ole);

#define STUB_SLOT_COUNT 32

struct stub_class_list
{
    GUID *clsids;
    DWORD count;
};

static CRITICAL_SECTION stub_cs = { NULL, -1, 0, 0, 0, 0 };
static struct stub_class_list stub_list = { NULL, 0 };
static BOOL stub_list_loaded;

static void stub_list_load( void )
{
    static const WCHAR softwareW[] = L"Software\\Wine\\Com";
    static const WCHAR valueW[] = L"StubLocalServerClsids";
    static const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    unsigned int i, j;

    if (stub_list_loaded)
        return;
    stub_list_loaded = TRUE;

    for (i = 0; i < ARRAY_SIZE( roots ); i++)
    {
        HKEY hkey;
        DWORD type, size = 0;
        WCHAR *data, *p;

        if (RegOpenKeyExW( roots[i], softwareW, 0, KEY_READ, &hkey ))
            continue;
        if (RegQueryValueExW( hkey, valueW, NULL, &type, NULL, &size )
                || type != REG_MULTI_SZ || size <= sizeof(WCHAR))
        {
            RegCloseKey( hkey );
            continue;
        }
        if (!(data = calloc( 1, size )))
        {
            RegCloseKey( hkey );
            continue;
        }
        if (RegQueryValueExW( hkey, valueW, NULL, &type, (BYTE *)data, &size ))
        {
            free( data );
            RegCloseKey( hkey );
            continue;
        }
        RegCloseKey( hkey );

        for (p = data, j = 0; *p; p += wcslen( p ) + 1, j++);
        if (j && stub_list.count + j <= stub_list.count + j) /* no overflow: j is bounded by size */
        {
            GUID *clsids = realloc( stub_list.clsids, (stub_list.count + j) * sizeof(*clsids) );
            if (clsids)
            {
                unsigned int n = stub_list.count;
                for (p = data; *p; p += wcslen( p ) + 1)
                {
                    if (SUCCEEDED( CLSIDFromString( p, &clsids[n] ) ))
                        n++;
                }
                stub_list.clsids = clsids;
                stub_list.count = n;
            }
        }
        free( data );
    }
}

BOOL com_is_stub_local_server( REFCLSID rclsid )
{
    BOOL ret = FALSE;
    DWORD i;

    EnterCriticalSection( &stub_cs );
    stub_list_load();
    for (i = 0; i < stub_list.count; i++)
        if (IsEqualCLSID( rclsid, &stub_list.clsids[i] ))
        {
            ret = TRUE;
            break;
        }
    LeaveCriticalSection( &stub_cs );
    return ret;
}

/*
 * The synthetic server object: a refcounted IUnknown which answers
 * QueryInterface for any interface with itself, and a vtable whose method
 * slots all return S_OK without touching their arguments.
 */

static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
static const GUID IID_IReferenceTrackerTarget = {0x5c13e51c, 0x4f32, 0x4726, {0xa3, 0xfd, 0xf3, 0xed, 0xd6, 0x3d, 0xa3, 0xa0}};

struct stub_server_object
{
    IUnknown IUnknown_u;
    LONG ref;
};

static inline struct stub_server_object *stub_object_from_IUnknown( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct stub_server_object, IUnknown_u );
}

static HRESULT WINAPI stub_server_QueryInterface( IUnknown *iface, REFIID riid, void **obj )
{
    struct stub_server_object *this = stub_object_from_IUnknown( iface );

    TRACE( "%p %s\n", this, debugstr_guid( riid ) );

    if (!obj)
        return E_INVALIDARG;
    *obj = NULL;

    if (IsEqualGUID( riid, &IID_IReferenceTracker ) ||
        IsEqualGUID( riid, &IID_IReferenceTrackerTarget ))
        return E_NOINTERFACE;

    IUnknown_AddRef( iface );
    *obj = iface;
    return S_OK;
}

static ULONG WINAPI stub_server_AddRef( IUnknown *iface )
{
    struct stub_server_object *this = stub_object_from_IUnknown( iface );
    return InterlockedIncrement( &this->ref );
}

static ULONG WINAPI stub_server_Release( IUnknown *iface )
{
    struct stub_server_object *this = stub_object_from_IUnknown( iface );
    ULONG ref = InterlockedDecrement( &this->ref );

    if (!ref)
        free( this );
    return ref;
}

#define DEFINE_STUB_SLOT( n ) \
    static HRESULT WINAPI stub_server_slot_##n( void *iface, ... ) \
    { \
        TRACE( "stub slot %u called\n", n ); \
        return S_OK; \
    }

DEFINE_STUB_SLOT( 0 )
DEFINE_STUB_SLOT( 1 )
DEFINE_STUB_SLOT( 2 )
DEFINE_STUB_SLOT( 3 )
DEFINE_STUB_SLOT( 4 )
DEFINE_STUB_SLOT( 5 )
DEFINE_STUB_SLOT( 6 )
DEFINE_STUB_SLOT( 7 )
DEFINE_STUB_SLOT( 8 )
DEFINE_STUB_SLOT( 9 )
DEFINE_STUB_SLOT( 10 )
DEFINE_STUB_SLOT( 11 )
DEFINE_STUB_SLOT( 12 )
DEFINE_STUB_SLOT( 13 )
DEFINE_STUB_SLOT( 14 )
DEFINE_STUB_SLOT( 15 )
DEFINE_STUB_SLOT( 16 )
DEFINE_STUB_SLOT( 17 )
DEFINE_STUB_SLOT( 18 )
DEFINE_STUB_SLOT( 19 )
DEFINE_STUB_SLOT( 20 )
DEFINE_STUB_SLOT( 21 )
DEFINE_STUB_SLOT( 22 )
DEFINE_STUB_SLOT( 23 )
DEFINE_STUB_SLOT( 24 )
DEFINE_STUB_SLOT( 25 )
DEFINE_STUB_SLOT( 26 )
DEFINE_STUB_SLOT( 27 )
DEFINE_STUB_SLOT( 28 )
DEFINE_STUB_SLOT( 29 )
DEFINE_STUB_SLOT( 30 )
DEFINE_STUB_SLOT( 31 )

static const void *stub_server_vtable[] =
{
    stub_server_QueryInterface,
    stub_server_AddRef,
    stub_server_Release,
    stub_server_slot_0,
    stub_server_slot_1,
    stub_server_slot_2,
    stub_server_slot_3,
    stub_server_slot_4,
    stub_server_slot_5,
    stub_server_slot_6,
    stub_server_slot_7,
    stub_server_slot_8,
    stub_server_slot_9,
    stub_server_slot_10,
    stub_server_slot_11,
    stub_server_slot_12,
    stub_server_slot_13,
    stub_server_slot_14,
    stub_server_slot_15,
    stub_server_slot_16,
    stub_server_slot_17,
    stub_server_slot_18,
    stub_server_slot_19,
    stub_server_slot_20,
    stub_server_slot_21,
    stub_server_slot_22,
    stub_server_slot_23,
    stub_server_slot_24,
    stub_server_slot_25,
    stub_server_slot_26,
    stub_server_slot_27,
    stub_server_slot_28,
    stub_server_slot_29,
    stub_server_slot_30,
    stub_server_slot_31,
};

/*
 * The synthetic class factory handing out the stub server object.
 */

struct stub_class_factory
{
    IClassFactory IClassFactory_u;
    LONG ref;
    CLSID clsid;
};

static inline struct stub_class_factory *stub_factory_from_IClassFactory( IClassFactory *iface )
{
    return CONTAINING_RECORD( iface, struct stub_class_factory, IClassFactory_u );
}

static HRESULT WINAPI stub_factory_QueryInterface( IClassFactory *iface, REFIID riid, void **obj )
{
    struct stub_class_factory *this = stub_factory_from_IClassFactory( iface );

    TRACE( "%p %s %p\n", this, debugstr_guid( riid ), obj );

    if (!obj)
        return E_INVALIDARG;
    *obj = NULL;
    if (IsEqualIID( riid, &IID_IUnknown ) || IsEqualIID( riid, &IID_IClassFactory ))
    {
        IClassFactory_AddRef( iface );
        *obj = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI stub_factory_AddRef( IClassFactory *iface )
{
    struct stub_class_factory *this = stub_factory_from_IClassFactory( iface );
    return InterlockedIncrement( &this->ref );
}

static ULONG WINAPI stub_factory_Release( IClassFactory *iface )
{
    struct stub_class_factory *this = stub_factory_from_IClassFactory( iface );
    ULONG ref = InterlockedDecrement( &this->ref );

    if (!ref)
        free( this );
    return ref;
}

static HRESULT WINAPI stub_factory_CreateInstance( IClassFactory *iface, IUnknown *outer, REFIID riid, void **obj )
{
    struct stub_class_factory *this = stub_factory_from_IClassFactory( iface );
    struct stub_server_object *object;

    TRACE( "%p %p %s %p\n", this, outer, debugstr_guid( riid ), obj );

    if (!obj)
        return E_INVALIDARG;
    *obj = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;

    if (!(object = calloc( 1, sizeof(*object) )))
        return E_OUTOFMEMORY;
    object->ref = 1;
    *(const void **)&object->IUnknown_u.lpVtbl = stub_server_vtable;

    IUnknown_QueryInterface( &object->IUnknown_u, riid, obj );
    IUnknown_Release( &object->IUnknown_u );
    return *obj ? S_OK : E_NOINTERFACE;
}

static HRESULT WINAPI stub_factory_LockServer( IClassFactory *iface, BOOL lock )
{
    return S_OK;
}

static const IClassFactoryVtbl stub_factory_vtable =
{
    stub_factory_QueryInterface,
    stub_factory_AddRef,
    stub_factory_Release,
    stub_factory_CreateInstance,
    stub_factory_LockServer,
};

HRESULT com_get_stub_class_object( REFCLSID rclsid, REFIID riid, void **obj )
{
    struct stub_class_factory *factory;

    TRACE( "%s %p\n", debugstr_guid( rclsid ), obj );

    if (!obj)
        return E_INVALIDARG;
    *obj = NULL;

    if (!(factory = calloc( 1, sizeof(*factory) )))
        return E_OUTOFMEMORY;
    factory->ref = 1;
    factory->clsid = *rclsid;
    factory->IClassFactory_u.lpVtbl = &stub_factory_vtable;

    IClassFactory_QueryInterface( &factory->IClassFactory_u, riid, obj );
    IClassFactory_Release( &factory->IClassFactory_u );
    return *obj ? S_OK : E_NOINTERFACE;
}
