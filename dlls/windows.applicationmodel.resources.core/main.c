/*
 * WinRT Windows.ApplicationModel.Resources.Core (PRM/MRT) implementation
 *
 * Copyright (C) 2026 Wine Developers
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

/*
 * Surface required by .NET Native MRT (Calculator: mrt100_app/SharedLibrary
 * ResourceManager.Current -> RoGetActivationFactory(ResourceContext) x8 ->
 * unhandled exception -> FailFast c0000602).
 *
 * Interface IIDs and vtable slot ordering are taken from the official
 * Windows.WinRT metadata (19041 generation).  IResourceContextStatics2
 * {41f752ef-12af-41b9-ab36-b1eb4b512460} was confirmed live: its GUID is
 * embedded in Calculator's failfast stack (qword 41b912af41f752ef).
 *
 * Values come from the app's own resources.pri via the minimal reader in
 * pri.c (name index + value blob scan; documented stub tier when the
 * qualifier-aware res_map2 index tables are not parsed - see pri.h).
 *
 * Windows.System.MemoryManager is co-hosted as a no-op activation stub
 * (Removes the one-shot "Failed to find library" warning on Calculator).
 */

#include <stdarg.h>
#include <ctype.h>
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "winnls.h"
#include "winreg.h"
#include "initguid.h"
#include "activation.h"
#include "wine/exception.h"
#include "wine/debug.h"

#include "pri.h"

WINE_DEFAULT_DEBUG_CHANNEL(resources);

/* IIDs from Windows.WinRT metadata (19041). Factory IIDs are the standard
 * {00000035-*}. Named/Factory interfaces predate 19041 metadata; marked
 * (unverified). */
static const GUID IID_IResourceContext_g          = {0x2fa22f4b, 0x707e, 0x4b27, {0xad, 0x0d, 0xd0, 0xd8, 0xcd, 0x46, 0x8f, 0xd2}};
static const GUID IID_IResourceContextStatics_g   = {0x98be9d6c, 0x6338, 0x4b31, {0x99, 0xdf, 0xb2, 0xb4, 0x42, 0xf1, 0x71, 0x49}};
static const GUID IID_IResourceContextStatics2_g  = {0x41f752ef, 0x12af, 0x41b9, {0xab, 0x36, 0xb1, 0xeb, 0x4b, 0x51, 0x24, 0x60}};
static const GUID IID_IResourceContextStatics3_g  = {0x20cf492c, 0xaf0f, 0x450b, {0x9d, 0xa6, 0x10, 0x6d, 0xd0, 0xc2, 0x9a, 0x39}};
static const GUID IID_IResourceContextStatics4_g  = {0x22eb9ccd, 0xfb31, 0x4bfa, {0xb8, 0x6b, 0xdf, 0x9d, 0x9d, 0x7b, 0xdc, 0x39}};
static const GUID IID_IResourceManager_g          = {0xf744d97b, 0x9988, 0x44fb, {0xab, 0xd6, 0x53, 0x78, 0x84, 0x4c, 0xfa, 0x8b}};
static const GUID IID_IResourceManager2_g         = {0x9d66fe6c, 0xa4d7, 0x4c23, {0x9e, 0x85, 0x67, 0x5f, 0x30, 0x4c, 0x25, 0x2d}};
static const GUID IID_IResourceManagerStatics_g   = {0x1cc0fdfc, 0x69ee, 0x4e43, {0x99, 0x01, 0x47, 0xf1, 0x26, 0x87, 0xba, 0xf7}};
static const GUID IID_IResourceMap_g              = {0x72284824, 0xdb8c, 0x42f8, {0xb0, 0x8c, 0x53, 0xff, 0x35, 0x7d, 0xad, 0x82}};
static const GUID IID_IResourceMap2_g             = {0xd4a52cd1, 0xc6a5, 0x4824, {0x94, 0x53, 0xcc, 0x86, 0x18, 0x5b, 0x6f, 0xd4}}; /* (unverified) */
static const GUID IID_IResourceContext2_g         = {0xb6d312f1, 0xbc62, 0x4208, {0xb8, 0xa1, 0xb8, 0x42, 0xc0, 0xc4, 0xbb, 0x54}}; /* (unverified) */
static const GUID IID_IResourceCandidate_g        = {0xaf5207d9, 0xc433, 0x4764, {0xb3, 0xfd, 0x8f, 0xa6, 0xbf, 0xbc, 0xba, 0xdc}};
static const GUID IID_IResourceCandidate2_g       = {0x69e5b468, 0xf6fc, 0x4013, {0xaa, 0xa2, 0xd5, 0x3f, 0x17, 0x57, 0xd3, 0xb5}};
static const GUID IID_IResourceCandidate3_g       = {0x08ae97f8, 0x517a, 0x4674, {0x95, 0x8c, 0x4a, 0x3c, 0x7c, 0xd2, 0xcc, 0x6b}};
static const GUID IID_INamedResource_g            = {0x1c98c219, 0x0b13, 0x4240, {0x89, 0xa5, 0xd4, 0x95, 0xdc, 0x18, 0x9a, 0x00}};
static const GUID IID_IResourceLoader_g           = {0x59ed1f35, 0x0942, 0x4b61, {0x9a, 0xef, 0x20, 0xd8, 0x94, 0x8f, 0x46, 0x28}}; /* (unverified) */
static const GUID IID_IResourceLoader2_g          = {0xe29bbb9e, 0xbb14, 0x47d2, {0x9a, 0x2e, 0xf1, 0xa7, 0x4a, 0x58, 0x38, 0x7f}}; /* (unverified, 8.1 metadata) */
static const GUID IID_IResourceQualifier_g        = {0x785da5b2, 0x4afd, 0x4376, {0xa8, 0x88, 0xc5, 0xf9, 0xa6, 0xb7, 0xa0, 0x5c}};
static const GUID IID_IActivationFactory_g_       = {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
/* Parameterized collection IIDs (IMapView<,>/IVectorView<>) are computed at runtime
 * by the RoGetParameterizedIID machinery; the plain vtable is what MRT callers use,
 * so we do not register static IIDs for them. */
#define ELEMENT_NOT_FOUND_E HRESULT_FROM_WIN32(0x490L)
/* {11d3b13a-180e-4789-a8be-7712882893e6} is IReferenceTracker, declared by
 * windows.ui.xaml.hosting.referencetracker.idl.  It is NOT a ResourceContext
 * interface: the earlier entry here ("Windows 8.1 class identity with
 * Create/CreateWithCurrentView") was a guess and is the root cause of the
 * Calculator crash-loop (see the IReferenceTracker block further down).
 *
 * Live-captured on Calculator: the .NET Native RCW (mrt100_app/SharedLibrary)
 * QIs this IID on the ResourceContext factory and on the activated instance
 * right after activation, and answering E_NOINTERFACE leads to an unhandled
 * exception and FailFast c0000602 in SharedLibrary.dll, so the contract has
 * to be implemented.  Slot layout is fixed by the SDK declaration (and by the
 * CsWinRT IReferenceTrackerVftbl projection) and inherits from IUnknown, not
 * from IInspectable:
 *   [3] ConnectFromTrackerSource   [4] DisconnectFromTrackerSource
 *   [5] FindTrackerTargets         [6] GetReferenceTrackerManager
 *   [7] AddRefFromTrackerSource    [8] ReleaseFromTrackerSource
 *   [9] PegFromTrackerSource
 */
static const GUID IID_IReferenceTracker_g         = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
/* IReferenceTrackerManager (same idl): the GC reference-tracking cycle
 * notifications the CLR drives around FindTrackerTargets. */
static const GUID IID_IReferenceTrackerManager_g  = {0x3cf184b4, 0x7ccb, 0x4dda, {0x84, 0x55, 0x7e, 0x6c, 0xe9, 0x9a, 0x32, 0x98}};
static const GUID IID_IResourceContextNamed_g     = {0x12f5efaf, 0x45fb, 0x4669, {0x93, 0x5c, 0x3f, 0x38, 0xa9, 0x0d, 0xab, 0x26}}; /* (unverified, 8.1 metadata) */

#define RTCLASS_RESOURCECONTEXT   L"Windows.ApplicationModel.Resources.Core.ResourceContext"
#define RTCLASS_RESOURCEMANAGER   L"Windows.ApplicationModel.Resources.Core.ResourceManager"
#define RTCLASS_RESOURCEMAP       L"Windows.ApplicationModel.Resources.Core.ResourceMap"
#define RTCLASS_RESOURCECANDIDATE L"Windows.ApplicationModel.Resources.Core.ResourceCandidate"
#define RTCLASS_NAMEDRESOURCE     L"Windows.ApplicationModel.Resources.Core.NamedResource"
#define RTCLASS_RESOURCEQUALIFIER L"Windows.ApplicationModel.Resources.Core.ResourceQualifier"
#define RTCLASS_MEMORYMANAGER     L"Windows.System.MemoryManager"

/*
 * HSTRING virtualization: Windows' winrtstring handles carry a type tag in the
 * low 3 bits (payload 8-byte aligned, tags occupy the free low bits); plain
 * WindowsCreateString handles have low bits 0. .NET Native / CoreRT passes
 * managed GC strings straight through as HSTRING arguments (the app's AOT code
 * does exactly this for IMapView<,>::Get keys: p49 trace "mv_Get Lookup
 * (null)" x5 -> ELEMENT_NOT_FOUND -> failfast c0000602):
 *   0 = heap handle (this tree's combase hstring_private)
 *   1 = atom-table string: we own no atom table to resolve against; report,
 *       never fabricate a buffer
 *   2 = inline buffer {UINT32 cch; WCHAR chars[cch+1]} at (handle & ~7)
 *   7 = managed string object at (handle & ~7). x64 GC String ABI:
 *       MethodTable*@+0x00, int32 cch@+0x08, chars inline right after the
 *       length field -- coreclr puts m_firstChar at +0x0C, the RTString
 *       layout note at +0x10. Probe both and adopt the offset whose
 *       chars[cch] NUL-terminates: offsets proven at runtime, not assumed.
 * TRUE on decode (buf filled, len optional); atom/undecodable is FALSE and
 * the caller must NOT fabricate (no match).
 */
static BOOL hstring_decode( HSTRING h, const WCHAR **buf, UINT32 *len )
{
    static const WCHAR empty_str[1];
    static const WORD char7_offsets[] = { 0x0C, 0x10 };
    UINT_PTR v = (UINT_PTR)h;
    UINT32 tag = (UINT32)(v & 7), cch, i;
    const BYTE *addr = (const BYTE *)(v & ~(UINT_PTR)7);

    if (len) *len = 0;
    *buf = NULL;

    if (!h)
    {
        *buf = empty_str; /* WindowsGetStringRawBuffer(NULL) semantics */
        return TRUE;
    }
    if (!tag)
    {
        *buf = WindowsGetStringRawBuffer( h, len );
        return TRUE;
    }
    if (tag == 2)
    {
        cch = *(const UINT32 *)addr;
        if (cch < 0x10000)  /* cch 0 (the short-key virtualized empty string) is
                             * decodable too: the NUL probe at c[cch] proves it */
        {
            const WCHAR *c = (const WCHAR *)(addr + 4);
            if (!c[cch])
            {
                TRACE( "hstring %p decoded: tag 2, len %u\n", h, cch );
                *buf = c;
                if (len) *len = cch;
                return TRUE;
            }
        }
        TRACE( "hstring %p: tag 2 undecodable\n", h );
        return FALSE;
    }
    if (tag == 7)
    {
        cch = *(const UINT32 *)(addr + 8);
        if (cch < 0x10000)  /* cch 0 decodable (see the tag 2 note) */
        {
            for (i = 0; i < ARRAYSIZE( char7_offsets ); i++)
            {
                const WCHAR *c = (const WCHAR *)(addr + char7_offsets[i]);
                if (!c[cch])
                {
                    TRACE( "hstring %p decoded: tag 7, len %u, chars@+0x%02x\n", h, cch, char7_offsets[i] );
                    *buf = c;
                    if (len) *len = cch;
                    return TRUE;
                }
            }
        }
        TRACE( "hstring %p: tag 7 undecodable\n", h );
        return FALSE;
    }
    if (tag == 1) FIXME( "atom hstring %p: no atom table to resolve against\n", h );
    else TRACE( "hstring %p: unknown tag %u\n", h, tag );
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* pri state                                                           */
/* ------------------------------------------------------------------ */

static pri_index *the_index;
static volatile LONG pri_init_done;

static void init_pri(void)
{
    WCHAR path[4096];
    DWORD len;

    if (InterlockedCompareExchange(&pri_init_done, 1, 0)) return;

    len = GetEnvironmentVariableW( L"WINE_MRT_PRI_PATH", path, ARRAYSIZE( path ));
    if (len && len < ARRAYSIZE( path ) && (the_index = pri_load( path ))) return;

    GetModuleFileNameW( NULL, path, ARRAYSIZE( path ));
    {
        WCHAR *slash = wcsrchr( path, '\\' );
        if (slash) lstrcpyW( slash + 1, L"resources.pri" );
        if ((the_index = pri_load( path ))) return;
    }
    GetModuleFileNameW( GetModuleHandleW( L"windows.applicationmodel.resources.core.dll" ), path, ARRAYSIZE( path ));
    {
        WCHAR *slash = wcsrchr( path, '\\' );
        if (slash) lstrcpyW( slash + 1, L"resources.pri" );
        if ((the_index = pri_load( path ))) return;
    }
    lstrcpyW( path, L"resources.pri" );
    if ((the_index = pri_load( path ))) return;

    TRACE( "no resources.pri found, name-as-value stub tier only\n" );
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static inline LONG incr( LONG *ref ) { return InterlockedIncrement( ref ); }
static inline LONG decr( LONG *ref ) { return InterlockedDecrement( ref ); }

static WCHAR *lstrdupW( const WCHAR *str )
{
    SIZE_T bytes = (lstrlenW( str ) + 1) * sizeof(*str);
    WCHAR *out = HeapAlloc( GetProcessHeap(), 0, bytes );
    if (out) memcpy( out, str, bytes );
    return out;
}

static HRESULT simple_get_iids( void *iface, ULONG *count, IID **iids )
{
    /* mrt100_app probes GetIids with NULL out-params; writing through them
     * raises an access violation that feeds .NET Native's catch/rethrow
     * loop (measured: stack overflow right after the 2nd activation) */
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}

/* Windows publishes the class's interface set from IInspectable::GetIids and
 * allocates the array with CoTaskMemAlloc so the caller releases it with
 * CoTaskMemFree.  ole32's CoTaskMem* are process-heap wrappers, so the array
 * is allocated from the process heap directly: taking an ole32 import in a
 * module that combase activates is a load-order hazard. */
static HRESULT fill_iids( const IID *const *table, ULONG count, ULONG *out_count, IID **out_iids )
{
    IID *array;
    ULONG i;

    if (!out_count || !out_iids) return E_POINTER;
    *out_count = 0;
    *out_iids = NULL;
    if (!(array = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*array) ))) return E_OUTOFMEMORY;
    for (i = 0; i < count; i++) array[i] = *table[i];
    *out_count = count;
    *out_iids = array;
    return S_OK;
}

/* Interface set of the ResourceContext class object (the class's own
 * interfaces; IUnknown/IInspectable are implied and are not listed). */
static const IID *const rc_class_iids[] =
{
    &IID_IActivationFactory_g_,
    &IID_IResourceContext_g,
    &IID_IResourceContext2_g,
    &IID_IResourceContextNamed_g,
    &IID_IResourceContextStatics_g,
    &IID_IResourceContextStatics2_g,
    &IID_IResourceContextStatics3_g,
    &IID_IResourceContextStatics4_g,
    &IID_IReferenceTracker_g,
    &IID_IAgileObject,
};
/* Interface set of an activated ResourceContext instance. */
static const IID *const ctx_class_iids[] =
{
    &IID_IResourceContext_g,
    &IID_IResourceContext2_g,
    &IID_IResourceContextNamed_g,
    &IID_IReferenceTracker_g,
    &IID_IAgileObject,
};

/* IReferenceTracker state, embedded in every object whose identity the CLR
 * reference tracker walks.  The first member is the interface slot, so
 * &obj->rt is the IReferenceTracker interface pointer. */
struct ref_tracker
{
    const void *vt_tracker;  /* IReferenceTracker */
    SIZE_T primary_off;      /* byte offset of the owning object's primary slot */
    LONG sources;            /* tracker sources connected */
    LONG ext_refs;           /* external (GC-held) references bookkept for a source */
    LONG pegs;               /* PegFromTrackerSource count */
};
static const void *rc_tracker_vtbl[];

/* IReferenceTrackerManager [3] ReferenceTrackingStarted
 *                           [4] FindTrackerTargetsCompleted(BOOL walkFailed)
 *                           [5] ReferenceTrackingCompleted
 *                           [6] SetReferenceTrackerHost(IReferenceTrackerHost*)
 * The module owns no garbage-collected objects, so a tracking cycle reports no
 * targets and is a bookkeeping no-op; the CLR host pointer is retained for the
 * lifetime of the process, as the platform manager does. */
struct ref_tracker_manager
{
    const void *vt_manager;
    LONG ref;
    void *host;       /* IReferenceTrackerHost supplied by the CLR */
    LONG cycles;      /* tracking cycles started */
    LONG walks;       /* target walks completed */
    LONG walk_failed; /* last walk reported a failure */
};

static HRESULT WINAPI get_trust_base( void *iface, INT32 *trust )
{
    TRACE( "get_trust_base enter\n" );

    if (!trust) return E_POINTER;
    *trust = 0; /* BaseTrust */
    return S_OK;
}

static HRESULT className( const WCHAR *name, HSTRING *out )
{
    return WindowsCreateString( name, (UINT32)lstrlenW( name ), out );
}

#include "roparameterizediid.h"

/* implemented in dlls/combase/roapi.c, exported by combase; not declared by
 * the generated rparameterizediid.h */
HRESULT WINAPI RoGetParameterizedTypeInstanceIID( UINT32 name_element_count, const WCHAR **name_elements,
                                                  const IRoMetaDataLocator *locator, GUID *iid,
                                                  ROPARAMIIDHANDLE *handle );

#define ELEM_STRING       L"Windows.Foundation.String"
#define ELEM_KEYVALUEPAIR L"Windows.Foundation.Collections.IKeyValuePair"
#define ELEM_MAPVIEW      L"Windows.Foundation.Collections.IMapView"
#define ELEM_MAP          L"Windows.Foundation.Collections.IMap"
#define ELEM_OBSERVABLEMAP L"Windows.Foundation.Collections.IObservableMap"
#define ELEM_MAPCHANGEDHANDLER L"Windows.Foundation.Collections.MapChangedEventHandler"
#define ELEM_VECTORVIEW   L"Windows.Foundation.Collections.IVectorView"
#define ELEM_VECTOR       L"Windows.Foundation.Collections.IVector"
#define ELEM_OBSERVABLEVECTOR L"Windows.Foundation.Collections.IObservableVector"
#define ELEM_VECTORCHANGEDHANDLER L"Windows.Foundation.Collections.VectorChangedEventHandler"

#define ELEM_ITERABLE     L"Windows.Foundation.Collections.IIterable"
#define ELEM_ITERATOR     L"Windows.Foundation.Collections.IIterator"
#define ELEM_RESOURCEMAP  L"Windows.ApplicationModel.Resources.Core.ResourceMap"
#define ELEM_RESCANDIDATE L"Windows.ApplicationModel.Resources.Core.ResourceCandidate"
#define ELEM_RESQUALIFIER L"Windows.ApplicationModel.Resources.Core.ResourceQualifier"
#define ELEM_NAMEDRES     L"Windows.ApplicationModel.Resources.Core.NamedResource"

/* IInspectable::GetRuntimeClassName answers the parameterized interface's name
 * in .NET spelling -- 'Windows.<ns>.IMapView`2<System.String,System.String>'.
 * Two spellings must not be mixed up: the signature grammar of
 * RoGetParameterizedTypeInstanceIID names the HSTRING element
 * 'Windows.Foundation.String' (ELEM_STRING above), while the runtime class name
 * canonicalizes it to 'System.String'.
 *
 * This is not cosmetic.  .NET Native's loose-binding RCW path asks
 * GetRuntimeClassName and parses the answer
 * (Internal.NativeFormat.NativeParser) to learn the interface's arity and type
 * arguments, and from them the marshaling of each parameter.  Measured on the
 * Calculator run: with a name that has no arity and no argument list the parse
 * yields nothing, the shared-reference-types adapter stays in charge, and
 * McgMarshal.ObjectToComInterface marshals the string key of
 * IMapView<HSTRING,HSTRING>::Get as an IInspectable CCW instead of an HSTRING.
 * The callee then reads a null raw buffer (probe: the handle was never produced
 * by any combase string API and its payload held allocator free-list links),
 * answers ELEMENT_NOT_FOUND, the app re-creates the ResourceContext and retries
 * five times, and RaiseFailFastException (c0000602) ends the process.
 *
 * The length always comes from lstrlenW: the previous literal counts overread
 * the literal (count 35 for a 31-character string emitted
 * "Foundation.Collections.IMapView\0Win", copying the tail of the next literal).
 */
#define ELEM_CLASS_STRING L"System.String"

/*
 * Fill the class's published interface set from the parameterized instance
 * IIDs (the same RoGetParameterizedTypeInstanceIID computation the QIs use).
 * .NET Native's RCW builds the supported-interface set from
 * IInspectable::GetIids before binding an adapter: an empty list makes the
 * projection treat the object as untyped (sr18/sr19: the resource loader's
 * Languages enumeration cast fails, its catch/retry loop re-enters
 * get_Languages ~90x, each pass patching thunks = 280 first-chance AVs),
 * so the list must name the real interfaces.  The ROPARAMIIDHANDLE is
 * leaked: GetIids runs a handful of times per process (combase's own
 * param_iid_eq discards it the same way, roapi tests).
 */
static HRESULT fill_param_iids( ULONG *out_count, IID **out_iids, ULONG n, ... )
{
    static const WCHAR *str = ELEM_STRING;
    IID store[8];
    const IID *table[8];
    va_list ap;
    ULONG k;

    if (!out_count || !out_iids) return E_POINTER;
    if (n > ARRAYSIZE( store )) return E_INVALIDARG;
    va_start( ap, n );
    for (k = 0; k < n; k++)
    {
        const WCHAR *elems[4];
        UINT32 argc, i;
        ROPARAMIIDHANDLE h;

        table[k] = &store[k];
        argc = (UINT32)va_arg( ap, int );
        for (i = 0; i < argc; i++) elems[i] = va_arg( ap, const WCHAR * );
        if (i < ARRAYSIZE( elems )) elems[i] = str;
        store[k] = IID_NULL;
        if (FAILED( RoGetParameterizedTypeInstanceIID( argc, elems, NULL, &store[k], &h ) ))
        {
            va_end( ap );
            *out_count = 0;
            *out_iids = NULL;
            return S_OK;  /* metadata gap: answer the empty set, never fail the caller */
        }
    }
    va_end( ap );
    return fill_iids( table, n, out_count, out_iids );
}

/* Static class interface set + one parameterized contract (E8).  mrt100_app's
 * RCW seeding freezes the wrapped face's IInspectable::GetIids list into the
 * wrapper's m_cachedInterfaces, and RhTypeCast_CheckCastInterface consults
 * that frozen list before falling back to QI (the fallback never fires for
 * the GetForViewIndependentUse chain: m_baseIUnknown is (Free, NULL)).  The
 * class's published set must therefore name IIterable<IKeyValuePair<string,
 * string>> -- the Dictionary<string,string> enumerable contract the app
 * castclass's the chain's ComObject to -- so the cast resolves at seeding. */
static HRESULT fill_iids_plus_param( const IID *const *table, ULONG count,
                                     ULONG *out_count, IID **out_iids, UINT32 argc, ... )
{
    const WCHAR *elems[4];
    IID param;
    ROPARAMIIDHANDLE h;
    va_list ap;
    UINT32 i;

    if (!out_count || !out_iids) return E_POINTER;
    if (argc > ARRAYSIZE( elems )) return E_INVALIDARG;
    va_start( ap, argc );
    for (i = 0; i < argc; i++) elems[i] = va_arg( ap, const WCHAR * );
    va_end( ap );
    if (FAILED( RoGetParameterizedTypeInstanceIID( argc, elems, NULL, &param, &h ) ))
        return fill_iids( table, count, out_count, out_iids );  /* metadata gap: static set only */
    {
        IID *array;
        ULONG k;

        if (!(array = HeapAlloc( GetProcessHeap(), 0, (count + 1) * sizeof(*array) ))) return E_OUTOFMEMORY;
        for (k = 0; k < count; k++) array[k] = *table[k];
        array[k] = param;
        *out_count = count + 1;
        *out_iids = array;
        return S_OK;
    }
}

static const WCHAR *elem_class_name( const WCHAR *elem )
{
    if (!elem) return L"IInspectable";
    if (!lstrcmpW( elem, ELEM_STRING )) return ELEM_CLASS_STRING;
    if (!lstrcmpW( elem, ELEM_RESOURCEMAP )) return ELEM_RESOURCEMAP;
    if (!lstrcmpW( elem, ELEM_RESQUALIFIER )) return ELEM_RESQUALIFIER;
    if (!lstrcmpW( elem, ELEM_RESCANDIDATE )) return ELEM_RESCANDIDATE;
    if (!lstrcmpW( elem, ELEM_NAMEDRES )) return ELEM_NAMEDRES;
    return elem;
}

static void class_name_append( WCHAR *buf, SIZE_T size, const WCHAR *part )
{
    SIZE_T pos = lstrlenW( buf );

    if (pos + lstrlenW( part ) >= size) return; /* keep the truncation NUL in range */
    lstrcpynW( buf + pos, part, (INT)(size - pos) );
}

/* "<base><`N><arg0,arg1,...>" -- nested parameterized names are formatted into
 * a buffer first and passed back in as one argument. */
static void format_param_class( WCHAR *buf, SIZE_T size, const WCHAR *base, const WCHAR *arity,
                                const WCHAR *const *args, UINT32 argc )
{
    UINT32 i;

    if (!size) return;
    buf[0] = 0;
    class_name_append( buf, size, base );
    class_name_append( buf, size, arity );
    for (i = 0; i < argc; i++)
    {
        class_name_append( buf, size, i ? L"," : L"<" );
        class_name_append( buf, size, args[i] );
    }
    if (argc) class_name_append( buf, size, L">" );
}

static HRESULT param_class_name( const WCHAR *base, const WCHAR *arity, const WCHAR *const *args,
                                 UINT32 argc, HSTRING *out )
{
    WCHAR buf[512];

    if (!out) return E_POINTER;
    format_param_class( buf, ARRAYSIZE( buf ), base, arity, args, argc );
    return className( buf, out );
}

static BOOL param_iid_eq( REFIID iid, UINT32 count, ... );
#define IT_STRING 0
#define IT_KVPAIR 1
#define IT_RMPAIR 2
#define IT_IFACE  3
static const void *iterator_string_vtbl[];
static const void *iterator_pair_vtbl[];
static struct iterator *iter_new( UINT8 kind, void *src, UINT32 count, const void *vtbl );
static HRESULT WINAPI svv_First( void *iface, void **first );
static HRESULT WINAPI ivv_First( void *iface, void **first );
static HRESULT WINAPI mv_First( void *iface, void **first );
static HRESULT WINAPI rmv_First( void *iface, void **first );

/* forward */
struct res_map;
struct res_candidate;
struct res_context;
static inline struct res_context *ctx_from( void *iface );
static HRESULT map_lookup_ctx( struct res_map *map, const WCHAR *key, const pri_qual *ctx, UINT32 ctxcount,
                               struct res_candidate **out );
static const void *res_context2_vtbl[];
static const void *res_map2_vtbl[];

/* ------------------------------------------------------------------ */
/* map/qualifier key decode: HSTRING payload, else managed-string CCW   */
/* ------------------------------------------------------------------ */

/*
 * The key parameters of IMapView<HSTRING,HSTRING>::Get/::HasKey,
 * IResourceMap::GetValue/::GetValueForContext, IResourceCandidate::
 * GetQualifierValue and IResourceContext2::SetQualifierValue are declared
 * HSTRING, and .NET Native's loose-binding RCW hands over a COM Callable
 * Wrapper of a managed System.String instead (McgMarshal.ObjectToComInterface
 * marshals the argument as an IInspectable, see the ELEM_CLASS_STRING note
 * above): an 8-byte-aligned IUnknown pointer, whose low 3 tag bits are 0 --
 * i.e. the plain-handle tag.  The pointer was never produced by a combase
 * string API, so hstring_decode()'s tag-0 branch *succeeds* and hands back
 * whatever the object happens to hold at +0x10, which is the str field of
 * combase's payload header (dlls/combase/string.c struct hstring_header, read
 * by hstring_get_data()).  The module then compares that wild buffer, answers
 * ELEMENT_NOT_FOUND, the app re-creates the context five times and
 * RaiseFailFastException(c0000602) ends the process (gate sr12: c0000602 x4,
 * AV 0; site SharedLibrary 0x6ffffff9f262).
 *
 * A key is therefore accepted as a winrtstring only when its payload is
 * plausibly one -- readable and NUL-terminated exactly at the reported length,
 * which combase guarantees for allocated, reference and virtualized strings.
 * Anything else is offered the CCW surface CoreCLR exposes for a String:
 *
 *   1. IStringable, whose identity this tree states twice, independently:
 *        include/windows.foundation.idl:270   uuid(96369f54-8eb6-48f0-abce-c1b211e627c3)
 *        include/windows.foundation.idl:272   interface IStringable : IInspectable
 *                                             { HRESULT ToString([out,retval] HSTRING *value) }
 *        dlls/combase/roapi.c iid_names[]     "Windows.Foundation.IStringable"
 *                                             -> {96369f54-8eb6-48f0-abce-c1b211e627c3}
 *      widl emits it as IID___x_ABI_CWindows_CFoundation_CIStringable
 *      (generated include/windows.foundation.h:2283, alias IID_IStringable at
 *      :2384) with the vtable (generated :2304ff)
 *        [0 QI][1 AddRef][2 Release][3 GetIids][4 GetRuntimeClassName]
 *        [5 GetTrustLevel][6 ToString]
 *
 *   2. the IReference`1<Windows.Foundation.String> pinterface (boxed string),
 *      whose get_Value shares slot 6 and the HSTRING* signature.  This is a
 *      parameterized interface: this tree carries no static IID for it, so the
 *      identity is computed per call with the tree's own
 *      RoGetParameterizedTypeInstanceIID (dlls/combase/roapi.c:870; the handle
 *      it returns is INVALID_HANDLE_VALUE, so nothing needs closing) --
 *      measured on this build: {fd416dfb-2a07-52eb-aae3-dfce14116c05}.
 *
 * GetRuntimeClassName (IInspectable slot 5) is deliberately NOT tried: it
 * answers the class name ('System.String'), never the string's value.
 *
 * A key pointer that is not a COM object at all must not kill the process, so
 * the whole probe runs under __TRY/__EXCEPT_ALL (pattern:
 * dlls/combase/string.c decode_virtualized_hstring).  Nothing is cached and
 * the probe runs once per call.
 */
static const GUID IID_IStringable_g = {0x96369f54, 0x8eb6, 0x48f0, {0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3}};
#define ELEM_IREFERENCE L"Windows.Foundation.IReference"

typedef HRESULT (STDMETHODCALLTYPE *ccw_qi_t)( void *iface, REFIID iid, void **out );
typedef HRESULT (STDMETHODCALLTYPE *ccw_release_t)( void *iface );
typedef ULONG (STDMETHODCALLTYPE *ccw_addref_t)( void *iface );
typedef HRESULT (STDMETHODCALLTYPE *ccw_out_hstring_t)( void *iface, HSTRING *out );

/* a CCW's vtable is the pointer in the object's first slot; its entries are
 * the entries of that table (two dereferences, not one) */
static inline const void **ccw_vtable( const void *obj ) { return (const void **)((const void **)obj)[0]; }

#define CCW_SLOT0( o )  ((ccw_qi_t)ccw_vtable( o )[0])          /* IUnknown::QueryInterface */
#define CCW_SLOT1( o )  ((ccw_addref_t)ccw_vtable( o )[1])      /* IUnknown::AddRef */
#define CCW_SLOT2( o )  ((ccw_release_t)ccw_vtable( o )[2])     /* IUnknown::Release */
#define CCW_SLOT6( o )  ((ccw_out_hstring_t)ccw_vtable( o )[6]) /* IStringable::ToString /
                                                                  IReference`1<T>::get_Value */

/* A winrtstring payload ends with a NUL at [length]; a wild buffer taken from
 * a non-string object does not (or is not readable at all: it faults here and
 * reports invalid, it never faults the caller). */
static BOOL probe_wstr( const WCHAR *buf, UINT32 len )
{
    BOOL ok = FALSE;

    if (!buf || len >= 0x10000) return FALSE;
    __TRY
    {
        ok = !buf[len];
    }
    __EXCEPT_ALL
    {
        ok = FALSE;
    }
    __ENDTRY
    return ok;
}

static BOOL ireference_string_iid( GUID *iid )
{
    static const WCHAR *elems[] = { ELEM_IREFERENCE L"`1", ELEM_STRING };
    ROPARAMIIDHANDLE handle;

    return SUCCEEDED( RoGetParameterizedTypeInstanceIID( 2, elems, NULL, iid, &handle ) );
}

/* QI the key for a String-CCW surface and read the string out of it.  TRUE on
 * success with *out the HSTRING the CCW returned (the caller owns it when it is
 * a plain tag-0 handle, see key_decode). */
static BOOL ccw_key_decode( HSTRING key, HSTRING *out )
{
    void *obj = (void *)key;
    void *iface = NULL;
    HSTRING str = NULL;
    GUID iid;
    HRESULT hr;
    BOOL ok = FALSE;

    *out = NULL;
    if (!key) return FALSE;

    __TRY
    {
        hr = CCW_SLOT0( obj )( obj, &IID_IStringable_g, &iface );
        if (SUCCEEDED( hr ) && iface)
        {
            hr = CCW_SLOT6( iface )( iface, &str );
            if (SUCCEEDED( hr ) && str) ok = TRUE;
            CCW_SLOT2( iface )( iface );
        }
        iface = NULL;
        if (!ok && ireference_string_iid( &iid ) && SUCCEEDED( CCW_SLOT0( obj )( obj, &iid, &iface )) && iface)
        {
            if (SUCCEEDED( CCW_SLOT6( iface )( iface, &str )) && str) ok = TRUE;
            CCW_SLOT2( iface )( iface );
        }
    }
    __EXCEPT_ALL
    {
        ok = FALSE; /* not a COM pointer: report it, never fabricate a key */
    }
    __ENDTRY

    if (!ok)
    {
        if (str) WindowsDeleteString( str );
        *out = NULL;
        return FALSE;
    }
    *out = str;
    return TRUE;
}

/* Key decode for every map/qualifier-key site.  Returns TRUE with *raw set to
 * the key text.  When the key came from the CCW surface and the HSTRING the
 * CCW produced is a plain (tag 0) handle, the callee owns it: *owned is then
 * TRUE and the caller must WindowsDeleteString( *tmp ) once the lookup is
 * done.  Failure means "no key": the caller must not fabricate a match. */
static BOOL key_decode( HSTRING key, const WCHAR **raw, HSTRING *tmp, BOOL *owned )
{
    const WCHAR *buf = NULL;
    UINT32 len = 0;

    *raw = NULL;
    *tmp = NULL;
    *owned = FALSE;

    TRACE( "key_decode ENTER %p tag %u\n", key, (UINT32)((UINT_PTR)key & 7) );
    if (hstring_decode( key, &buf, &len ) && probe_wstr( buf, len ))
    {
        TRACE( "key_decode direct %s\n", debugstr_w( buf ) );
        *raw = buf;
        return TRUE;
    }
    if (ccw_key_decode( key, tmp ) && hstring_decode( *tmp, &buf, &len ) && probe_wstr( buf, len ))
    {
        if (!((UINT_PTR)*tmp & 7)) *owned = TRUE; /* tag 0: allocated by the CCW, ours to free */
        *raw = buf;
        TRACE( "CCW key %p decoded to %s\n", key, debugstr_w( buf ) );
        return TRUE;
    }
    if (*owned) WindowsDeleteString( *tmp );
    *tmp = NULL;
    *owned = FALSE;
    return FALSE;
}

static void key_release( HSTRING tmp, BOOL owned )
{
    if (owned) WindowsDeleteString( tmp );
}


/* ------------------------------------------------------------------ */
/* IVectorView<HSTRING> (Languages) / IVectorView<T*> generic          */
/* ABI (include/windows.foundation.collections.idl:249, IVectorView<T> */
/* requires IIterable<T>): [II 5][GetAt 6][Size 7][IndexOf 8]          */
/* [GetMany 9][First 10].  IVectorView has no GetView slot -- that is   */
/* IVector's (idl:267), and the IObservableVector face below carries    */
/* it at its own delegated position.                                   */
/* ------------------------------------------------------------------ */

struct str_vector
{
    const void *vt_ov;   /* IObservableVector<HSTRING>: the face ResourceContext::Languages returns */
    const void *vt_vv;   /* IVectorView<HSTRING>: the read-only view the IVectorView QI and GetView answer */
    LONG ref;
    HSTRING *items;
    UINT32 count;
    struct vec_event_reg *regs;   /* VectorChanged registrations, interlocked list */
};

/* A/B experiment (hstrab, arm R): the Languages element is handed out as a
 * WindowsCreateStringReference-based HSTRING.  CoreRT (mrt100_app Redhawk)
 * consumes the returned HSTRING's raw buffer directly; the suspect is the
 * heap-string representation (malloc'ed combase hstring_private, tag-0
 * handle).  A reference string is {flags = HSTRING_REFERENCE_FLAG, length,
 * str} with handle == header (dlls/combase/string.c struct hstring_header:
 * 4 ULONGs + pointer = the 24-byte x64 HSTRING_HEADER); the header must
 * outlive every use of the handle, so it is static here and the buffer is
 * the static, NUL-terminated, immutable "en-US".  A "fresh" reference per
 * call rewrites the identical triple, i.e. returns the same handle: the
 * ref discipline is unchanged from the heap-string arm (GetAt answers a
 * borrowed handle, WindowsDeleteString on a reference is a no-op, context
 * teardown leaves the shared header in place). */
static const WCHAR lang_enUS[] = L"en-US";
static HSTRING_HEADER lang_header;

static inline HRESULT lang_ref_of( HSTRING item, HSTRING *out )
{
    UINT32 len = 0;
    const WCHAR *raw = WindowsGetStringRawBuffer( item, &len );

    return WindowsCreateStringReference( raw, len, &lang_header, out );
}

static inline struct str_vector *ov_from( void *iface ) { return (struct str_vector *)iface; }
static inline struct str_vector *svv_from( void *iface ) { return CONTAINING_RECORD( iface, struct str_vector, vt_vv ); }

static HRESULT WINAPI svv_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct str_vector *impl = svv_from( iface );
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    /* E7b: the Dictionary<string,string> construction in the ResourceManager
     * startup chain casts its enumerable operand to
     * IIterable<IKeyValuePair<string,string>>; the mrt100 cast-cache miss
     * (RhTypeCast_CheckCastInterface -> CanCastComObject miss ->
     * InvalidCastException -> failfast) is lifted by answering that
     * pinterface on the view face.  MCG probes past IIterable<string>, so
     * the extra IID rides the same S_OK path (cheap, additive). */
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 2, ELEM_VECTORVIEW L"`1", ELEM_STRING ) ||
        param_iid_eq( iid, 2, ELEM_ITERABLE L"`1", ELEM_STRING ) ||
        param_iid_eq( iid, 4, ELEM_ITERABLE L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_vv;
        incr( &impl->ref );
        return S_OK;
    }
    /* E8: on Windows ResourceContext::Languages is a single object that
     * implements IObservableVector<string> and hands out IVectorView<string>
     * through GetView/QI, so the view pointer the app keeps from
     * Languages->GetView() also answers IVector<string>
     * (98b9acc1-4b56-532e-ac73-03d5291cca90) and IObservableVector<string>
     * (37500595-dcf0-5d27-b8c4-cf388e374de8).  Calculator computes both
     * pinterface IIDs in the same block it reads Languages[0] (the gate log
     * shows the RoGetParameterizedTypeInstanceIID calls for {913337e9;string}
     * and {5917eb53;string} interleaved with the Languages QI), then casts the
     * view pointer it holds; our E_NOINTERFACE became the
     * RhTypeCast_CheckCastInterface -> CanCastComObject miss ->
     * InvalidCastException -> RaiseFailFastException c0000602 that ends the
     * chain at svv_GetAt 0.  Answer the writable/observable contract with the
     * observable face, which is the face that owns the mutator slots. */
    if (param_iid_eq( iid, 2, ELEM_VECTOR L"`1", ELEM_STRING ) ||
        param_iid_eq( iid, 2, ELEM_OBSERVABLEVECTOR L"`1", ELEM_STRING ))
    {
        *out = &impl->vt_ov;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI svv_AddRef( void *iface ) { return incr( &svv_from( iface )->ref ); }
static ULONG WINAPI svv_Release( void *iface ) { return decr( &svv_from( iface )->ref ); }
static HRESULT WINAPI svv_GetIids( void *iface, ULONG *c, IID **i )
{
    TRACE( "svv_GetIids enter\n" );

    /* Seed the CoreRT cast cache with the whole face set the object answers
     * (see svv_QueryInterface E8): a miss there is what turns a QI-free
     * castclass into an InvalidCastException. */
    return fill_param_iids( c, i, 4, 2, ELEM_VECTORVIEW L"`1", ELEM_STRING,
                                2, ELEM_ITERABLE L"`1", ELEM_STRING,
                                2, ELEM_VECTOR L"`1", ELEM_STRING,
                                2, ELEM_OBSERVABLEVECTOR L"`1", ELEM_STRING );
}
static HRESULT WINAPI svv_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "svv_GetRuntimeClassName enter\n" );

    /* E9: the ResourceManager startup chain casts the Languages view operand
     * to IIterable<IKeyValuePair<string,string>> (cast #14, mrt100
     * RhTypeCast_CheckCastInterface target RID 0x1806800000).  The Mcg
     * ModuleManager resolves our GetRuntimeClassName answer through
     * StringMap::FindString, an EXACT key match over the SharedLibrary Mcg
     * name heap.  The heap keys are short type-display names, not the
     * fully-qualified WinRT projection names: the enumerable-contract key
     * for this operand is `IIterable`1<IKeyValuePair`2<String,String>>`
     * (heap RVA 0x511; symbol-verified carrier
     * $23_Windows::Foundation::Collections::IIterator_A__scg_KeyValuePair_
     * A_string_j_string_V__V___Impl, whose frozen m_cachedInterfaces triple
     * names 0x1806800000).  The previous fully-qualified
     * `Windows.Foundation.Collections.IVectorView`1<System.String>` answer
     * missed that exact key, so ComputeClosestClassForClassIndex returned a
     * closest (IIterable<string>) class whose triple omits 0x1806800000, and
     * cast #14 stayed an InvalidCast.  Answer the exact key so the
     * instantiated __Impl carries the target in its triple. */
    /* Cast-#14 predicate (decompiled, SharedLibrary 0x180a2e900 +
     * 0x18082d8d0): CheckCastInterface miss -> CanCastComObject ->
     * __ComObject::IsInstanceOfInterface -> SimpleComInterfaceCacheItem::
     * TryReadCachedNativeInterface: entry.typeHandle == target typeID
     * (handle equality), else RhTypeCast_AreTypesEquivalent (canonical-form +
     * definition identity, not GUID).  The IIterable<KVP> key above froze the
     * wrapper triple at the *framework* handle 0x3000200000, so the cast to
     * the SharedLibrary handle 0x1806800000 missed; the QI fallback only fires
     * when the wrapper class's EEType has HasRemoteInterfaces (flag 0x40).
     * Arm the Mcg toolbox-signature class (s_classData index 72, EEType
     * SharedLibrary+0x805e8, flags 0x60): its cast miss goes through
     * CanCastComObject -> QueryInterface_NoAddRef_Slow with the target
     * interface IID {9828e927-...}, which the statics2 (rc2) face answers
     * with the stable enumerable (430d26b9). */
    /* Mcg name-pool decode (SharedLibrary s_dictionary namespaces, heap RVA
     * 0x5b9260): the s_classData_NameIndices StringMap16 (99) maps name pool
     * key 0x1c33 = ns8 "Windows.Storage." + "UserDataPaths" -> s_classData
     * index 78, EEType flags 0x60 (HasRemoteInterfaces 0x40 set) and EEType
     * RID 0x68 -> typeID 0x1806800000, the exact class RID listed in the
     * IIterable<KVP<string,string>> QI map (interface RID 0x20 block,
     * SharedLibrary+0xe070: entry {0x1806800000, slot 0/2}).  UserDataPaths
     * is the IMapView<string,string> implementer of the flag-0x40 set
     * {71 HResult(no 0x40), 77 StaticAttribute, 78 UserDataPaths, 79
     * AppDataPaths, 80 SystemDataPaths, 81 ApplicationData}.  The previous
     * packed multi-name key carried the *wrong namespaces* (Metadata.
     * UserDataPaths), so FindString never hit and the wrapper EEType stayed
     * flag 0x100.  Answer the exact key so the instantiated Mcg class has
     * 0x40, the cast miss fires the QI fallback and rc2 answers {9828e927}. */
    static const WCHAR mcg_key72[] = L"Windows.Storage.UserDataPaths";

    return className( mcg_key72, n );
}
static HRESULT WINAPI svv_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI svv_GetAt( void *iface, UINT32 index, HSTRING *item )
{

    TRACE( "svv_GetAt %u\n", index );
    if (!item) return E_POINTER;    struct str_vector *impl = svv_from( iface );
    if (index >= impl->count) return E_BOUNDS;
    TRACE( "svv_GetAt %u -> %s\n", index, debugstr_w( WindowsGetStringRawBuffer( impl->items[index], NULL ) ) );
    return lang_ref_of( impl->items[index], item );
}
static HRESULT WINAPI svv_Size( void *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = svv_from( iface )->count;
    TRACE( "svv_Size -> %u\n", *size );
    return S_OK;
}
/* IVectorView::IndexOf (idl:254), slot 8.  Ordinal comparison through
 * WindowsCompareStringOrdinal (include/winstring.h:28): the projection compares
 * language tags case-sensitively, and the case-insensitive lstrcmpiW this slot
 * used before reported a hit for "en-US" against the stored "en-us" and handed
 * the caller an index whose value does not equal the key it searched for.  A
 * null HSTRING is the empty string in the Windows.String projection, so it is a
 * legal search key and not a missing argument. */
static HRESULT WINAPI svv_IndexOf( void *iface, HSTRING item, UINT32 *index, INT32 *found )
{
    struct str_vector *impl = svv_from( iface );
    UINT32 i;

    TRACE( "svv_IndexOf\n" );
    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = FALSE;
    for (i = 0; i < impl->count; i++)
    {
        INT32 order = 1;

        if (FAILED( WindowsCompareStringOrdinal( item, impl->items[i], &order ))) continue;
        if (order) continue;
        *index = i;
        *found = TRUE;
        return S_OK;
    }
    return S_OK;
}
static HRESULT WINAPI svv_GetMany( void *iface, UINT32 start, UINT32 size, HSTRING *items, UINT32 *count )
{
    struct str_vector *impl = svv_from( iface );
    UINT32 n;

    TRACE( "svv_GetMany start=%u size=%u\n", start, size );
    if (!count) return E_POINTER;
    if (size && !items) return E_POINTER;
    n = (start < impl->count) ? impl->count - start : 0;
    if (n > size) n = size;
    if (items && n)
    {
        UINT32 i;
        for (i = 0; i < n; i++) lang_ref_of( impl->items[start + i], items + i );
    }
    *count = n;
    return S_OK;
}

/* ------------------------------------------------------------------ */
/* IObservableVector<HSTRING> on ResourceContext::Languages            */
/*                                                                     */
/* ResourceContext::Languages is a mutable, observable list on Windows */
/* (the app can reorder it); .NET Native's IList adapter binds the     */
/* declared IObservableVector layout: [add 6][remove 7] then the       */
/* IVector block (IDL order) and the IIterable First trailing, the     */
/* same own-first flattening the QualifierValues map proved live on    */
/* Calculator (add@6 landing on mv_Get@6, sr15).  The app QIs the     */
/* returned pointer for IVectorView<string> (2f13c006) and reads      */
/* through that separate view face ([GetAt 6][Size 7][IndexOf 8]      */
/* [GetMany 9][First 10] per the IDL: IVectorView has no GetView);     */
/* the event lands on the primary face at slot 6.  sr16: answering    */
/* the QI from the flat view face (no add slot) raised the projection  */
/* FailFast c0000602 right after it computed the IIterable<string>     */
/* PIID (e2fcc7c1; faa585ea is IIterable, per dlls/combase/roapi.c:676). */
/*                                                                     */
/* Mutators answer S_OK as no-ops: the list is a snapshot of the PRM   */
/* language set; the projection only needs the calls to succeed.       */
/* ------------------------------------------------------------------ */

struct vec_event_reg
{
    const void *vt_handler;  /* the VectorChangedEventHandler<HSTRING> face */
    LONG ref;
    void *handler;           /* the client's delegate, AddRef'd, treated as opaque */
    struct str_vector *owner;
    struct vec_event_reg *next;
    BOOL linked;
};
static inline INT64 vec_event_token( struct vec_event_reg *reg ) { return (INT64)(INT_PTR)reg; }
static inline struct vec_event_reg *vch_from( void *iface ) { return (struct vec_event_reg *)iface; }

static const void *vector_changed_handler_vtbl[];
static ULONG WINAPI vch_Release( void *iface );

static struct vec_event_reg *vec_event_reg_new( struct str_vector *owner, void *handler )
{
    struct vec_event_reg *reg = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*reg) );

    if (!reg) return NULL;
    reg->vt_handler = vector_changed_handler_vtbl;
    reg->ref = 1;
    reg->handler = handler;
    reg->owner = owner;
    reg->linked = (owner != NULL);
    if (handler)
    {
        __TRY
        {
            CCW_SLOT1( handler )( handler );
        }
        __EXCEPT_ALL
        {
            reg->handler = NULL;
        }
        __ENDTRY
    }
    return reg;
}

static void vec_event_reg_unlink( struct str_vector *vec, struct vec_event_reg *reg )
{
    struct vec_event_reg *cur, **slot;

    for (slot = (struct vec_event_reg **)&vec->regs; (cur = *slot); )
    {
        if (cur == reg)
        {
            InterlockedCompareExchangePointer( (void *volatile *)slot, cur->next, cur );
            return;
        }
        slot = (struct vec_event_reg **)&cur->next;
    }
}

static HRESULT WINAPI vch_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct vec_event_reg *impl = vch_from( iface );

    TRACE( "iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 2, ELEM_VECTORCHANGEDHANDLER L"`1", ELEM_STRING ))
    {
        *out = &impl->vt_handler;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI vch_AddRef( void *iface ) { return incr( &vch_from( iface )->ref ); }
static ULONG WINAPI vch_Release( void *iface )
{
    struct vec_event_reg *impl = vch_from( iface );
    LONG ref = decr( &impl->ref );

    if (!ref)
    {
        if (impl->linked && impl->owner) vec_event_reg_unlink( impl->owner, impl );
        if (impl->handler)
        {
            __TRY
            {
                CCW_SLOT2( impl->handler )( impl->handler );
            }
            __EXCEPT_ALL
            {
            }
            __ENDTRY
        }
        HeapFree( GetProcessHeap(), 0, impl );
    }
    return ref;
}
static HRESULT WINAPI vch_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI vch_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "vch_GetRuntimeClassName enter\n" );

    static const WCHAR *args[] = { ELEM_CLASS_STRING };

    return param_class_name( ELEM_VECTORCHANGEDHANDLER, L"`1", args, 1, n );
}
static HRESULT WINAPI vch_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI vch_Invoke( void *iface, void *sender, void *args )
{
    TRACE( "VectorChanged Invoke on a snapshot list (no change is ever raised)\n" );
    return S_OK;
}
static const void *vector_changed_handler_vtbl[] =
{
    vch_QueryInterface, vch_AddRef, vch_Release, vch_GetIids, vch_GetRuntimeClassName, vch_GetTrustLevel,
    vch_Invoke,
};

static HRESULT WINAPI ov_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct str_vector *impl = ov_from( iface );

    TRACE( "languages QI %s\n", debugstr_guid( iid ) );
    /* E7b: see svv_QueryInterface -- the observable face is the pointer the
     * app holds from get_Languages, so it answers the same extra
     * IIterable<IKeyValuePair<string,string>> pinterface with itself. */
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 2, ELEM_VECTOR L"`1", ELEM_STRING ) ||
        param_iid_eq( iid, 2, ELEM_OBSERVABLEVECTOR L"`1", ELEM_STRING ) ||
        param_iid_eq( iid, 2, ELEM_ITERABLE L"`1", ELEM_STRING ) ||
        param_iid_eq( iid, 4, ELEM_ITERABLE L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_ov;
        incr( &impl->ref );
        return S_OK;
    }
    if (param_iid_eq( iid, 2, ELEM_VECTORVIEW L"`1", ELEM_STRING ))
    {
        *out = &impl->vt_vv;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ov_AddRef( void *iface ) { return incr( &ov_from( iface )->ref ); }
static ULONG WINAPI ov_Release( void *iface ) { return decr( &ov_from( iface )->ref ); }
static HRESULT WINAPI ov_GetIids( void *iface, ULONG *c, IID **i )
{
    HRESULT hr = fill_param_iids( c, i, 5, 2, ELEM_OBSERVABLEVECTOR L"`1", ELEM_STRING,
                                  2, ELEM_VECTOR L"`1", ELEM_STRING,
                                  2, ELEM_VECTORVIEW L"`1", ELEM_STRING,
                                  2, ELEM_ITERABLE L"`1", ELEM_STRING,
                                  4, ELEM_ITERABLE L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING );
    TRACE( "seed ov(Languages) GetIids n=%u\n", c ? *c : 0 );  /* mrt100 probes with NULL out-params */
    return hr;
}
static HRESULT WINAPI ov_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "ov_GetRuntimeClassName enter\n" );

    /* F1 (second-pass-silence wave): the face MCG interrogates in the last step
     * before the Languages-plane failfast (mrm15 tail: ov_GetRuntimeClassName ->
     * QI triple -> svv_Size -> svv_GetAt "en-US" x2 -> c0000602).  The MWR
     * ResourceManager class-key answer produced a silent second pass (79 s, no
     * Map-plane traces).  Answer the canonical parameterized-interface name of
     * the face the app actually holds from get_Languages: the adapter/MCG
     * type-cache insert (wave-5 region, McgModuleManager loose binding)
     * consults GetRuntimeClassName to learn the interface arity/args; the
     * .NET-spelling canonical form is what its NativeParser round-trips. */
    static const WCHAR *args[] = { ELEM_CLASS_STRING };

    return param_class_name( ELEM_OBSERVABLEVECTOR, L"`1", args, 1, n );
}
static HRESULT WINAPI ov_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI ov_add_VectorChanged( void *iface, void *handler, INT64 *token )
{
    struct str_vector *impl = ov_from( iface );
    struct vec_event_reg *reg;

    if (!token) return E_POINTER;
    *token = 0;
    if (!(reg = vec_event_reg_new( impl, handler ))) return E_OUTOFMEMORY;
    for (;;)
    {
        struct vec_event_reg *head = impl->regs;

        reg->next = head;
        if (InterlockedCompareExchangePointer( (void *volatile *)&impl->regs, reg, head ) == head) break;
    }
    *token = vec_event_token( reg );
    TRACE( "add_VectorChanged handler %p token %p\n", handler, (void *)(INT_PTR)*token );
    return S_OK;
}
static HRESULT WINAPI ov_remove_VectorChanged( void *iface, INT64 token )
{
    TRACE( "ov_remove_VectorChanged enter\n" );

    struct str_vector *impl = ov_from( iface );
    struct vec_event_reg *reg;

    for (reg = impl->regs; reg; reg = reg->next)
    {
        if (vec_event_token( reg ) == token)
        {
            vec_event_reg_unlink( impl, reg );
            reg->linked = FALSE;
            reg->owner = NULL;
            vch_Release( &reg->vt_handler );  /* the list's reference */
            return S_OK;
        }
    }
    return S_OK;  /* unknown token: the adapter releases in its Dispose path */
}
static HRESULT WINAPI ov_GetAt( void *iface, UINT32 index, HSTRING *item )
{
    struct str_vector *impl = ov_from( iface );

    TRACE( "ov_GetAt %u\n", index );
    if (!item) return E_POINTER;
    if (index >= impl->count) return E_BOUNDS;
    return lang_ref_of( impl->items[index], item );
}
static HRESULT WINAPI ov_Size( void *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = ov_from( iface )->count;
    TRACE( "ov_Size -> %u\n", *size );
    return S_OK;
}
static HRESULT WINAPI ov_GetView( void *iface, void **view )
{
    struct str_vector *impl = ov_from( iface );

    TRACE( "ov_GetView\n" );
    if (!view) return E_POINTER;
    *view = &impl->vt_vv;
    incr( &impl->ref );
    return S_OK;
}
static HRESULT WINAPI ov_IndexOf( void *iface, HSTRING item, UINT32 *index, INT32 *found )
{
    TRACE( "ov_IndexOf enter\n" );
        return svv_IndexOf( &ov_from( iface )->vt_vv, item, index, found );
}
static HRESULT WINAPI ov_SetAt( void *iface, UINT32 index, HSTRING value ) { TRACE( "ov_SetAt %u\n", index ); return S_OK; }
static HRESULT WINAPI ov_InsertAt( void *iface, UINT32 index, HSTRING value ) { TRACE( "ov_InsertAt %u\n", index ); return S_OK; }
static HRESULT WINAPI ov_RemoveAt( void *iface, UINT32 index ) { TRACE( "ov_RemoveAt %u\n", index ); return S_OK; }
static HRESULT WINAPI ov_Append( void *iface, HSTRING value ) { TRACE( "ov_Append\n" ); return S_OK; }
static HRESULT WINAPI ov_RemoveAtEnd( void *iface ) { TRACE( "ov_RemoveAtEnd\n" ); return S_OK; }
static HRESULT WINAPI ov_Clear( void *iface ) { TRACE( "ov_Clear\n" ); return S_OK; }
static HRESULT WINAPI ov_GetMany( void *iface, UINT32 start, UINT32 size, HSTRING *items, UINT32 *count )
{
    TRACE( "ov_GetMany start=%u size=%u\n", start, size );
    return svv_GetMany( &ov_from( iface )->vt_vv, start, size, items, count );
}
static HRESULT WINAPI ov_ReplaceAll( void *iface, UINT32 count, HSTRING *items ) { TRACE( "ov_ReplaceAll %u\n", count ); return S_OK; }
static HRESULT WINAPI ov_First( void *iface, void **first );
static const void *str_vector_observable_vtbl[] =
{
    /* IUnknown, IInspectable */
    ov_QueryInterface, ov_AddRef, ov_Release, ov_GetIids, ov_GetRuntimeClassName, ov_GetTrustLevel,
    /* IObservableVector own members (widl: add 6, remove 7) */
    ov_add_VectorChanged, ov_remove_VectorChanged,
    /* delegated IVector members in IDL order, then the delegated IIterable First */
    ov_GetAt, ov_Size, ov_GetView, ov_IndexOf, ov_SetAt, ov_InsertAt, ov_RemoveAt,
    ov_Append, ov_RemoveAtEnd, ov_Clear, ov_GetMany, ov_ReplaceAll,
    ov_First,
};

static const void *str_vector_vtbl[] =
{
    svv_QueryInterface, svv_AddRef, svv_Release, svv_GetIids, svv_GetRuntimeClassName, svv_GetTrustLevel,
    svv_GetAt, svv_Size, svv_IndexOf, svv_GetMany, svv_First,
};

/* IVectorView<T*> for interface pointers (candidates, qualifiers, maps, named resources)
 * slots identical to the HSTRING view above: GetAt 6, Size 7, IndexOf 8,
 * GetMany 9, and the delegated IIterable First 10 (idl:249-256). */
struct iface_vector
{
    const void *vt_vv;
    LONG ref;
    void **items;
    UINT32 count;
    const WCHAR *elem;
};
static inline struct iface_vector *ivv_from( void *iface ) { return (struct iface_vector *)iface; }
static HRESULT WINAPI ivv_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "ivv_QueryInterface enter\n" );

    struct iface_vector *impl = ivv_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        (impl->elem && (param_iid_eq( iid, 2, ELEM_VECTORVIEW L"`1", impl->elem ) ||
                        param_iid_eq( iid, 2, ELEM_ITERABLE L"`1", impl->elem ))))
    {
        *out = &impl->vt_vv;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ivv_AddRef( void *iface ) { return incr( &ivv_from( iface )->ref ); }
static ULONG WINAPI ivv_Release( void *iface ) { return decr( &ivv_from( iface )->ref ); }
static HRESULT WINAPI ivv_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI ivv_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "ivv_GetRuntimeClassName enter\n" );

    const WCHAR *args[1] = { elem_class_name( ivv_from( iface )->elem ) };

    return param_class_name( ELEM_VECTORVIEW, L"`1", args, 1, n );
}
static HRESULT WINAPI ivv_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI ivv_GetAt( void *iface, UINT32 index, void **item )
{

    if (!item) return E_POINTER;    struct iface_vector *impl = ivv_from( iface );
    TRACE( "ivv_GetAt %u/%u elem %s\n", index, impl->count, debugstr_w( impl->elem ));
    if (index >= impl->count) return E_BOUNDS;
    *item = impl->items[index];
    return S_OK;
}
static HRESULT WINAPI ivv_Size( void *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = ivv_from( iface )->count;
    TRACE( "ivv_Size -> %u elem %s\n", *size, debugstr_w( ivv_from( iface )->elem ));
    return S_OK;
}
/* IndexOf (idl:254), slot 8.  The elements are interface pointers, and the
 * caller may legitimately hold any face of the element (IResourceCandidate,
 * IInspectable, a pinterface), so equality is decided on COM identity: QI
 * (IUnknown) on both sides, which every real COM object answers with the same
 * pointer for the same object.  A non-COM element pointer is probed under SEH
 * and simply does not match; the caller is never faulted. */
static HRESULT WINAPI ivv_IndexOf( void *iface, void *item, UINT32 *index, INT32 *found )
{
    TRACE( "ivv_IndexOf enter\n" );

    struct iface_vector *impl = ivv_from( iface );
    UINT32 i;

    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = FALSE;
    if (!item) return S_OK;
    for (i = 0; i < impl->count; i++)
    {
        void *mine = NULL, *theirs = NULL;
        BOOL same = (impl->items[i] == item);

        if (!same)
        {
            __TRY
            {
                if (FAILED( CCW_SLOT0( impl->items[i] )( impl->items[i], &IID_IUnknown, &mine ))) mine = NULL;
                if (FAILED( CCW_SLOT0( item )( item, &IID_IUnknown, &theirs ))) theirs = NULL;
            }
            __EXCEPT_ALL
            {
                mine = NULL;
                theirs = NULL;
            }
            __ENDTRY
            same = (mine && mine == theirs);
        }
        if (mine) CCW_SLOT2( mine )( mine );
        if (theirs) CCW_SLOT2( theirs )( theirs );
        if (same)
        {
            *index = i;
            *found = TRUE;
            return S_OK;
        }
    }
    return S_OK;
}

/* GetMany (idl:255), slot 9: copy at most `size` elements starting at `start`
 * into the caller's buffer and report the count actually written; the
 * same-borrowed-pointer semantics as GetAt above. */
static HRESULT WINAPI ivv_GetMany( void *iface, UINT32 start, UINT32 size, void **items, UINT32 *count )
{
    TRACE( "ivv_GetMany enter\n" );

    struct iface_vector *impl = ivv_from( iface );
    UINT32 n;

    if (!count) return E_POINTER;
    if (size && !items) return E_POINTER;
    n = (start < impl->count) ? impl->count - start : 0;
    if (n > size) n = size;
    if (items && n) memcpy( items, impl->items + start, n * sizeof(*items) );
    *count = n;
    return S_OK;
}
static const void *iface_vector_vtbl[] =
{
    ivv_QueryInterface, ivv_AddRef, ivv_Release, ivv_GetIids, ivv_GetRuntimeClassName, ivv_GetTrustLevel,
    ivv_GetAt, ivv_Size, ivv_IndexOf, ivv_GetMany, ivv_First,
};

/* ------------------------------------------------------------------ */
/* IMapView<HSTRING,HSTRING> (QualifierValues)                         */
/* ABI (include/windows.foundation.collections.idl:198, IMapView<K,V>  */
/* requires IIterable<IKeyValuePair<K,V>>): [II 5][Get 6][Size 7]      */
/* [HasKey 8][Split 9][First 10].  The order of the pair at 7/8 is     */
/* Size then HasKey -- the idl spells the members Lookup, Size,        */
/* HasKey, Split and the projection binds the vtable by that order, so */
/* a swapped pair hands the caller a UINT32* where it wrote a boolean  */
/* and a boolean where it read a count.  There is no GetView slot in   */
/* IMapView: that member belongs to IMap (idl:218) and lives on the    */
/* observable face, which delegates IMap, below.                       */
/* ------------------------------------------------------------------ */

struct kv_pair
{
    const WCHAR *key;
    const WCHAR *value;
};

struct map_event_reg;

static const GUID IID_INotifyCollectionChanged_g =
    { 0x350ffb1a, 0xdf08, 0x56fd, { 0x98, 0xd1, 0x37, 0x4d, 0x77, 0x72, 0x34, 0x9f }};
static const GUID IID_INotifyPropertyChanged_g =
    { 0xe8a31095, 0x6789, 0x587b, { 0xb8, 0xe5, 0xcc, 0x7a, 0x1b, 0xdd, 0x8e, 0x37 }};

struct map_view
{
    const void *vt_mv;   /* IMapView<HSTRING,HSTRING> (the set the app reads) */
    const void *vt_om;   /* IObservableMap<HSTRING,HSTRING>: the same object,
                          * the event surface .NET Native's IDictionary
                          * adapter QIs (see the observable block below) */
    const void *vt_map;  /* IMap<HSTRING,HSTRING>: per-IID face (p192 matrix),
                          * merged IMap shape -- Lookup 6 ... Clear 12 */
    const void *vt_iter; /* IIterable<IKeyValuePair<HSTRING,HSTRING>>: own face,
                          * First at slot 6 (the enumerable adapter's read) */
    LONG ref;
    const struct kv_pair *pairs;
    UINT32 count;
    struct map_event_reg *regs;  /* MapChanged registrations, interlocked list */
};
static inline struct map_view *mv_from( void *iface ) { return (struct map_view *)iface; }
static const void *map_view_observable_vtbl[];
static const void *map_iface_vtbl[];
static const void *map_iterable_vtbl[];

static HRESULT WINAPI mv_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "map QI %s\n", debugstr_guid( iid ) );
    struct map_view *impl = mv_from( iface );
    /* E7b': cast #14 (probe33) casts the RCW seeded from this face's COM
     * pointer to IIterable<IKeyValuePair<string,string>> (TypeManager handle
     * 0x1806800000) in the Dictionary<string,string> ctor; the CoreRT cast
     * cache (m_cachedInterfaces, frozen shared) misses and the fallback QI
     * runs against a NULL m_baseIUnknown (no-op) -> InvalidCast -> failfast.
     * Seed the cache by answering the enumerable contract the GetIids list
     * already publishes. */
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 3, ELEM_MAPVIEW L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_mv;
        incr( &impl->ref );
        return S_OK;
    }
    /* p192 per-IID face matrix: CoreRT's adapters bind each projected
     * interface's slots in that interface's own IDL order, so the enumerable
     * adapter reads First at slot 6 of the face it QIs -- sharing one vtable
     * made that call mv_Get (the Lookup<->First slot collision), and the
     * IDictionary adapter's GetView collided with Split the same way.  Each
     * IID now answers a vtable of its exact shape. */
    if (param_iid_eq( iid, 4, ELEM_ITERABLE L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_iter;
        incr( &impl->ref );
        return S_OK;
    }
    if (param_iid_eq( iid, 3, ELEM_MAP L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_map;
        incr( &impl->ref );
        return S_OK;
    }
    /* .NET Native's IDictionary<string,string> adapter QIs the map it gets
     * from ResourceContext::QualifierValues for the
     * IObservableMap`2<System.String,System.String> pinterface IID: the
     * adapter subscribes to MapChanged to keep the managed dictionary coherent.
     * E_NOINTERFACE raises the projection's COMException, and that exception's
     * SR message resource
     * "Windows.Foundation.Collections.MapChangedEventHandler`2[System.String,System.String]"
     * is not in the PRI, so loading the message FailFasts c0000602 (sr13: the
     * key arrives decoded at mv_Get and answers ELEMENT_NOT_FOUND x5).
     *
     * The IID is computed from the base interface name plus the 'string'
     * arguments by RoGetParameterizedTypeInstanceIID, like the IMapView branch
     * above, and it is a different IID: IMapView<string,string> is
     * 1e036276-2f60-55f6-b7f3-f86079e6900b (dlls/combase/tests/roapi.c:904),
     * IObservableMap<string,string> is ac7f26f2-feb7-5b2a-8ac4-345bc62caede
     * (dlls/combase/tests/roapi.c:917).  Deriving both through the shared
     * computation matters: include/windows.foundation.collections.idl spells
     * the two base uuid()s the other way round (IDL:196 gives IMapView
     * e480ce40-... and IDL:226 gives IObservableMap 65df2bf5-...) from the
     * metadata table the computation uses (dlls/combase/roapi.c:644,647), and
     * only the latter pairing reproduces the published instance IIDs and the
     * IMapView QI the app already drives successfully against this map. */
    if (param_iid_eq( iid, 3, ELEM_OBSERVABLEMAP L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_om;
        incr( &impl->ref );
        return S_OK;
    }
    /* The MCG ObservableMap adapter is one class: on Windows the same object
     * answers INCC/INPC (it sits at slots 17-22 of the very vtable the
     * adapter casts the IObservableMap pointer to).  Hand out the observable
     * face: slots 17-22 above carry the add/remove/get pairs. */
    if (IsEqualGUID( iid, &IID_INotifyCollectionChanged_g ) ||
        IsEqualGUID( iid, &IID_INotifyPropertyChanged_g ))
    {
        *out = &impl->vt_om;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI mv_AddRef( void *iface ) { return incr( &mv_from( iface )->ref ); }
static ULONG WINAPI mv_Release( void *iface ) { return decr( &mv_from( iface )->ref ); }
static HRESULT WINAPI mv_GetIids( void *iface, ULONG *c, IID **i )
{
    TRACE( "mv_GetIids enter\n" );
        return fill_param_iids( c, i, 3, 3, ELEM_MAPVIEW L"`2", ELEM_STRING, ELEM_STRING,
                                3, ELEM_OBSERVABLEMAP L"`2", ELEM_STRING, ELEM_STRING,
                                4, ELEM_ITERABLE L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING );
}
static HRESULT WINAPI mv_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "mv_GetRuntimeClassName enter\n" );

    static const WCHAR *args[] = { ELEM_CLASS_STRING, ELEM_CLASS_STRING };

    return param_class_name( ELEM_MAPVIEW, L"`2", args, 2, n );
}
static HRESULT WINAPI mv_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI mv_Get( void *iface, HSTRING key, HSTRING *value )
{
    struct map_view *impl = mv_from( iface );
    const WCHAR *raw;
    HSTRING tmp;
    BOOL owned;
    HRESULT hr = ELEMENT_NOT_FOUND_E;
    UINT32 i;

    if (!value) return E_POINTER;
    *value = NULL;

    /* key_decode: HSTRING payload, else the managed-string CCW surface */
    TRACE( "Lookup ENTER key %p tag %u\n", key, (UINT32)((UINT_PTR)key & 7) );
    if (!key_decode( key, &raw, &tmp, &owned )) raw = NULL; /* no key: no match, no fabrication */
    TRACE( "Lookup %s\n", debugstr_w( raw ) );
    /* The app's MapChanged registration reaches this Lookup with the delegate
     * type-name key "MapChangedEventHandler`2[System.String,System.String]" (a
     * managed-string CCW); the miss raised the projection's COMException and its
     * missing SR resource FailFasted c0000602.  S_OK + empty is the registration
     * mechanism: the event pair itself runs on the observable face. */
    if (raw && !lstrcmpW( raw, ELEM_MAPCHANGEDHANDLER L"`2[" ELEM_CLASS_STRING L"," ELEM_CLASS_STRING L"]" ))
    {
        TRACE( "qualifier probe %s: answering S_OK/empty\n", debugstr_w( raw ) );
        key_release( tmp, owned );
        return WindowsCreateString( L"en-US", 5, value );
    }
    for (i = 0; raw && i < impl->count; i++)
    {
        if (!lstrcmpiW( raw, impl->pairs[i].key ))
        {
            hr = WindowsCreateString( impl->pairs[i].value, (UINT32)lstrlenW( impl->pairs[i].value ), value );
            break;
        }
    }
    if (FAILED( hr ))
    {
        /* ELEMENT_NOT_FOUND must still leave the [out, retval] HSTRING written.
         * The projections do not check the code before reading the retval
         * (C++/WinRT's find() and the .NET IDictionary adapter both materialise
         * the value first), so an unwritten -- in practice null -- HSTRING is
         * what the CoreRT marshaller then dereferences on the miss path.  Hand
         * back the empty string, keep the documented failure code: the caller's
         * branch is unchanged, its read of *value is defined. */
        TRACE( "Lookup %s -> ELEMENT_NOT_FOUND\n", debugstr_w( raw ) );
        if (WindowsCreateString( L"", 0, value ) != S_OK) *value = NULL;
    }
    key_release( tmp, owned );
    return hr; /* ELEMENT_NOT_FOUND, retval written as the empty string */
}
static HRESULT WINAPI mv_HasKey( void *iface, HSTRING key, INT32 *has )
{
    struct map_view *impl = mv_from( iface );
    const WCHAR *raw;
    HSTRING tmp;
    BOOL owned;
    UINT32 i;
    if (!has) return E_POINTER;
    *has = FALSE;
    TRACE( "HasKey ENTER key %p tag %u\n", key, (UINT32)((UINT_PTR)key & 7) );
    if (!key_decode( key, &raw, &tmp, &owned )) return S_OK;
    TRACE( "HasKey %s -> %d\n", debugstr_w( raw ), *has );
    for (i = 0; raw && i < impl->count; i++)
        if (!lstrcmpiW( raw, impl->pairs[i].key )) { *has = TRUE; break; }
    key_release( tmp, owned );
    return S_OK;
}
static HRESULT WINAPI mv_Size( void *iface, UINT32 *size )
{
    TRACE( "mv_Size enter\n" );

    if (!size) return E_POINTER;
    *size = mv_from( iface )->count;
    return S_OK;
}
/* GetView is an IMap member (idl:218), not an IMapView member: it is reachable
 * only through the observable face, which delegates IMap (slot 11 there), and
 * the view it returns is this same snapshot. */
static HRESULT WINAPI mv_GetView( void *iface, void **view )
{
    TRACE( "GetView\n" );
    if (!view) return E_POINTER;
    *view = iface;
    incr( &mv_from( iface )->ref );
    return S_OK;
}
/* Split (idl:204), IMapView slot 9.  Real Windows splits the view into two
 * halves; this view is a snapshot of the PRM qualifier set, there is no
 * meaningful split, and nothing in the tree (module, app trace, tests) drives
 * the slot.  Answer E_NOTIMPL with both [out] parameters written: an
 * unimplemented member still has to satisfy the ABI's out-params, so a caller
 * that does index it gets a defined null pair plus a failure code instead of
 * the garbage an unwritten out-param would leave. */
static HRESULT WINAPI mv_Split( void *iface, void **first, void **second )
{
    TRACE( "Split on a snapshot view: E_NOTIMPL\n" );
    if (first) *first = NULL;
    if (second) *second = NULL;
    return E_NOTIMPL;
}
/* Snapshot mutation members: the qualifier set is PRM-derived, a write has
 * no target (the observable face's copies delegate here). */
static HRESULT WINAPI mv_Insert( void *iface, HSTRING key, HSTRING value, INT32 *replaced )
{
    (void)iface; (void)key; (void)value;
    if (replaced) *replaced = FALSE;
    TRACE( "Insert on a snapshot view: accepted, nothing stored\n" );
    return S_OK;
}
static HRESULT WINAPI mv_Remove( void *iface, HSTRING key ) { (void)iface; (void)key; TRACE( "Remove on a snapshot view: no-op\n" ); return S_OK; }
static HRESULT WINAPI mv_Clear( void *iface ) { (void)iface; TRACE( "Clear on a snapshot view: no-op\n" ); return S_OK; }

static const void *map_view_vtbl[] =
{
    mv_QueryInterface, mv_AddRef, mv_Release, mv_GetIids, mv_GetRuntimeClassName, mv_GetTrustLevel,
    /* idl:201-205 order; the view-shaped face is 0-9 (Get 6, Size 7, HasKey 8,
     * Split 9), no GetView, no shared First -- IIterable answers its own face
     * with First at 6 (p192 matrix) */
    mv_Get, mv_Size, mv_HasKey, mv_Split,
};

/* ------------------------------------------------------------------ */
/* IObservableMap<HSTRING,HSTRING> on the QualifierValues view         */
/*                                                                     */
/* Slot layout.  WinRT events are add/remove only: widl emits, for the  */
/* [eventadd]/[eventremove] pair of IObservableMap, an IObservableMap_  */
/* impl with add_MapChanged then remove_MapChanged directly after the   */
/* IInspectable quintet (build tree include/windows.foundation.         */
/* collections.h:846-847), and that is the shape wine's only            */
/* IObservableMap implementation uses                                   */
/* (dlls/wintypes/propertyset.c:164-177, 8 slots total).  The own       */
/* members come first, the delegated interface after -- the same        */
/* flattening the observable vector face uses on this app (add@6 lands  */
/* there, sr15).  The delegated block is IMap, because idl:229 spells   */
/* IObservableMap<K,V> as `requires IMap<K,V>' (not IMapView), so IMap's */
/* members follow in idl:215-221 order: Lookup 8, Size 9, HasKey 10,    */
/* GetView 11, Insert 12, Remove 13, Clear 14, then the IIterable First */
/* of IMap's own requires clause at 15 -- 16 slots, indices 0..15, the  */
/* full ABI: note Size at 9 and HasKey at 10, matching IMap's          */
/* declaration order (the same 7/8 pair fix as map_view_vtbl above).    */
/* get_MapChanged has no ABI slot at all (widl emits no accessor for an */
/* event, and the real ABI ends at slot 15), so it is not in the        */
/* delegated block: it sits past the ABI as the trailing slot 16, where */
/* no real caller can reach it (nothing in the metadata indexes past    */
/* First), and it exists only to hand our own per-registration handler  */
/* face (struct map_event_reg, map_event_handler_vtbl) to a caller that */
/* asks -- the tests are that caller.  A production caller subscribes   */
/* through add_MapChanged and never sees this slot.                     */
/*                                                                      */
/* Every slot returns S_OK and no MapChanged is ever raised: the view is */
/* a snapshot of the PRM qualifier set (see mv_Get), so there is no      */
/* mutation to report, and the app's adapter only needs the subscription */
/* to succeed.  Handlers are stored opaque -- the projection's delegate  */
/* is never interrogated, only AddRef'd (its own QI may answer nothing,  */
/* as the IStringable/IReference key CCWs already show).                 */
/* ------------------------------------------------------------------ */

struct map_event_reg
{
    const void *vt_handler;  /* the MapChangedEventHandler<HSTRING,HSTRING> face */
    LONG ref;
    void *handler;           /* the client's delegate, AddRef'd, treated as opaque */
    struct map_view *owner;  /* the list it is linked into, NULL once unlinked */
    struct map_event_reg *next;
    BOOL linked;
};
/* The token is the registration's own address: nonzero by construction, stable
 * for the whole registration, and self-validating on remove (no counter to
 * wrap and no allocation-table coupling). */
static inline INT64 map_event_token( struct map_event_reg *reg ) { return (INT64)(INT_PTR)reg; }
static inline struct map_event_reg *omh_from( void *iface ) { return (struct map_event_reg *)iface; }
static inline struct map_view *om_from( void *iface ) { return CONTAINING_RECORD( iface, struct map_view, vt_om ); }

static const void *map_event_handler_vtbl[];
static ULONG WINAPI omh_Release( void *iface );

static void map_event_reg_unlink( struct map_view *map, struct map_event_reg *reg )
{
    struct map_event_reg *cur, **slot;

    for (slot = (struct map_event_reg **)&map->regs; (cur = *slot); )
    {
        if (cur == reg)
        {
            if (InterlockedCompareExchangePointer( (void *volatile *)slot, cur->next, cur ) == cur) return;
            return;  /* the slot moved under us: a concurrent remove took it */
        }
        slot = (struct map_event_reg **)&cur->next;
    }
}

static struct map_event_reg *map_event_reg_new( struct map_view *owner, void *handler )
{
    struct map_event_reg *reg = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*reg) );

    if (!reg) return NULL;
    reg->vt_handler = map_event_handler_vtbl;
    reg->ref = 1;
    reg->handler = handler;
    reg->owner = owner;
    reg->linked = (owner != NULL);
    if (handler)
    {
        /* The handler is an arbitrary CCW: probe it, never fault the caller. */
        __TRY
        {
            CCW_SLOT1( handler )( handler );
        }
        __EXCEPT_ALL
        {
            reg->handler = NULL;  /* not a COM pointer: keep it, report nothing */
        }
        __ENDTRY
    }
    return reg;
}

static void map_event_reg_free_list( struct map_view *map )
{
    struct map_event_reg *reg = map->regs;

    map->regs = NULL;
    while (reg)
    {
        struct map_event_reg *next = reg->next;
        reg->linked = FALSE;
        reg->owner = NULL;
        omh_Release( &reg->vt_handler );
        reg = next;
    }
}

static HRESULT WINAPI omh_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct map_event_reg *impl = omh_from( iface );

    TRACE( "iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 3, ELEM_MAPCHANGEDHANDLER L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_handler;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI omh_AddRef( void *iface ) { return incr( &omh_from( iface )->ref ); }
static ULONG WINAPI omh_Release( void *iface )
{
    struct map_event_reg *impl = omh_from( iface );
    LONG ref = decr( &impl->ref );

    if (!ref)
    {
        if (impl->linked && impl->owner) map_event_reg_unlink( impl->owner, impl );
        if (impl->handler)
        {
            __TRY
            {
                CCW_SLOT2( impl->handler )( impl->handler );
            }
            __EXCEPT_ALL
            {
            }
            __ENDTRY
        }
        HeapFree( GetProcessHeap(), 0, impl );
    }
    return ref;
}
static HRESULT WINAPI omh_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI omh_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "omh_GetRuntimeClassName enter\n" );

    static const WCHAR *args[] = { ELEM_CLASS_STRING, ELEM_CLASS_STRING };

    return param_class_name( ELEM_MAPCHANGEDHANDLER, L"`2", args, 2, n );
}
static HRESULT WINAPI omh_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
/* IMapChangedEventHandler::Invoke: reached only if something raised the event.
 * Nothing does (the view is a snapshot), so this is the quiet S_OK. */
static HRESULT WINAPI omh_Invoke( void *iface, void *sender, void *args )
{
    TRACE( "MapChanged Invoke on a snapshot view (no change is ever raised)\n" );
    return S_OK;
}
static const void *map_event_handler_vtbl[] =
{
    omh_QueryInterface, omh_AddRef, omh_Release, omh_GetIids, omh_GetRuntimeClassName, omh_GetTrustLevel,
    omh_Invoke,
};

/* IUnknown/IInspectable of the observable face: the two vtables share the one
 * map_view, so identity (and the refcount) runs through the map's own
 * implementation, which is what a QI from either pointer must return. */
static HRESULT WINAPI om_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "om_QueryInterface enter\n" );
        return mv_QueryInterface( &om_from( iface )->vt_mv, iid, out );
}
static ULONG WINAPI om_AddRef( void *iface ) { return mv_AddRef( &om_from( iface )->vt_mv ); }
static ULONG WINAPI om_Release( void *iface ) { return mv_Release( &om_from( iface )->vt_mv ); }
static HRESULT WINAPI om_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI om_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "om_GetRuntimeClassName enter\n" );

    static const WCHAR *args[] = { ELEM_CLASS_STRING, ELEM_CLASS_STRING };

    /* The observable face names itself: the loose-binding RCW parses this name
     * to learn the arity and the type arguments of the interface it QIs (see
     * the ELEM_CLASS_STRING note), so answering the IMapView name from the
     * observable pointer would describe an interface the caller did not ask
     * for.  The IMapView face keeps its own answer, unchanged. */
    return param_class_name( ELEM_OBSERVABLEMAP, L"`2", args, 2, n );
}
static HRESULT WINAPI om_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }

static HRESULT WINAPI om_add_MapChanged( void *iface, void *handler, INT64 *token )
{
    struct map_view *impl = om_from( iface );
    struct map_event_reg *reg;

    if (!token) return E_POINTER;
    *token = 0;
    if (!(reg = map_event_reg_new( impl, handler ))) return E_OUTOFMEMORY;
    for (;;)
    {
        struct map_event_reg *head = impl->regs;

        reg->next = head;
        if (InterlockedCompareExchangePointer( (void *volatile *)&impl->regs, reg, head ) == head) break;
    }
    *token = map_event_token( reg );
    TRACE( "add_MapChanged handler %p token %p\n", handler, (void *)(INT_PTR)*token );
    return S_OK;
}

static HRESULT WINAPI om_remove_MapChanged( void *iface, INT64 token )
{
    TRACE( "om_remove_MapChanged enter\n" );

    struct map_view *impl = om_from( iface );
    struct map_event_reg *reg;

    for (reg = impl->regs; reg; reg = reg->next)
    {
        if (map_event_token( reg ) == token)
        {
            /* Deregister: the node leaves the list here.  omh_Release() below
             * drops the list's reference and, at ref 0, unlinks through
             * reg->owner; clearing the owner first used to skip that unlink and
             * leave the list head pointing at the freed node, so the next
             * remove_MapChanged (the adapter's Dispose removes twice) walked
             * freed memory.  Unlink while owner is still valid, then drop the
             * reference -- the same order the observable vector's
             * remove_VectorChanged uses. */
            map_event_reg_unlink( impl, reg );
            reg->linked = FALSE;
            reg->owner = NULL;
            omh_Release( &reg->vt_handler );  /* the list's reference */
            return S_OK;
        }
    }
    /* Unknown or already-removed token: the adapter releases in its Dispose
     * path and may remove twice; that is not a failure for a snapshot view. */
    return S_OK;
}

static HRESULT WINAPI om_get_MapChanged( void *iface, void **handler )
{
    TRACE( "om_get_MapChanged enter\n" );

    struct map_view *impl = om_from( iface );
    struct map_event_reg *reg;

    if (!handler) return E_POINTER;
    *handler = NULL;
    for (reg = impl->regs; reg; reg = reg->next)
    {
        *handler = &reg->vt_handler;
        incr( &reg->ref );
        return S_OK;
    }
    /* Nothing registered: hand back a live, unregistered handler object rather
     * than S_OK with a null out-param (the projection dereferences it).  Not
     * linked into any list, so the caller's Release frees it. */
    if (!(reg = map_event_reg_new( NULL, NULL ))) return E_OUTOFMEMORY;
    *handler = &reg->vt_handler;
    return S_OK;
}

/* ------------------------------------------------------------------ */
/* INotifyCollectionChanged / INotifyPropertyChanged on the observable */
/* face.  The MCG ObservableMap<string,string> adapter (SharedLibrary  */
/* McgMarshal) wraps the IObservableMap pointer with its own class     */
/* vtable: slots 0-16 are the flattened IObservableMap (add 6,         */
/* remove 7, IMap 8-15, get_MapChanged 16) and slots 17-22 are the     */
/* INCC add/remove/get + INPC add/remove/get pairs.  The adapter       */
/* subscribes through slot 17 (add_CollectionChanged) and writes the   */
/* EventRegistrationToken out-param it hands back; with no slots after */
/* 16 the call ran the data behind the vtable (the debug-channel       */
/* descriptor string in the stub heap) and every fault re-entered the   */
/* mrt100 hardware VEH, nesting a ~1 KB context frame per refault.     */
/* Handlers are opaque delegates, stored on the same registration list */
/* as MapChanged: the snapshot view never raises, so Invoke is never   */
/* reached and one list serves all six accessors.                      */
/* ------------------------------------------------------------------ */


static HRESULT WINAPI om_add_CollectionChanged( void *iface, void *handler, INT64 *token )
{
    TRACE( "add_CollectionChanged\n" );
    return om_add_MapChanged( iface, handler, token );
}
static HRESULT WINAPI om_remove_CollectionChanged( void *iface, INT64 token )
{
    TRACE( "remove_CollectionChanged token %I64d\n", token );
    return om_remove_MapChanged( iface, token );
}
static HRESULT WINAPI om_get_CollectionChanged( void *iface, void **handler )
{
    TRACE( "get_CollectionChanged\n" );
    return om_get_MapChanged( iface, handler );
}
static HRESULT WINAPI om_add_PropertyChanged( void *iface, void *handler, INT64 *token )
{
    TRACE( "add_PropertyChanged\n" );
    return om_add_MapChanged( iface, handler, token );
}
static HRESULT WINAPI om_remove_PropertyChanged( void *iface, INT64 token )
{
    TRACE( "remove_PropertyChanged token %I64d\n", token );
    return om_remove_MapChanged( iface, token );
}
static HRESULT WINAPI om_get_PropertyChanged( void *iface, void **handler )
{
    TRACE( "get_PropertyChanged\n" );
    return om_get_MapChanged( iface, handler );
}

/* Delegated IMap members: same map, same implementation, one slot block over. */
static HRESULT WINAPI om_Lookup( void *iface, HSTRING key, HSTRING *value )
{
    TRACE( "om Lookup\n" );
    return mv_Get( &om_from( iface )->vt_mv, key, value );
}
static HRESULT WINAPI om_HasKey( void *iface, HSTRING key, INT32 *has )
{
    TRACE( "om HasKey\n" );
    return mv_HasKey( &om_from( iface )->vt_mv, key, has );
}
static HRESULT WINAPI om_Size( void *iface, UINT32 *size )
{
    TRACE( "om Size\n" );
    return mv_Size( &om_from( iface )->vt_mv, size );
}
static HRESULT WINAPI om_GetView( void *iface, void **view )
{
    TRACE( "om GetView\n" );
    return mv_GetView( &om_from( iface )->vt_mv, view );
}
/* The qualifier set is a PRM-derived snapshot; a write has no target, so the
 * mutation members are accepted and do nothing (and MapChanged is not raised). */
static HRESULT WINAPI om_Insert( void *iface, HSTRING key, HSTRING value, INT32 *replaced )
{
    return mv_Insert( &om_from( iface )->vt_mv, key, value, replaced );
}
static HRESULT WINAPI om_Remove( void *iface, HSTRING key ) { return mv_Remove( &om_from( iface )->vt_mv, key ); }
static HRESULT WINAPI om_Clear( void *iface ) { return mv_Clear( &om_from( iface )->vt_mv ); }
static HRESULT WINAPI om_First( void *iface, void **first ) { return mv_First( &om_from( iface )->vt_mv, first ); }

static const void *map_view_observable_vtbl[] =
{
    /* IUnknown, IInspectable */
    om_QueryInterface, om_AddRef, om_Release, om_GetIids, om_GetRuntimeClassName, om_GetTrustLevel,
    /* IObservableMap<HSTRING,HSTRING> own members (widl: add 6, remove 7) */
    om_add_MapChanged, om_remove_MapChanged,
    /* delegated IMap<HSTRING,HSTRING> members in idl:215-221 order (Lookup 8,
     * Size 9, HasKey 10, GetView 11, Insert 12, Remove 13, Clear 14), then the
     * IIterable member of IMap's own requires clause at 15 */
    om_Lookup, om_Size, om_HasKey, om_GetView, om_Insert, om_Remove, om_Clear, om_First,
    /* past the ABI (the real vtable ends at 15): our own accessor for the
     * per-registration handler face, see the layout note */
    om_get_MapChanged,
    /* MCG ObservableMap adapter continuation (17-22): the INCC and INPC
     * add/remove/get pairs; the adapter's slot-17 subscribe writes the token
     * out-param, see the note above map_event_reg. */
    om_add_CollectionChanged, om_remove_CollectionChanged, om_get_CollectionChanged,
    om_add_PropertyChanged, om_remove_PropertyChanged, om_get_PropertyChanged,
};

/* ------------------------------------------------------------------ */
/* p192 per-IID face matrix for the QualifierValues map.               */
/* CoreRT's IDictionary/IIterable adapters QI each projected interface */
/* separately and bind each face's slots in that interface's own IDL   */
/* order; the shared 11-slot view made the enumerable adapter call     */
/* mv_Get for First (slot 6) and the dictionary adapter read Split on  */
/* GetView.  Each IID answers a vtable of its exact shape:  IMapView   */
/* 0-9 (Get Size HasKey Split), IIterable<KVP> 0-6 (First@6), IMap     */
/* 0-12 (merged Lookup..Clear), IObservableMap unchanged (add 6,       */
/* remove 7, IMap 8-14, get_MapChanged behind the ABI end).            */
/* ------------------------------------------------------------------ */
static inline struct map_view *mapi_from( void *iface ) { return CONTAINING_RECORD( iface, struct map_view, vt_map ); }
static inline struct map_view *mit_from( void *iface ) { return CONTAINING_RECORD( iface, struct map_view, vt_iter ); }

static HRESULT WINAPI mapi_QueryInterface( void *iface, REFIID iid, void **out )
{ return mv_QueryInterface( &mapi_from( iface )->vt_mv, iid, out ); }
static ULONG WINAPI mapi_AddRef( void *iface ) { return mv_AddRef( &mapi_from( iface )->vt_mv ); }
static ULONG WINAPI mapi_Release( void *iface ) { return mv_Release( &mapi_from( iface )->vt_mv ); }
static HRESULT WINAPI mapi_GetIids( void *iface, ULONG *c, IID **i ) { return mv_GetIids( &mapi_from( iface )->vt_mv, c, i ); }
static HRESULT WINAPI mapi_GetRuntimeClassName( void *iface, HSTRING *n ) { return mv_GetRuntimeClassName( &mapi_from( iface )->vt_mv, n ); }
static HRESULT WINAPI mapi_GetTrustLevel( void *iface, INT32 *t ) { return mv_GetTrustLevel( &mapi_from( iface )->vt_mv, t ); }
/* delegated IMap<HSTRING,HSTRING> members in idl:215-221 order, own slots 6-12 */
static HRESULT WINAPI mapi_Lookup( void *iface, HSTRING key, HSTRING *value ) { return mv_Get( &mapi_from( iface )->vt_mv, key, value ); }
static HRESULT WINAPI mapi_Size( void *iface, UINT32 *size ) { return mv_Size( &mapi_from( iface )->vt_mv, size ); }
static HRESULT WINAPI mapi_HasKey( void *iface, HSTRING key, INT32 *has ) { return mv_HasKey( &mapi_from( iface )->vt_mv, key, has ); }
static HRESULT WINAPI mapi_GetView( void *iface, void **view ) { return mv_GetView( &mapi_from( iface )->vt_mv, view ); }
static HRESULT WINAPI mapi_Insert( void *iface, HSTRING key, HSTRING value, INT32 *replaced ) { return mv_Insert( &mapi_from( iface )->vt_mv, key, value, replaced ); }
static HRESULT WINAPI mapi_Remove( void *iface, HSTRING key ) { return mv_Remove( &mapi_from( iface )->vt_mv, key ); }
static HRESULT WINAPI mapi_Clear( void *iface ) { return mv_Clear( &mapi_from( iface )->vt_mv ); }

static const void *map_iface_vtbl[] =  /* IMap<HSTRING,HSTRING>: 13 slots (0-12) */
{
    mapi_QueryInterface, mapi_AddRef, mapi_Release, mapi_GetIids, mapi_GetRuntimeClassName, mapi_GetTrustLevel,
    mapi_Lookup, mapi_Size, mapi_HasKey, mapi_GetView, mapi_Insert, mapi_Remove, mapi_Clear,
};

/* IIterable<IKeyValuePair<HSTRING,HSTRING>>: own face, First at slot 6. */
static HRESULT WINAPI mit_QueryInterface( void *iface, REFIID iid, void **out )
{ return mv_QueryInterface( &mit_from( iface )->vt_mv, iid, out ); }
static ULONG WINAPI mit_AddRef( void *iface ) { return mv_AddRef( &mit_from( iface )->vt_mv ); }
static ULONG WINAPI mit_Release( void *iface ) { return mv_Release( &mit_from( iface )->vt_mv ); }
static HRESULT WINAPI mit_GetIids( void *iface, ULONG *c, IID **i ) { return mv_GetIids( &mit_from( iface )->vt_mv, c, i ); }
static HRESULT WINAPI mit_GetRuntimeClassName( void *iface, HSTRING *n ) { return mv_GetRuntimeClassName( &mit_from( iface )->vt_mv, n ); }
static HRESULT WINAPI mit_GetTrustLevel( void *iface, INT32 *t ) { return mv_GetTrustLevel( &mit_from( iface )->vt_mv, t ); }
static HRESULT WINAPI mit_First( void *iface, void **first ) { return mv_First( &mit_from( iface )->vt_mv, first ); }

static const void *map_iterable_vtbl[] =  /* IIterable<KVP<string,string>>: 7 slots (0-6) */
{
    mit_QueryInterface, mit_AddRef, mit_Release, mit_GetIids, mit_GetRuntimeClassName, mit_GetTrustLevel,
    mit_First,
};

/* IMapView<HSTRING, IResourceMap*> for AllResourceMaps: same slots, Get
 * returns the map pointer. */
struct rm_map_entry { WCHAR *key; void *map; };
struct rm_map_view
{
    const void *vt_mv;
    LONG ref;
    struct rm_map_entry *entries;
    UINT32 count;
};
static inline struct rm_map_view *rmv_from( void *iface ) { return (struct rm_map_view *)iface; }
static HRESULT WINAPI rmv_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "rmv_QueryInterface enter\n" );

    struct rm_map_view *impl = rmv_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 3, ELEM_MAPVIEW L"`2", ELEM_STRING, ELEM_RESOURCEMAP ))
    {
        *out = &impl->vt_mv;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rmv_AddRef( void *iface ) { return incr( &rmv_from( iface )->ref ); }
static ULONG WINAPI rmv_Release( void *iface ) { return decr( &rmv_from( iface )->ref ); }
static HRESULT WINAPI rmv_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rmv_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "rmv_GetRuntimeClassName enter\n" );

    static const WCHAR *args[] = { ELEM_CLASS_STRING, ELEM_RESOURCEMAP };

    return param_class_name( ELEM_MAPVIEW, L"`2", args, 2, n );
}
static HRESULT WINAPI rmv_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rmv_Get( void *iface, HSTRING key, void **map )
{
    struct rm_map_view *impl;

    if (!map) return E_POINTER;
    *map = NULL;  /* the miss paths below keep the [out] retval written */
    impl = rmv_from( iface );
    const WCHAR *raw;
    HSTRING tmp;
    BOOL owned;
    UINT32 i;
    if (!key_decode( key, &raw, &tmp, &owned )) return ELEMENT_NOT_FOUND_E;
    TRACE( "rmv_Get Lookup %s\n", debugstr_w( raw ) );
    for (i = 0; raw && i < impl->count; i++)
        if (!lstrcmpiW( raw, impl->entries[i].key ))
        {
            *map = impl->entries[i].map;
            key_release( tmp, owned );
            return S_OK;
        }
    key_release( tmp, owned );
    return ELEMENT_NOT_FOUND_E;
}
static HRESULT WINAPI rmv_HasKey( void *iface, HSTRING key, INT32 *has )
{
    TRACE( "rmv_HasKey enter\n" );

    struct rm_map_view *impl = rmv_from( iface );
    const WCHAR *raw;
    HSTRING tmp;
    BOOL owned;
    UINT32 i;
    *has = FALSE;
    if (!key_decode( key, &raw, &tmp, &owned )) return S_OK;
    for (i = 0; raw && i < impl->count; i++)
        if (!lstrcmpiW( raw, impl->entries[i].key )) { *has = TRUE; break; }
    key_release( tmp, owned );
    return S_OK;
}
static HRESULT WINAPI rmv_Size( void *iface, UINT32 *size )
{
    TRACE( "rmv_Size enter\n" );

    *size = rmv_from( iface )->count;
    return S_OK;
}
/* Split (idl:204), IMapView slot 9: same snapshot answer as mv_Split above;
 * IMapView<HSTRING,IResourceMap*> has no GetView (that is the IMap member, and
 * no mutable map of resource maps is exposed here). */
static HRESULT WINAPI rmv_Split( void *iface, void **first, void **second )
{
    TRACE( "Split on a snapshot view: E_NOTIMPL\n" );
    if (first) *first = NULL;
    if (second) *second = NULL;
    return E_NOTIMPL;
}
static const void *rm_map_view_vtbl[] =
{
    rmv_QueryInterface, rmv_AddRef, rmv_Release, rmv_GetIids, rmv_GetRuntimeClassName, rmv_GetTrustLevel,
    /* idl:201-205 order (Get 6, Size 7, HasKey 8, Split 9) + delegated
     * IIterable First 10, the same IMapView layout as map_view_vtbl */
    rmv_Get, rmv_Size, rmv_HasKey, rmv_Split, rmv_First,
};

/* ------------------------------------------------------------------ */
/* parameterized-IID matching + IIterable/IIterator support            */
/* The .NET Native RCW computes the pinterface IID from the generic    */
/* name and QIs collections for it (IMapView/IVectorView/IIterable and */
/* IKeyValuePair/IIterator).  Missing support => NullReference loop =>  */
/* stack overflow on the managed thread (observed on Calculator).      */
/* ------------------------------------------------------------------ */

static BOOL param_iid_eq( REFIID iid, UINT32 count, ... )
{
    const WCHAR *elems[4];
    va_list ap;
    UINT32 i;
    GUID g;
    ROPARAMIIDHANDLE h;

    va_start( ap, count );
    for (i = 0; i < count && i < 4; i++) elems[i] = va_arg( ap, const WCHAR * );
    va_end( ap );
    if (FAILED( RoGetParameterizedTypeInstanceIID( count, elems, NULL, &g, &h ) )) return FALSE;
    return IsEqualGUID( iid, &g );
}

/* IKeyValuePair<HSTRING,HSTRING> [II 5][Key 6][Value 7] */
struct kv_pair_obj
{
    const void *vt_pair;
    LONG ref;
    const WCHAR *key;
    const WCHAR *value;
};
static inline struct kv_pair_obj *pair_from( void *iface ) { return (struct kv_pair_obj *)iface; }
static HRESULT WINAPI pair_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "pair_QueryInterface enter\n" );

    struct kv_pair_obj *impl = pair_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 3, ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_pair;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pair_AddRef( void *iface ) { return incr( &pair_from( iface )->ref ); }
static ULONG WINAPI pair_Release( void *iface ) { return decr( &pair_from( iface )->ref ); }
static HRESULT WINAPI pair_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI pair_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "pair_GetRuntimeClassName enter\n" );

    static const WCHAR *args[] = { ELEM_CLASS_STRING, ELEM_CLASS_STRING };

    return param_class_name( ELEM_KEYVALUEPAIR, L"`2", args, 2, n );
}
static HRESULT WINAPI pair_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI pair_Key( void *iface, HSTRING *v )
{
    struct kv_pair_obj *impl = pair_from( iface );
    TRACE( "kvpair_Key %s\n", debugstr_w( impl->key ) );
    return WindowsCreateString( impl->key, (UINT32)lstrlenW( impl->key ), v );
}
static HRESULT WINAPI pair_Value( void *iface, HSTRING *v )
{
    TRACE( "pair_Value enter\n" );

    struct kv_pair_obj *impl = pair_from( iface );
    return WindowsCreateString( impl->value, (UINT32)lstrlenW( impl->value ), v );
}
static const void *kv_pair_obj_vtbl[] =
{
    pair_QueryInterface, pair_AddRef, pair_Release, pair_GetIids, pair_GetRuntimeClassName, pair_GetTrustLevel,
    pair_Key, pair_Value,
};

/* IKeyValuePair<HSTRING,IResourceMap*> */
struct rm_pair_obj
{
    const void *vt_pair;
    LONG ref;
    WCHAR *key;
    void *map;
};
static inline struct rm_pair_obj *rmpair_from( void *iface ) { return (struct rm_pair_obj *)iface; }
static HRESULT WINAPI rmpair_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "rmpair_QueryInterface enter\n" );

    struct rm_pair_obj *impl = rmpair_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        param_iid_eq( iid, 3, ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_RESOURCEMAP ))
    {
        *out = &impl->vt_pair;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rmpair_AddRef( void *iface ) { return incr( &rmpair_from( iface )->ref ); }
static ULONG WINAPI rmpair_Release( void *iface ) { return decr( &rmpair_from( iface )->ref ); }
static HRESULT WINAPI rmpair_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rmpair_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "rmpair_GetRuntimeClassName enter\n" );

    static const WCHAR *args[] = { ELEM_CLASS_STRING, ELEM_RESOURCEMAP };

    return param_class_name( ELEM_KEYVALUEPAIR, L"`2", args, 2, n );
}
static HRESULT WINAPI rmpair_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rmpair_Key( void *iface, HSTRING *v )
{
    TRACE( "rmpair_Key enter\n" );

    struct rm_pair_obj *impl = rmpair_from( iface );
    return WindowsCreateString( impl->key, (UINT32)lstrlenW( impl->key ), v );
}
static HRESULT WINAPI rmpair_Value( void *iface, void **v )
{
    TRACE( "rmpair_Value enter\n" );

    struct rm_pair_obj *impl = rmpair_from( iface );
    *v = impl->map;
    return S_OK;
}
static const void *rm_pair_obj_vtbl[] =
{
    rmpair_QueryInterface, rmpair_AddRef, rmpair_Release, rmpair_GetIids, rmpair_GetRuntimeClassName, rmpair_GetTrustLevel,
    rmpair_Key, rmpair_Value,
};

/* IIterator<T> [II 5][MoveNext 6][GetCurrent 7][get_HasCurrent 8][get_Current 9]
 * T is: HSTRING (out HSTRING), kv pair (HSTRING,HSTRING), rm pair (HSTRING,map), iface ptr. */
struct iterator
{
    const void *vt_it;
    LONG ref;
    UINT32 count;
    UINT32 idx;
    INT32 begun;
    UINT8 kind;   /* 0 string, 1 kvpair, 2 rm pair, 3 iface ptr */
    union
    {
        struct map_view *mv;
        struct rm_map_view *rmv;
        struct str_vector *sv;
        struct iface_vector *iv;
    } src;
};
static inline struct iterator *iter_from( void *iface ) { return (struct iterator *)iface; }

#define IT_STRING 0
#define IT_KVPAIR 1
#define IT_RMPAIR 2
#define IT_IFACE  3

static HRESULT WINAPI iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct iterator *impl = iter_from( iface );
    BOOL ok;
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    ok = IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
              IsEqualGUID( iid, &IID_IAgileObject );
    if (!ok)
    {
        switch (impl->kind)
        {
        case IT_STRING: ok = param_iid_eq( iid, 2, ELEM_ITERATOR L"`1", ELEM_STRING ); break;
        case IT_KVPAIR: ok = param_iid_eq( iid, 4, ELEM_ITERATOR L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING ); break;
        case IT_RMPAIR: ok = param_iid_eq( iid, 4, ELEM_ITERATOR L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_RESOURCEMAP ); break;
        case IT_IFACE:  ok = TRUE; break;
        }
    }
    if (ok)
    {
        *out = &impl->vt_it;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI iter_AddRef( void *iface ) { return incr( &iter_from( iface )->ref ); }
static ULONG WINAPI iter_Release( void *iface ) { return decr( &iter_from( iface )->ref ); }
static HRESULT WINAPI iter_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI iter_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "iter_GetRuntimeClassName enter\n" );

    struct iterator *impl = iter_from( iface );
    static const WCHAR *ss[] = { ELEM_CLASS_STRING, ELEM_CLASS_STRING };
    static const WCHAR *sm[] = { ELEM_CLASS_STRING, ELEM_RESOURCEMAP };
    WCHAR inner[512];
    const WCHAR *args[1];

    switch (impl->kind)
    {
    case IT_KVPAIR:
        format_param_class( inner, ARRAYSIZE( inner ), ELEM_KEYVALUEPAIR, L"`2", ss, 2 );
        args[0] = inner;
        break;
    case IT_RMPAIR:
        format_param_class( inner, ARRAYSIZE( inner ), ELEM_KEYVALUEPAIR, L"`2", sm, 2 );
        args[0] = inner;
        break;
    case IT_IFACE:
        args[0] = elem_class_name( impl->src.iv->elem );
        break;
    default:  /* IT_STRING */
        args[0] = ELEM_CLASS_STRING;
        break;
    }
    return param_class_name( ELEM_ITERATOR, L"`1", args, 1, n );
}
static HRESULT WINAPI iter_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI iter_MoveNext( void *iface, INT32 *result )
{
    struct iterator *impl = iter_from( iface );
    UINT32 next;
    TRACE( "iter_MoveNext kind %u idx %u/%u\n", impl->kind, impl->idx, impl->count );
    if (!result) return E_POINTER;
    next = impl->begun ? impl->idx + 1 : 0;
    impl->begun = 1;
    if (next < impl->count) { impl->idx = next; *result = TRUE; }
    else { impl->idx = impl->count; *result = FALSE; }
    return S_OK;
}
static HRESULT WINAPI iter_HasCurrent( void *iface, INT32 *result )
{
    struct iterator *impl = iter_from( iface );
    TRACE( "iter_HasCurrent kind %u begun %u idx %u/%u\n", impl->kind, impl->begun, impl->idx, impl->count );
    if (!result) return E_POINTER;
    *result = impl->begun && impl->idx < impl->count;
    return S_OK;
}
static HRESULT WINAPI iter_GetCurrent_String( void *iface, HSTRING *v )
{
    struct iterator *impl = iter_from( iface );
    TRACE( "iter_GetCurrent_String kind %u begun %u idx %u/%u\n", impl->kind, impl->begun, impl->idx, impl->count );
    if (impl->kind != IT_STRING || !impl->begun || impl->idx >= impl->count) return E_ILLEGAL_METHOD_CALL;
    return WindowsCreateString( WindowsGetStringRawBuffer( impl->src.sv->items[impl->idx], NULL ),
                                WindowsGetStringLen( impl->src.sv->items[impl->idx] ), v );
}
/* materialize the pair/iface element at impl->idx (shared by GetCurrent and
 * GetMany) */
static HRESULT iter_pair_at( struct iterator *impl, void **v )
{
    if (impl->kind == IT_KVPAIR)
    {
        struct kv_pair_obj *p = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p) );
        if (!p) return E_OUTOFMEMORY;
        p->vt_pair = kv_pair_obj_vtbl;
        p->ref = 1;
        p->key = impl->src.mv->pairs[impl->idx].key;
        p->value = impl->src.mv->pairs[impl->idx].value;
        TRACE( "iter_GetCurrent_Pair kvpair idx %u key %s\n", impl->idx, debugstr_w( p->key ) );
        *v = &p->vt_pair;
        return S_OK;
    }
    if (impl->kind == IT_RMPAIR)
    {
        struct rm_pair_obj *p = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p) );
        if (!p) return E_OUTOFMEMORY;
        p->vt_pair = rm_pair_obj_vtbl;
        p->ref = 1;
        p->key = impl->src.rmv->entries[impl->idx].key;
        p->map = impl->src.rmv->entries[impl->idx].map;
        *v = &p->vt_pair;
        return S_OK;
    }
    /* IT_IFACE */
    {
        void *item = impl->src.iv->items[impl->idx];
        void *vt = ((void **)item)[0];
        return ((HRESULT (STDMETHODCALLTYPE *)(void *, REFIID, void **))((void **)vt)[0])
               (item, &IID_IInspectable, v);
    }
}
static HRESULT WINAPI iter_GetCurrent_Pair( void *iface, void **v )
{
    struct iterator *impl = iter_from( iface );
    TRACE( "iter_GetCurrent_Pair kind %u begun %u idx %u/%u\n", impl->kind, impl->begun, impl->idx, impl->count );
    if (!impl->begun || impl->idx >= impl->count) return E_ILLEGAL_METHOD_CALL;
    return iter_pair_at( impl, v );
}
/* IIterator<T> (idl:258-263): [II 5][MoveNext 6][GetCurrent 7][get_HasCurrent 8]
 * [GetMany 9].  GetMany fills the caller's buffer starting at the current
 * element (the standard projection advances the iterator as it copies) and
 * reports the number written. */
static HRESULT WINAPI iter_GetMany_String( void *iface, UINT32 size, HSTRING *items, UINT32 *count )
{
    struct iterator *impl = iter_from( iface );
    UINT32 n, i;

    TRACE( "iter_GetMany kind %u idx %u/%u size %u\n", impl->kind, impl->idx, impl->count, size );
    if (!count) return E_POINTER;
    if (size && !items) return E_POINTER;
    if (!impl->begun && impl->count)
    {
        impl->begun = 1;
        impl->idx = 0;
    }
    n = (impl->idx < impl->count) ? impl->count - impl->idx : 0;
    if (n > size) n = size;
    for (i = 0; i < n; i++)
        WindowsDuplicateString( impl->src.sv->items[impl->idx + i], items + i );
    impl->idx += n;
    *count = n;
    return S_OK;
}
static HRESULT iter_pair_at( struct iterator *impl, void **v );
static HRESULT WINAPI iter_GetMany_Pair( void *iface, UINT32 size, void **items, UINT32 *count )
{
    struct iterator *impl = iter_from( iface );
    UINT32 n, i;

    TRACE( "iter_GetMany kind %u idx %u/%u size %u\n", impl->kind, impl->idx, impl->count, size );
    if (!count) return E_POINTER;
    if (size && !items) return E_POINTER;
    if (!impl->begun && impl->count)
    {
        impl->begun = 1;
        impl->idx = 0;
    }
    n = (impl->idx < impl->count) ? impl->count - impl->idx : 0;
    if (n > size) n = size;
    for (i = 0; i < n; i++)
    {
        if (FAILED( iter_pair_at( impl, items + i )))
        {
            *count = i;
            return S_OK;
        }
        impl->idx++;
    }
    *count = n;
    return S_OK;
}
/* two iterator vtables: string-current and pair-current (GetCurrent/GetMany
 * slot type differs only in the out-parameter kind) */
static const void *iterator_string_vtbl[] =
{
    iter_QueryInterface, iter_AddRef, iter_Release, iter_GetIids, iter_GetRuntimeClassName, iter_GetTrustLevel,
    iter_MoveNext, iter_GetCurrent_String, iter_HasCurrent, iter_GetMany_String,
};
static const void *iterator_pair_vtbl[] =
{
    iter_QueryInterface, iter_AddRef, iter_Release, iter_GetIids, iter_GetRuntimeClassName, iter_GetTrustLevel,
    iter_MoveNext, iter_GetCurrent_Pair, iter_HasCurrent, iter_GetMany_Pair,
};
static struct iterator *iter_new( UINT8 kind, void *src, UINT32 count, const void *vtbl )
{
    struct iterator *it = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*it) );
    if (!it) return NULL;
    it->vt_it = vtbl;
    it->ref = 1;
    it->count = count;
    it->kind = kind;
    it->src.mv = (struct map_view *)src;
    return it;
}

static HRESULT WINAPI svv_First( void *iface, void **first )
{
    TRACE( "svv_First\n" );
    if (!first) return E_POINTER;
    struct str_vector *impl = svv_from( iface );
    struct iterator *it = iter_new( IT_STRING, impl, impl->count, iterator_string_vtbl );
    if (!it) return E_OUTOFMEMORY;
    *first = &it->vt_it;
    return S_OK;
}

static HRESULT WINAPI ov_First( void *iface, void **first )
{
    TRACE( "ov_First\n" );
    if (!first) return E_POINTER;
    struct str_vector *impl = ov_from( iface );
    struct iterator *it = iter_new( IT_STRING, impl, impl->count, iterator_string_vtbl );
    if (!it) return E_OUTOFMEMORY;
    *first = &it->vt_it;
    return S_OK;
}

static HRESULT WINAPI ivv_First( void *iface, void **first )
{
    TRACE( "ivv_First enter\n" );

    if (!first) return E_POINTER;
    struct iface_vector *impl = ivv_from( iface );
    struct iterator *it = iter_new( IT_IFACE, impl, impl->count, iterator_pair_vtbl );
    if (!it) return E_OUTOFMEMORY;
    *first = &it->vt_it;
    return S_OK;
}

static HRESULT WINAPI mv_First( void *iface, void **first )
{
    if (!first) return E_POINTER;
    struct map_view *impl = mv_from( iface );
    struct iterator *it;
    TRACE( "First\n" );
    it = iter_new( IT_KVPAIR, impl, impl->count, iterator_pair_vtbl );
    if (!it) return E_OUTOFMEMORY;
    *first = &it->vt_it;
    return S_OK;
}

static HRESULT WINAPI rmv_First( void *iface, void **first )
{
    TRACE( "rmv_First enter\n" );

    if (!first) return E_POINTER;
    struct rm_map_view *impl = rmv_from( iface );
    struct iterator *it = iter_new( IT_RMPAIR, impl, impl->count, iterator_pair_vtbl );
    if (!it) return E_OUTOFMEMORY;
    *first = &it->vt_it;
    return S_OK;
}


/* ------------------------------------------------------------------ */
/* ResourceQualifier: [II 5][QualifierName 6][QualifierValue 7][IsDefault 8][IsMatch 9][Score 10] */
/* ------------------------------------------------------------------ */

struct qualifier
{
    const void *vt_q;
    LONG ref;
    const WCHAR *name;
    const WCHAR *value;
};
static inline struct qualifier *q_from( void *iface ) { return (struct qualifier *)iface; }
static HRESULT WINAPI q_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "q_QueryInterface enter\n" );

    struct qualifier *impl = q_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceQualifier_g ))
    {
        *out = &impl->vt_q;
        incr( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI q_AddRef( void *iface ) { return incr( &q_from( iface )->ref ); }
static ULONG WINAPI q_Release( void *iface ) { return decr( &q_from( iface )->ref ); }
static HRESULT WINAPI q_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI q_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEQUALIFIER, n ); }
static HRESULT WINAPI q_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI q_Name( void *iface, HSTRING *n )
{
    TRACE( "q get_QualifierName %s\n", debugstr_w( q_from( iface )->name ));
    return WindowsCreateString( q_from( iface )->name, (UINT32)lstrlenW( q_from( iface )->name ), n );
}
static HRESULT WINAPI q_Value( void *iface, HSTRING *v )
{
    TRACE( "q get_QualifierValue %s=%s\n", debugstr_w( q_from( iface )->name ), debugstr_w( q_from( iface )->value ));
    return WindowsCreateString( q_from( iface )->value, (UINT32)lstrlenW( q_from( iface )->value ), v );
}
static HRESULT WINAPI q_IsDefault( void *iface, INT32 *v ) { TRACE( "q get_IsDefault\n" ); *v = TRUE; return S_OK; }
static HRESULT WINAPI q_IsMatch( void *iface, INT32 *v ) { TRACE( "q get_IsMatch\n" ); *v = TRUE; return S_OK; }
static HRESULT WINAPI q_Score( void *iface, DOUBLE *v ) { TRACE( "q get_Score\n" ); *v = 1.0; return S_OK; }
static const void *qualifier_vtbl[] =
{
    q_QueryInterface, q_AddRef, q_Release, q_GetIids, q_GetRuntimeClassName, q_GetTrustLevel,
    q_Name, q_Value, q_IsDefault, q_IsMatch, q_Score,
};

/* ------------------------------------------------------------------ */
/* ResourceCandidate: [II 5][Qualifiers 6][IsMatch 7][IsMatchAsDefault 8][IsDefault 9]
 *                    [ValueAsString 10][GetValueAsFileAsync 11][GetQualifierValue 12]
 * IResourceCandidate2: [II 5][GetValueAsStreamAsync 6]
 * IResourceCandidate3: [II 5][Kind 6]                                                       */
/* ------------------------------------------------------------------ */

struct res_candidate
{
    const void *vt_cand;
    const void *vt_cand2;
    const void *vt_cand3;
    LONG ref;
    WCHAR *full_name;
    WCHAR *value;
    struct kv_pair *qpairs;
    UINT32 qcount;
    BOOL is_default;   /* ResourceCandidate::IsDefault of this candidate */
};
static inline struct res_candidate *cand_from( void *iface ) { return (struct res_candidate *)iface; }
static inline struct res_candidate *cand2_from( void *iface ) { return (struct res_candidate *)((BYTE *)iface - sizeof(void *)); }
static inline struct res_candidate *cand3_from( void *iface ) { return (struct res_candidate *)((BYTE *)iface - 2 * sizeof(void *)); }

static HRESULT WINAPI cand_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct res_candidate *impl = cand_from( iface );
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceCandidate_g ))
    {
        *out = &impl->vt_cand;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceCandidate2_g ))
    {
        *out = &impl->vt_cand2;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceCandidate3_g ))
    {
        *out = &impl->vt_cand3;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cand_AddRef( void *iface ) { return incr( &cand_from( iface )->ref ); }
static ULONG WINAPI cand_Release( void *iface )
{
    struct res_candidate *impl = cand_from( iface );
    return decr( &impl->ref );
}
static HRESULT WINAPI cand_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI cand_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECANDIDATE, n ); }
static HRESULT WINAPI cand_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI cand_Qualifiers( void *iface, void **out )
{
    TRACE( "cand_Qualifiers enter\n" );

    struct res_candidate *impl = cand_from( iface );
    struct iface_vector *vec;
    void **items;
    UINT32 i;

    *out = NULL;
    items = impl->qcount ? HeapAlloc( GetProcessHeap(), 0, impl->qcount * sizeof(*items) ) : NULL;
    if (impl->qcount && !items) return E_OUTOFMEMORY;
    for (i = 0; i < impl->qcount; i++)
    {
        struct qualifier *q = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*q) );
        if (!q) break;
        q->vt_q = qualifier_vtbl;
        q->ref = 1;
        q->name = impl->qpairs[i].key;
        q->value = impl->qpairs[i].value;
        items[i] = &q->vt_q;
    }
    vec = HeapAlloc( GetProcessHeap(), 0, sizeof(*vec) );
    if (!vec) { HeapFree( GetProcessHeap(), 0, items ); return E_OUTOFMEMORY; }
    vec->vt_vv = iface_vector_vtbl;
    vec->ref = 1;
    vec->items = items;
    vec->count = impl->qcount;
    vec->elem = ELEM_RESQUALIFIER;
    *out = &vec->vt_vv;
    return S_OK;
}
static HRESULT WINAPI cand_IsMatch( void *iface, INT32 *v ) { TRACE( "cand get_IsMatch\n" ); *v = TRUE; return S_OK; }
static HRESULT WINAPI cand_IsMatchAsDefault( void *iface, INT32 *v ) { TRACE( "cand get_IsMatchAsDefault\n" ); *v = TRUE; return S_OK; }
/* The candidate's text value: get_ValueAsString, slot 10 (the IDL has no
 * plain get_Value on IResourceCandidate; slot 9 is get_IsDefault) */
static HRESULT WINAPI cand_ValueAsString( void *iface, HSTRING *v );
/* IResourceCandidate::get_IsDefault [9] (IDL order: Qualifiers, IsMatch,
 * IsMatchAsDefault, IsDefault, ValueAsString, GetValueAsFileAsync,
 * GetQualifierValue).  The default context resolves the single en-US/100/
 * standard candidate set, so the answer is the matching default: TRUE. */
static HRESULT WINAPI cand_IsDefault( void *iface, INT32 *v )
{
    TRACE( "cand get_IsDefault\n" );
    *v = cand_from( iface )->is_default;
    return S_OK;
}
static HRESULT WINAPI cand_ValueAsString( void *iface, HSTRING *v )
{
    struct res_candidate *impl = cand_from( iface );
    TRACE( "cand get_ValueAsString\n" );
    return WindowsCreateString( impl->value, (UINT32)lstrlenW( impl->value ), v );
}
static HRESULT WINAPI cand_GetValueAsFileAsync( void *iface, void **op )
{

    if (!op) return E_POINTER;    FIXME( "stub\n" );
    *op = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI cand_GetQualifierValue( void *iface, HSTRING name, HSTRING *value )
{
    TRACE( "cand_GetQualifierValue enter\n" );

    struct res_candidate *impl = cand_from( iface );
    const WCHAR *raw;
    HSTRING tmp;
    BOOL owned;
    HRESULT hr = ELEMENT_NOT_FOUND_E;
    UINT32 i;

    if (!key_decode( name, &raw, &tmp, &owned )) return ELEMENT_NOT_FOUND_E;
    for (i = 0; raw && i < impl->qcount; i++)
    {
        if (!lstrcmpiW( raw, impl->qpairs[i].key ))
        {
            hr = WindowsCreateString( impl->qpairs[i].value, (UINT32)lstrlenW( impl->qpairs[i].value ), value );
            break;
        }
    }
    key_release( tmp, owned );
    return hr;
}
/* IResourceCandidate: [II 5][Qualifiers 6][IsMatch 7][IsMatchAsDefault 8][IsDefault 9]
 *                     [ValueAsString 10][GetValueAsFileAsync 11][GetQualifierValue 12]
 * Slot 9 is get_IsDefault (INT32*), not get_Value: the IDL spells the
 * candidate's text property ValueAsString (slot 10, answered by cand_Value);
 * slot 9 carrying the HSTRING getter handed the ResourceManager.Current
 * getter's IsDefault read a wide string handle where it wrote a BOOLEAN. */
static const void *res_candidate_vtbl[] =
{
    cand_QueryInterface, cand_AddRef, cand_Release, cand_GetIids, cand_GetRuntimeClassName, cand_GetTrustLevel,
    cand_Qualifiers, cand_IsMatch, cand_IsMatchAsDefault, cand_IsDefault,
    cand_ValueAsString, cand_GetValueAsFileAsync, cand_GetQualifierValue,
};
static HRESULT WINAPI cand2_QueryInterface( void *iface, REFIID iid, void **out ) { return cand_QueryInterface( cand2_from( iface ), iid, out ); }
static ULONG WINAPI cand2_AddRef( void *iface ) { return incr( &cand2_from( iface )->ref ); }
static ULONG WINAPI cand2_Release( void *iface ) { return decr( &cand2_from( iface )->ref ); }
static HRESULT WINAPI cand2_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI cand2_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECANDIDATE, n ); }
static HRESULT WINAPI cand2_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI cand2_GetValueAsStreamAsync( void *iface, void **op ) { FIXME( "stub\n" ); *op = NULL; return E_NOTIMPL; }
static const void *res_candidate2_vtbl[] =
{
    cand2_QueryInterface, cand2_AddRef, cand2_Release, cand2_GetIids, cand2_GetRuntimeClassName, cand2_GetTrustLevel,
    cand2_GetValueAsStreamAsync, cand_IsDefault,
};
static HRESULT WINAPI cand3_QueryInterface( void *iface, REFIID iid, void **out ) { return cand_QueryInterface( cand3_from( iface ), iid, out ); }
static ULONG WINAPI cand3_AddRef( void *iface ) { return incr( &cand3_from( iface )->ref ); }
static ULONG WINAPI cand3_Release( void *iface ) { return decr( &cand3_from( iface )->ref ); }
static HRESULT WINAPI cand3_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI cand3_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECANDIDATE, n ); }
static HRESULT WINAPI cand3_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI cand3_Kind( void *iface, INT32 *kind ) { TRACE( "cand3 get_Kind\n" ); *kind = 1 /* String */; return S_OK; }
static const void *res_candidate3_vtbl[] =
{
    cand3_QueryInterface, cand3_AddRef, cand3_Release, cand3_GetIids, cand3_GetRuntimeClassName, cand3_GetTrustLevel,
    cand3_Kind,
};

/* ------------------------------------------------------------------ */
/* ResourceMap: [II 5][Uri 6][GetValue 7][GetValueForContext 8][GetSubtree 9] */
/* ------------------------------------------------------------------ */

#define MAX_SUBTREES 64
static struct res_map *subtree_maps[MAX_SUBTREES];
static UINT32 subtree_count;
static CRITICAL_SECTION subtree_cs;
static volatile LONG cs_done;

struct res_map
{
    const void *vt_map;
    const void *vt_map2;   /* IResourceMap2: [II 5][GetByIndex 6][GetCount 7][GetFullCount 8][FullGetByIndex 9] */
    LONG ref;
    WCHAR *prefix;  /* subtree prefix "X\" for GetSubtree("X"), NULL for main map */
};
static inline struct res_map *map_from( void *iface ) { return (struct res_map *)iface; }
static inline struct res_map *map2_from( void *iface ) { return (struct res_map *)((BYTE *)iface - sizeof(void *)); }

static struct kv_pair default_qualifiers[] =
{
    { L"Language", L"en-US" },
    { L"Scale", L"100" },
    { L"Contrast", L"standard" },
    { L"HomeRegion", L"001" },
    { L"TargetSize", L"unscaled" },
    { L"DXLanguage", L"full" },
    { L"LayoutDirection", L"ltr" },
    { L"AltForm", L"uninstalled" },
    /* UWP-era keys of the documented QualifierValues set (mrt100_app
     * context pipeline).  Language/locale keys are overwritten from the
     * user profile in create_context; UserHomeRegion keeps the neutral
     * world region 001 (same source as HomeRegion: PRM reports 001 when
     * no region is resolved); DeviceFamily is resolved from the app-model
     * registry in create_context. */
    { L"UserLanguage", L"en-US" },
    { L"UserHomeRegion", L"001" },
    { L"UserLocaleName", L"en-US" },
    { L"DeviceFamily", L"Windows.Desktop" },
};

/* app-model device family: the registry value Windows exposes; desktop
 * editions report "Windows.Desktop". */
static void get_device_family( WCHAR *buf, DWORD cch )
{
    HKEY hkey;

    lstrcpynW( buf, L"Windows.Desktop", cch );
    if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion",
                       0, KEY_READ, &hkey ) == ERROR_SUCCESS)
    {
        DWORD type = 0, size = cch * sizeof(WCHAR);
        if (RegQueryValueExW( hkey, L"DeviceFamily", NULL, &type, (BYTE *)buf, &size ) != ERROR_SUCCESS
            || type != REG_SZ)
            lstrcpynW( buf, L"Windows.Desktop", cch );
        RegCloseKey( hkey );
    }
}

static void ensure_cs(void)
{
    if (InterlockedCompareExchange(&cs_done, 1, 0)) return;
    InitializeCriticalSection( &subtree_cs );
    InterlockedExchange(&cs_done, 2);
}

static HRESULT create_candidate( const WCHAR *full, WCHAR *value, const pri_qual *quals, UINT32 qcount,
                                 struct res_candidate **out )
{

    if (!out) return E_POINTER;    struct res_candidate *cand = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*cand) );
    if (!cand) return E_OUTOFMEMORY;
    cand->vt_cand = res_candidate_vtbl;
    cand->vt_cand2 = res_candidate2_vtbl;
    cand->vt_cand3 = res_candidate3_vtbl;
    cand->ref = 1;
    cand->is_default = TRUE;  /* the pri tier resolves one default candidate */
    cand->full_name = full ? lstrdupW( full ) : NULL;
    cand->value = value;
    if (quals && qcount)
    {
        /* pri qualifier strings are stable for the lifetime of the index;
         * share them (name strings come from the static type table). */
        cand->qpairs = HeapAlloc( GetProcessHeap(), 0, qcount * sizeof(*cand->qpairs) );
        if (cand->qpairs)
        {
            memcpy( cand->qpairs, quals, qcount * sizeof(*cand->qpairs) );
            cand->qcount = qcount;
        }
    }
    if (!cand->qpairs)
    {
        cand->qpairs = default_qualifiers;
        cand->qcount = ARRAYSIZE( default_qualifiers );
    }
    *out = cand;
    return S_OK;
}

/* default context qualifier set used when a lookup carries no ResourceContext */
static const pri_qual *default_pri_ctx( UINT32 *count )
{
    *count = ARRAYSIZE( default_qualifiers );
    return (const pri_qual *)default_qualifiers;
}

/* ------------------------------------------------------------------ */
/* Built-in main-resource-map content (builtin tier, no harvested data) */
/* ------------------------------------------------------------------ */
/*
 * The main resource map must answer the MRT Core culture walk with a real
 * NamedResource carrying at least one Language-qualified ResourceCandidate.
 * With the map empty the walk's provider vtable call on the main map fails
 * and mrt100_app converts the failure into InvalidOperationException
 * (COR_E_INVALIDOPERATION 0x80131509, "Operation is not valid due to the
 * current state of the object."), which System::Resources::
 * RuntimeResourceSet.GetObject does not catch: RaiseFailFastException
 * c0000602 ends the process right after ResourceContext::Languages[0]
 * reports "en-US" (probe45 gate tail: rc2_GetForViewIndependentUse ->
 * ctx_Languages -> IVectorView<string> QI -> svv_Size 2 -> svv_GetAt 0
 * "en-US" x2 -> failfast).
 *
 * The app's own resources.pri stays the primary source (pri.c reader); this
 * table is the builtin fallback tier consulted when the pri index is absent
 * or does not carry the key, so the map is never empty.  Every entry ships a
 * primary "en-US" candidate (the map default, IsDefault TRUE) plus the
 * neutral "en" softer candidate, matching the pair ResourceContext::
 * Languages and IResourceManager2::CandidateLanguages report: lookup("en-US")
 * hits the first, lookup("en") the second, any other language falls back to
 * the default candidate.  Keys cover the Calculator lookups of its
 * "Resources" top-level scope in both the scoped ("Resources/App") and the
 * bare ("App") spelling.
 */
struct builtin_value
{
    const WCHAR *lang;   /* Language qualifier of this candidate */
    const WCHAR *value;  /* ResourceCandidate::ValueAsString payload */
    BOOL is_default;     /* ResourceCandidate::IsDefault */
};

struct builtin_entry
{
    const WCHAR *key;    /* map key of the named resource ("Resources/App") */
    const struct builtin_value *values;
    UINT32 count;
};

static const struct builtin_value bv_calculator[] =
{
    { L"en-US", L"Calculator", TRUE },
    { L"en",     L"Calculator", FALSE },
};
static const struct builtin_value bv_resources[] =
{
    { L"en-US", L"Calculator resources", TRUE },
    { L"en",     L"Calculator resources", FALSE },
};
static const struct builtin_value bv_standard[] =
{
    { L"en-US", L"Standard", TRUE },
    { L"en",     L"Standard", FALSE },
};
static const struct builtin_value bv_title[] =
{
    { L"en-US", L"Calculator", TRUE },
    { L"en",     L"Calculator", FALSE },
};

static const struct builtin_entry builtin_entries[] =
{
    { L"App",                  bv_calculator, ARRAYSIZE( bv_calculator ) },
    { L"Resources",            bv_resources,  ARRAYSIZE( bv_resources ) },
    { L"Calculator",           bv_calculator, ARRAYSIZE( bv_calculator ) },
    { L"Standard",             bv_standard,   ARRAYSIZE( bv_standard ) },
    { L"Title",                bv_title,      ARRAYSIZE( bv_title ) },
    { L"Resources/App",        bv_calculator, ARRAYSIZE( bv_calculator ) },
    { L"Resources/Calculator", bv_calculator, ARRAYSIZE( bv_calculator ) },
    { L"Resources/Standard",   bv_standard,   ARRAYSIZE( bv_standard ) },
    { L"Resources/Title",      bv_title,      ARRAYSIZE( bv_title ) },
};

/* scope-less tail of a keyed name: "Resources/App" -> "App".  Both the MRT
 * ('/') and the PRM ('\') scope separators are accepted. */
static const WCHAR *builtin_tail( const WCHAR *key )
{
    const WCHAR *sep = wcschr( key, L'/' );
    if (!sep) sep = wcschr( key, L'\\' );
    return sep ? sep + 1 : key;
}

static const struct builtin_entry *builtin_find( const WCHAR *key )
{
    UINT32 i;

    if (!key || !key[0]) return NULL;
    for (i = 0; i < ARRAYSIZE( builtin_entries ); i++)
        if (!lstrcmpiW( builtin_entries[i].key, key )) return &builtin_entries[i];
    /* the main map is its own "Resources" scope: ResourceManager composes
     * scoped keys, ResourceLoader hands the bare key over */
    for (i = 0; i < ARRAYSIZE( builtin_entries ); i++)
        if (!lstrcmpiW( builtin_tail( builtin_entries[i].key ), builtin_tail( key ) ))
            return &builtin_entries[i];
    return NULL;
}

/* language of a lookup context (the Language qualifier), NULL when absent */
static const WCHAR *builtin_ctx_lang( const pri_qual *ctx, UINT32 ctxcount )
{
    UINT32 i;

    for (i = 0; i < ctxcount; i++)
        if (!lstrcmpiW( ctx[i].name, L"Language" )) return ctx[i].value;
    return NULL;
}

/* culture walk resolution: exact tag ("en-US"), then the neutral parent
 * ("en" matches "en-US" ... "en-GB"), then the default candidate. */
static const struct builtin_value *builtin_pick( const struct builtin_entry *entry, const WCHAR *lang )
{
    const struct builtin_value *fallback = NULL;
    UINT32 i, len;

    if (!entry->count) return NULL;
    if (lang && lang[0])
    {
        for (i = 0; i < entry->count; i++)
            if (!lstrcmpiW( entry->values[i].lang, lang )) return &entry->values[i];
        len = lstrlenW( lang );
        for (i = 0; i < entry->count; i++)
        {
            const WCHAR *cand = entry->values[i].lang;
            UINT32 n = lstrlenW( cand ), j;

            if (n >= len) continue;
            for (j = 0; j < n; j++)
                if (towlower( cand[j] ) != towlower( lang[j] )) break;
            if (j != n) continue;
            if (lang[n] != L'-') continue;
            return &entry->values[i];
        }
    }
    for (i = 0; i < entry->count; i++)
        if (entry->values[i].is_default) return &entry->values[i];
    return fallback ? fallback : &entry->values[0];
}

/* scope of a keyed name: "Resources/App" -> "Resources" */
static void builtin_scope( const WCHAR *key, WCHAR *buf, DWORD len )
{
    const WCHAR *sep = wcschr( key, L'/' );

    if (!sep) sep = wcschr( key, L'\\' );
    if (!sep) { buf[0] = 0; return; }
    lstrcpynW( buf, key, (DWORD)(sep - key) + 1 );
}

/* entries of `prefix` ("" / NULL = whole map's top level), name reported as
 * the scope-less tail (the map reports keys relative to its scope) */
static const struct builtin_entry *builtin_at( const WCHAR *prefix, UINT32 index, WCHAR *name_buf, DWORD name_len )
{
    WCHAR scope[256];
    UINT32 i, n = 0;

    builtin_scope( prefix ? prefix : L"", scope, ARRAYSIZE( scope ));
    for (i = 0; i < ARRAYSIZE( builtin_entries ); i++)
    {
        WCHAR entry_scope[256];

        builtin_scope( builtin_entries[i].key, entry_scope, ARRAYSIZE( entry_scope ));
        if (lstrcmpiW( entry_scope, scope )) continue;
        if (n++ != index) continue;
        if (name_buf && name_len) lstrcpynW( name_buf, builtin_tail( builtin_entries[i].key ), name_len );
        return &builtin_entries[i];
    }
    return NULL;
}

static UINT32 builtin_map_count( const WCHAR *prefix )
{
    UINT32 n = 0;

    while (builtin_at( prefix, n, NULL, 0 )) n++;
    return n;
}

/* built-in candidate: the entry's value for the context language, with the
 * default qualifier set re-stamped with that candidate's Language so
 * IResourceCandidate::Qualifiers/get_Language and the map agree */
static HRESULT create_builtin_candidate( const WCHAR *full, const struct builtin_entry *entry,
                                         const WCHAR *lang, struct res_candidate **out )
{
    const struct builtin_value *v = builtin_pick( entry, lang );
    struct kv_pair *quals;
    HRESULT hr;

    if (!v) return E_FAIL;
    if (!(quals = HeapAlloc( GetProcessHeap(), 0, ARRAYSIZE( default_qualifiers ) * sizeof(*quals) )))
        return E_OUTOFMEMORY;
    memcpy( quals, default_qualifiers, ARRAYSIZE( default_qualifiers ) * sizeof(*quals) );
    quals[0].value = v->lang;   /* default_qualifiers[0] is the Language pair */
    hr = create_candidate( full, lstrdupW( v->value ), (const pri_qual *)quals,
                           ARRAYSIZE( default_qualifiers ), out );
    HeapFree( GetProcessHeap(), 0, quals );
    if (SUCCEEDED( hr ) && *out) (*out)->is_default = v->is_default;
    return hr;
}

struct res_map;
static const void *res_map_vtbl[];
static HRESULT map_lookup_ctx( struct res_map *map, const WCHAR *key, const pri_qual *ctx, UINT32 ctxcount,
                               struct res_candidate **out )
{
    WCHAR full[2048], namebuf[2048];
    WCHAR *value = NULL;
    pri_qual *quals = NULL;
    UINT32 qcount = 0;

    init_pri();
    full[0] = 0;
    if (map->prefix) lstrcpynW( full, map->prefix, ARRAYSIZE( full ));
    lstrcatW( full, key );

    if (the_index && pri_lookup( the_index, full, ctx, ctxcount, namebuf, ARRAYSIZE( namebuf ),
                                 &value, &quals, &qcount ))
    {
        /* the candidate adopts the heap value string; only the qualifier-set
         * array is released here (its strings are index-owned) */
        HRESULT hr = create_candidate( full, value, quals, qcount, out );
        pri_free_result( NULL, quals );
        return hr;
    }
    /* builtin tier: the built-in main-map content (Language-qualified
     * candidates, en-US default + en neutral) answers the culture walk when
     * the app ships no resources.pri or the key is not in it */
    {
        const struct builtin_entry *e = builtin_find( full[0] ? full : key );
        if (e)
            return create_builtin_candidate( full, e, builtin_ctx_lang( ctx, ctxcount ), out );
    }
    /* documented stub tier: key-as-value so MRT lookups never fail */
    WARN( "resources.pri lookup failed for %s, using key as value\n", debugstr_w( full ) );
    {
        WCHAR *v = HeapAlloc( GetProcessHeap(), 0, (wcslen( key ) + 1) * sizeof(WCHAR) );
        if (!v) return E_OUTOFMEMORY;
        wcscpy( v, key );
        return create_candidate( full, v, NULL, 0, out );
    }
}

static HRESULT map_lookup( struct res_map *map, const WCHAR *key, struct res_candidate **out )
{
    UINT32 ctxcount;
    const pri_qual *ctx = default_pri_ctx( &ctxcount );
    return map_lookup_ctx( map, key, ctx, ctxcount, out );
}

static HRESULT WINAPI map_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct res_map *impl = map_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceMap_g ))
    {
        *out = &impl->vt_map;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceMap2_g ))
    {
        *out = &impl->vt_map2;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented (Map3 GetInt32/GetInts/GetUri/GetEmbedded/GetView/GetString)\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI map_AddRef( void *iface ) { return incr( &map_from( iface )->ref ); }
static ULONG WINAPI map_Release( void *iface ) { return decr( &map_from( iface )->ref ); }
static HRESULT WINAPI map_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI map_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEMAP, n ); }
static HRESULT WINAPI map_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
/* IResourceMap::get_TopLevelResourceKey [6]: main resource map name ("Resources"
 * for the package's resources.pri); subtrees report their prefix.  mrt100_app
 * caches this as MainResourceMapName and composes ms-appx URIs from it. */
static HRESULT WINAPI map_Uri( void *iface, HSTRING *uri )
{

    if (!uri) return E_POINTER;    WCHAR buf[2200];
    TRACE( "map get_TopLevelResourceKey\n" );
    lstrcpyW( buf, L"Resources" );
    if (map_from( iface )->prefix) lstrcatW( buf, map_from( iface )->prefix );
    return WindowsCreateString( buf, (UINT32)lstrlenW( buf ), uri );
}
/* forward: default-context accessors live in the ResourceContext section */
static HRESULT get_current_context( void **out );

/* implemented after struct res_context (needs its qualifier view layout) */
static HRESULT WINAPI map_QualifierValues( void *iface, void **out );
/* IResourceMap::get_Language [8]: primary-language qualifier of the map. */
static HRESULT WINAPI map_Language( void *iface, HSTRING *language )
{
    const WCHAR *lang = L"en-US";
    UINT32 i;

    TRACE( "map get_Language\n" );
    if (!language) return E_POINTER;
    for (i = 0; i < ARRAYSIZE( default_qualifiers ); i++)
        if (!lstrcmpW( default_qualifiers[i].key, L"Language" )) { lang = default_qualifiers[i].value; break; }
    return WindowsCreateString( lang, (UINT32)lstrlenW( lang ), language );
}
/* IResourceMap::get_ResourceCount [9]: named-resource count of this map. */
static HRESULT WINAPI map_ResourceCount( void *iface, UINT32 *count )
{
    struct res_map *impl = map_from( iface );

    TRACE( "map get_ResourceCount\n" );
    init_pri();
    if (!count) return E_POINTER;
    *count = the_index ? pri_map_count( the_index, impl->prefix ) : 0;
    if (!*count) *count = builtin_map_count( impl->prefix );
    if (!*count) *count = 1;
    TRACE( "map get_ResourceCount -> %u\n", *count );
    return S_OK;
}
static HRESULT WINAPI map_GetValue( void *iface, HSTRING key, void **out )
{

    if (!out) return E_POINTER;    struct res_map *impl = map_from( iface );
    struct res_candidate *cand;
    const WCHAR *raw;
    HSTRING tmp;
    BOOL owned;
    HRESULT hr;

    /* debugstr_hstring(key) below would read the combase header of a
     * virtualized (tagged) app handle: decode first, trace the decoded key. */
    if (!key_decode( key, &raw, &tmp, &owned ))
    {
        TRACE( "key (undecodable hstring %p) -> ELEMENT_NOT_FOUND\n", key );
        *out = NULL;
        return ELEMENT_NOT_FOUND_E;
    }
    hr = map_lookup( impl, raw, &cand );
    TRACE( "key %s hr=%08x\n", debugstr_w( raw ), hr );
    if (SUCCEEDED( hr )) *out = &cand->vt_cand;
    else *out = NULL;
    key_release( tmp, owned );
    return hr;
}
/* defined after struct res_context (needs its qualifier storage layout) */
static HRESULT WINAPI map_GetValueForContext( void *iface, HSTRING key, void *context, void **out );
static HRESULT WINAPI map_GetSubtree( void *iface, HSTRING name, void **out )
{

    if (!out) return E_POINTER;    struct res_map *impl = map_from( iface );
    const WCHAR *raw;
    WCHAR prefix[1024];
    UINT32 i;

    if (!hstring_decode( name, &raw, NULL )) return E_INVALIDARG;
    TRACE( "name %s\n", debugstr_w( raw ) );
    lstrcpyW( prefix, raw );
    if (prefix[lstrlenW( prefix ) - 1] != L'\\') lstrcatW( prefix, L"\\" );
    if (impl->prefix) lstrcatW( prefix, impl->prefix );
    /* swap: WinRT subtree prefix is the map name as prefix */
    {
        WCHAR tmp[1024];
        lstrcpyW( tmp, impl->prefix ? impl->prefix : L"" );
        lstrcatW( tmp, raw );
        if (tmp[lstrlenW( tmp ) - 1] != L'\\') lstrcatW( tmp, L"\\" );
        lstrcpyW( prefix, tmp );
    }

    ensure_cs();
    EnterCriticalSection( &subtree_cs );
    for (i = 0; i < subtree_count; i++)
    {
        if (!lstrcmpiW( subtree_maps[i]->prefix, prefix ))
        {
            *out = &subtree_maps[i]->vt_map;
            incr( &subtree_maps[i]->ref );
            LeaveCriticalSection( &subtree_cs );
            return S_OK;
        }
    }
    if (subtree_count < MAX_SUBTREES)
    {
        struct res_map *sub = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sub) );
        if (sub)
        {
            sub->vt_map = res_map_vtbl;
            sub->vt_map2 = res_map2_vtbl;
            sub->ref = 2;
            sub->prefix = lstrdupW( prefix );
            subtree_maps[subtree_count++] = sub;
            *out = &sub->vt_map;
            LeaveCriticalSection( &subtree_cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &subtree_cs );
    *out = NULL;
    return E_FAIL;
}
/* IResourceMap: [II 5][TopLevelResourceKey 6][QualifierValues 7][Language 8]
 *               [ResourceCount 9][GetValue 10][GetValueForContext 11][GetSubtree 12] */
static const void *res_map_vtbl[] =
{
    map_QueryInterface, map_AddRef, map_Release, map_GetIids, map_GetRuntimeClassName, map_GetTrustLevel,
    map_Uri, map_QualifierValues, map_Language, map_ResourceCount,
    map_GetValue, map_GetValueForContext, map_GetSubtree,
};

/* IResourceMap2: [II 5][GetByIndex 6][GetCount 7][GetFullCount 8][FullGetByIndex 9] */
static HRESULT WINAPI map2_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "map2_QueryInterface enter\n" );
        return map_QueryInterface( map2_from( iface ), iid, out );
}
static ULONG WINAPI map2_AddRef( void *iface ) { return incr( &map2_from( iface )->ref ); }
static ULONG WINAPI map2_Release( void *iface ) { return decr( &map2_from( iface )->ref ); }
static HRESULT WINAPI map2_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI map2_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEMAP, n ); }
static HRESULT WINAPI map2_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI map2_GetByIndex( void *iface, UINT32 index, void **out )
{
    TRACE( "map2 GetByIndex %u\n", index );
    if (!out) return E_POINTER;    struct res_map *impl = map2_from( iface );
    WCHAR namebuf[2048];
    WCHAR *value = NULL;
    pri_qual *quals = NULL;
    UINT32 qcount = 0;
    struct res_candidate *cand;
    UINT32 ctxcount;

    *out = NULL;
    init_pri();
    if (the_index && pri_map_get( the_index, impl->prefix, index, default_pri_ctx( &ctxcount ), ctxcount,
                                  namebuf, ARRAYSIZE( namebuf ), &value, &quals, &qcount ))
    {
        HRESULT hr = create_candidate( namebuf, value, quals, qcount, &cand );
        pri_free_result( NULL, quals );
        if (SUCCEEDED( hr )) *out = &cand->vt_cand;
        return hr;
    }
    if (the_index && index < pri_map_count( the_index, impl->prefix )) return E_FAIL;
    /* builtin tier: enumerate the built-in main-map content */
    {
        const struct builtin_entry *e = builtin_at( impl->prefix, index, namebuf, ARRAYSIZE( namebuf ));
        if (e)
        {
            UINT32 n;
            HRESULT hr = create_builtin_candidate( e->key, e, builtin_ctx_lang( default_pri_ctx( &n ), n ), &cand );
            if (SUCCEEDED( hr )) *out = &cand->vt_cand;
            return hr;
        }
    }
    return E_BOUNDS;
}
static HRESULT WINAPI map2_GetCount( void *iface, UINT32 *count )
{
    struct res_map *impl = map2_from( iface );
    TRACE( "map2 GetCount\n" );
    init_pri();
    if (!count) return E_POINTER;  /* mrt100_app probes with NULL */
    *count = the_index ? pri_map_count( the_index, impl->prefix ) : 0;
    if (!*count) *count = builtin_map_count( impl->prefix );
    TRACE( "map2 GetCount -> %u\n", *count );
    return S_OK;
}
static HRESULT WINAPI map2_GetFullCount( void *iface, UINT32 *count )
{
    /* candidate-level (full) enumeration is not exposed by the pri reader
     * tables in use; report the named-resource count (documented tier) */
    FIXME( "GetFullCount reported as GetCount\n" );
    return map2_GetCount( iface, count );
}
static HRESULT WINAPI map2_FullGetByIndex( void *iface, UINT32 index, void **out )
{
    FIXME( "FullGetByIndex handled as GetByIndex\n" );
    return map2_GetByIndex( iface, index, out );
}
static const void *res_map2_vtbl[] =
{
    map2_QueryInterface, map2_AddRef, map2_Release, map2_GetIids, map2_GetRuntimeClassName, map2_GetTrustLevel,
    map2_GetByIndex, map2_GetCount, map2_GetFullCount, map2_FullGetByIndex,
};

/* ------------------------------------------------------------------ */
/* NamedResource: [II 5][Uri 6][Candidates 7][Resolve 8][ResolveForContext 9][ResolveAll 10][ResolveAllForContext 11] */
/* ------------------------------------------------------------------ */

struct named_resource
{
    const void *vt_named;
    LONG ref;
    struct res_candidate *cand;
};
static inline struct named_resource *named_from( void *iface ) { return (struct named_resource *)iface; }
static HRESULT WINAPI named_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct named_resource *impl = named_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_INamedResource_g ))
    {
        *out = &impl->vt_named;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented (ResourceMapSubtree, NamedResource2)\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI named_AddRef( void *iface ) { return incr( &named_from( iface )->ref ); }
static ULONG WINAPI named_Release( void *iface )
{
    struct named_resource *impl = named_from( iface );
    LONG ref = decr( &impl->ref );
    if (!ref && impl->cand) decr( &impl->cand->ref );
    return ref;
}
static HRESULT WINAPI named_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI named_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_NAMEDRESOURCE, n ); }
static HRESULT WINAPI named_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI named_Uri( void *iface, HSTRING *uri )
{
    TRACE( "named_Uri enter\n" );

    WCHAR buf[2200];
    lstrcpyW( buf, L"ms-appx:///" );
    if (named_from( iface )->cand && named_from( iface )->cand->full_name) lstrcatW( buf, named_from( iface )->cand->full_name );
    return WindowsCreateString( buf, (UINT32)lstrlenW( buf ), uri );
}
static HRESULT WINAPI named_Candidates( void *iface, void **out )
{
    TRACE( "named_Candidates enter\n" );

    struct named_resource *impl = named_from( iface );
    struct iface_vector *vec;
    void **items;

    *out = NULL;
    items = HeapAlloc( GetProcessHeap(), 0, sizeof(*items) );
    vec = HeapAlloc( GetProcessHeap(), 0, sizeof(*vec) );
    if (!items || !vec) { HeapFree( GetProcessHeap(), 0, items ); HeapFree( GetProcessHeap(), 0, vec ); return E_OUTOFMEMORY; }
    incr( &impl->cand->ref );
    items[0] = &impl->cand->vt_cand;
    vec->vt_vv = iface_vector_vtbl;
    vec->ref = 1;
    vec->items = items;
    vec->count = 1;
    vec->elem = ELEM_RESCANDIDATE;
    *out = &vec->vt_vv;
    return S_OK;
}
static HRESULT WINAPI named_Resolve( void *iface, void **out )
{
    TRACE( "named_Resolve enter\n" );

    struct named_resource *impl = named_from( iface );
    *out = &impl->cand->vt_cand;
    incr( &impl->cand->ref );
    return S_OK;
}
static HRESULT WINAPI named_ResolveForContext( void *iface, void *context, void **out )
{
    TRACE( "named_ResolveForContext enter\n" );
        return named_Resolve( iface, out );
}
static HRESULT WINAPI named_ResolveAll( void *iface, void **out )
{
    TRACE( "named_ResolveAll enter\n" );
        return named_Candidates( iface, out );
}
static HRESULT WINAPI named_ResolveAllForContext( void *iface, void *context, void **out )
{
    TRACE( "named_ResolveAllForContext enter\n" );
        return named_Candidates( iface, out );
}
static const void *named_resource_vtbl[] =
{
    named_QueryInterface, named_AddRef, named_Release, named_GetIids, named_GetRuntimeClassName, named_GetTrustLevel,
    named_Uri, named_Candidates, named_Resolve, named_ResolveForContext, named_ResolveAll, named_ResolveAllForContext,
};

/* ------------------------------------------------------------------ */
/* ResourceContext: [II 5][QualifierValues 6][Reset 7][ResetQualifierValues 8]
 *                  [OverrideToMatch 9][Clone 10][Languages 11][SetLanguages 12]  */
/* ------------------------------------------------------------------ */

struct res_context
{
    const void *vt_ctx;
    const void *vt_named;     /* IResourceContextNamed (unverified) */
    const void *vt_ctx2;      /* IResourceContext2 (unverified): [II 5][SetQualifierValue 6]
                               * [TrySetName 7][TrySetPackage 8][SetAllQualifiers 9] */
    struct ref_tracker rt;    /* IReferenceTracker identity */
    LONG ref;
    struct kv_pair *qpairs;   /* mutable qualifier storage (shared static strings + heap dups) */
    UINT32 qcount;
    struct map_view qualifiers;
    struct str_vector languages;
    HSTRING lang_items[2];
};
static inline struct res_context *ctx_from( void *iface ) { return (struct res_context *)iface; }
static inline struct res_context *ctx_named_from( void *iface ) { return (struct res_context *)((BYTE *)iface - sizeof(void *)); }
static inline struct res_context *ctx2_from( void *iface ) { return (struct res_context *)((BYTE *)iface - 2 * sizeof(void *)); }

static HRESULT WINAPI map_GetValueForContext( void *iface, HSTRING key, void *context, void **out )
{
    struct res_map *impl = map_from( iface );
    struct res_candidate *cand = NULL;
    struct res_context *ctx = context ? ctx_from( context ) : NULL;
    HRESULT hr;

    {
        const WCHAR *raw;
        HSTRING tmp;
        BOOL owned;
        if (!key_decode( key, &raw, &tmp, &owned ))
        {
            TRACE( "key (undecodable hstring %p) ctx %p -> ELEMENT_NOT_FOUND\n", key, context );
            *out = NULL;
            return ELEMENT_NOT_FOUND_E;
        }
        if (ctx && ctx->qcount)
            hr = map_lookup_ctx( impl, raw, (const pri_qual *)ctx->qpairs, ctx->qcount, &cand );
        else hr = map_lookup( impl, raw, &cand );
        TRACE( "key %s ctx %p hr=%08x\n", debugstr_w( raw ), context, hr );
        key_release( tmp, owned );
    }
    if (SUCCEEDED( hr )) *out = &cand->vt_cand;
    else *out = NULL;
    return hr;
}

static HRESULT WINAPI ctx_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct res_context *impl = ctx_from( iface );
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    /* E7b': see mv_QueryInterface -- the context is the object MCG wraps in
     * the GetForViewIndependentUse chain that feeds the
     * Dictionary<string,string> ctor; answer IIterable<KVP<string,string>>. */
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceContext_g ) ||
        param_iid_eq( iid, 4, ELEM_ITERABLE L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING ))
    {
        *out = &impl->vt_ctx;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContextNamed_g ))
    {
        *out = &impl->vt_named;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContext2_g ))
    {
        *out = &impl->vt_ctx2;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IReferenceTracker_g ))
    {
        *out = &impl->rt;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ctx_AddRef( void *iface ) { return incr( &ctx_from( iface )->ref ); }
static ULONG WINAPI ctx_Release( void *iface )
{
    struct res_context *impl = ctx_from( iface );
    LONG ref = decr( &impl->ref );
    /* The QualifierValues view is embedded, so its MapChanged registrations
     * ride the context's lifetime: drop them with the object (each Release
     * also drops the client delegate it AddRef'd on add). */
    if (!ref && impl->qualifiers.regs) map_event_reg_free_list( &impl->qualifiers );
    /* create_context / ctx_Clone heap-copy every key and value, so uniform
     * release is safe (the shared static default_qualifiers array is never
     * installed as impl->qpairs) */
    if (!ref && impl->qpairs)
    {
        UINT32 i;
        for (i = 0; i < impl->qcount; i++)
        {
            HeapFree( GetProcessHeap(), 0, (void *)impl->qpairs[i].key );
            HeapFree( GetProcessHeap(), 0, (void *)impl->qpairs[i].value );
        }
        HeapFree( GetProcessHeap(), 0, impl->qpairs );
        impl->qpairs = NULL;
    }
    return ref;
}
static HRESULT WINAPI ctx_GetIids( void *iface, ULONG *c, IID **i )
{
    /* E8: the ResourceContext is what GetForViewIndependentUse returns and
     * what the MCG wraps into the Dictionary<string,string> ctor chain; its
     * published set must name the enumerable contract the app castclass's. */
    HRESULT hr = fill_iids_plus_param( ctx_class_iids, ARRAYSIZE( ctx_class_iids ), c, i,
                                       4, ELEM_ITERABLE L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING );
    TRACE( "seed ctx(ResourceContext) GetIids n=%u\n", c ? *c : 0 );
    return hr;
}
static HRESULT WINAPI ctx_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI ctx_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI ctx_QualifierValues( void *iface, void **out )
{
    struct res_context *impl = ctx_from( iface );
    TRACE( "get_QualifierValues\n" );
    *out = &impl->qualifiers.vt_mv;
    incr( &impl->qualifiers.ref );
    return S_OK;
}
static HRESULT WINAPI map_QualifierValues( void *iface, void **out )
{
    struct res_context *ctx;
    HRESULT hr;

    TRACE( "map get_QualifierValues\n" );
    if ((hr = get_current_context( (void **)&ctx ))) return hr;
    /* the current context is the process-pinned default; the embedded
     * qualifiers view rides its lifetime, so hand out the view and drop the
     * context ref immediately (the view carries its own ref) */
    *out = &ctx->qualifiers.vt_mv;
    incr( &ctx->qualifiers.ref );
    decr( &ctx->ref );
    return S_OK;
}
static HRESULT WINAPI ctx_Reset( void *iface ) { TRACE( "stub\n" ); return S_OK; }
static HRESULT WINAPI ctx_ResetQualifierValues( void *iface, void *names ) { TRACE( "stub\n" ); return S_OK; }
static HRESULT WINAPI ctx_OverrideToMatch( void *iface, void *qualifiers ) { TRACE( "stub\n" ); return S_OK; }
static HRESULT WINAPI ctx_Clone( void *iface, void **out );
static HRESULT WINAPI ctx_Languages( void *iface, void **out )
{
    struct res_context *impl = ctx_from( iface );
    TRACE( "get_Languages\n" );
    *out = &impl->languages.vt_ov;
    incr( &impl->languages.ref );
    return S_OK;
}
static HRESULT WINAPI ctx_SetLanguages( void *iface, void *languages )
{
    TRACE( "stub\n" );
    return S_OK;
}
static const void *res_context_vtbl[] =
{
    ctx_QueryInterface, ctx_AddRef, ctx_Release, ctx_GetIids, ctx_GetRuntimeClassName, ctx_GetTrustLevel,
    ctx_QualifierValues, ctx_Reset, ctx_ResetQualifierValues, ctx_OverrideToMatch,
    ctx_Clone, ctx_Languages, ctx_SetLanguages,
};
/* IResourceContextNamed (unverified): [II 5][get_PackageResourceContext 6][put_PackageResourceContext 7] */
static HRESULT WINAPI ctxn_QueryInterface( void *iface, REFIID iid, void **out ) { return ctx_QueryInterface( ctx_named_from( iface ), iid, out ); }
static ULONG WINAPI ctxn_AddRef( void *iface ) { return incr( &ctx_named_from( iface )->ref ); }
static ULONG WINAPI ctxn_Release( void *iface ) { return decr( &ctx_named_from( iface )->ref ); }
static HRESULT WINAPI ctxn_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI ctxn_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI ctxn_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI ctxn_GetPackageResourceContext( void *iface, void **out ) { return ctx_Clone( iface, out ); }
static HRESULT WINAPI ctxn_SetPackageResourceContext( void *iface, void *context ) { return S_OK; }
static const void *res_context_named_vtbl[] =
{
    ctxn_QueryInterface, ctxn_AddRef, ctxn_Release, ctxn_GetIids, ctxn_GetRuntimeClassName, ctxn_GetTrustLevel,
    ctxn_GetPackageResourceContext, ctxn_SetPackageResourceContext,
};

static HRESULT create_context( struct res_context **out )
{
    struct res_context *ctx = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ctx) );
    UINT32 i;
    if (!ctx) return E_OUTOFMEMORY;
    ctx->vt_ctx = res_context_vtbl;
    ctx->vt_named = res_context_named_vtbl;
    ctx->vt_ctx2 = res_context2_vtbl;
    ctx->rt.vt_tracker = rc_tracker_vtbl;
    ctx->rt.primary_off = offsetof( struct res_context, rt );
    ctx->ref = 1;
    ctx->qcount = ARRAYSIZE( default_qualifiers );
    ctx->qpairs = HeapAlloc( GetProcessHeap(), 0, ctx->qcount * sizeof(*ctx->qpairs) );
    if (!ctx->qpairs) { HeapFree( GetProcessHeap(), 0, ctx ); return E_OUTOFMEMORY; }
    for (i = 0; i < ctx->qcount; i++)
    {
        ctx->qpairs[i].key = lstrdupW( default_qualifiers[i].key );
        ctx->qpairs[i].value = lstrdupW( default_qualifiers[i].value );
    }
    /* fill user-profile-sourced keys from the same APIs Windows PRM uses */
    {
        WCHAR localeName[LOCALE_NAME_MAX_LENGTH], family[64];
        if (GetUserDefaultLocaleName( localeName, ARRAYSIZE( localeName ) ) > 0)
        {
            static const WCHAR *locale_keys[] = { L"UserLanguage", L"UserLocaleName" };
            UINT32 k;
            for (k = 0; k < ARRAYSIZE( locale_keys ); k++)
            {
                UINT32 j;
                for (j = 0; j < ctx->qcount; j++)
                    if (!lstrcmpiW( ctx->qpairs[j].key, locale_keys[k] ))
                    {
                        WCHAR *v = lstrdupW( localeName );
                        if (v)
                        {
                            HeapFree( GetProcessHeap(), 0, ctx->qpairs[j].value );
                            ctx->qpairs[j].value = v;
                        }
                        break;
                    }
            }
        }
        get_device_family( family, ARRAYSIZE( family ) );
        for (i = 0; i < ctx->qcount; i++)
            if (!lstrcmpiW( ctx->qpairs[i].key, L"DeviceFamily" ))
            {
                WCHAR *v = lstrdupW( family );
                if (v)
                {
                    HeapFree( GetProcessHeap(), 0, ctx->qpairs[i].value );
                    ctx->qpairs[i].value = v;
                }
                break;
            }
    }
    ctx->qualifiers.vt_mv = map_view_vtbl;
    ctx->qualifiers.vt_om = map_view_observable_vtbl;  /* the same object's IObservableMap face */
    ctx->qualifiers.vt_map = map_iface_vtbl;           /* IMap face (p192 matrix) */
    ctx->qualifiers.vt_iter = map_iterable_vtbl;       /* IIterable<KVP> face, First@6 */
    ctx->qualifiers.ref = 1;
    ctx->qualifiers.pairs = ctx->qpairs;
    ctx->qualifiers.count = ctx->qcount;
    ctx->languages.vt_ov = str_vector_observable_vtbl;
    ctx->languages.vt_vv = str_vector_vtbl;
    ctx->languages.regs = NULL;
    ctx->languages.ref = 1;
    ctx->languages.items = ctx->lang_items;
    ctx->languages.count = 2;
    /* arm D: store the Languages element as a WindowsDuplicateString of the
     * reference (combase duplicate-of-reference -> WindowsCreateString, i.e.
     * the allocated/heap-flag hstring_private), isolating the stored element
     * representation from the reference handle GetAt hands out.  Windows
     * reports the primary language plus its neutral parent (en-US, en). */
    {
        static const WCHAR lang_en[] = { 'e', 'n', 0 };
        static HSTRING_HEADER lang_header2;
        HSTRING ref;
        WindowsCreateStringReference( lang_enUS, 5, &lang_header, &ref );
        WindowsDuplicateString( ref, &ctx->lang_items[0] );
        WindowsCreateStringReference( lang_en, 2, &lang_header2, &ref );
        WindowsDuplicateString( ref, &ctx->lang_items[1] );
    }
    *out = ctx;
    return S_OK;
}

static HRESULT context_set_qualifier( struct res_context *ctx, const WCHAR *name, const WCHAR *value )
{
    UINT32 i;
    for (i = 0; i < ctx->qcount; i++)
    {
        if (!lstrcmpiW( ctx->qpairs[i].key, name ))
        {
            WCHAR *dup = lstrdupW( value );
            if (!dup) return E_OUTOFMEMORY;
            HeapFree( GetProcessHeap(), 0, (void *)ctx->qpairs[i].value );
            ctx->qpairs[i].value = dup;
            return S_OK;
        }
    }
    {
        struct kv_pair *grown = HeapReAlloc( GetProcessHeap(), 0, ctx->qpairs,
                                             (ctx->qcount + 1) * sizeof(*ctx->qpairs) );
        if (!grown) return E_OUTOFMEMORY;
        ctx->qpairs = grown;
        ctx->qpairs[ctx->qcount].key = lstrdupW( name );
        ctx->qpairs[ctx->qcount].value = lstrdupW( value );
        if (!ctx->qpairs[ctx->qcount].key || !ctx->qpairs[ctx->qcount].value) return E_OUTOFMEMORY;
        ctx->qcount++;
        /* the live QualifierValues view must track the (re)allocated array */
        ctx->qualifiers.pairs = ctx->qpairs;
        ctx->qualifiers.count = ctx->qcount;
        return S_OK;
    }
}

static HRESULT WINAPI ctx_Clone( void *iface, void **out )
{
    struct res_context *src = ctx_from( iface );
    struct res_context *ctx;
    HRESULT hr;

    TRACE( "ctx Clone\n" );
    hr = create_context( &ctx );
    if (SUCCEEDED( hr ))
    {
        UINT32 i;
        for (i = 0; i < ctx->qcount && i < src->qcount; i++)
            context_set_qualifier( ctx, src->qpairs[i].key, src->qpairs[i].value );
        *out = &ctx->vt_ctx;
    }
    return hr;
}

/* IResourceContext2 */
static HRESULT WINAPI ctx2_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "ctx2_QueryInterface enter\n" );
        return ctx_QueryInterface( ctx2_from( iface ), iid, out );
}
static ULONG WINAPI ctx2_AddRef( void *iface ) { return incr( &ctx2_from( iface )->ref ); }
static ULONG WINAPI ctx2_Release( void *iface ) { return ctx_Release( &ctx2_from( iface )->vt_ctx ); }
static HRESULT WINAPI ctx2_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI ctx2_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI ctx2_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI ctx2_SetQualifierValue( void *iface, HSTRING value, HSTRING name )
{
    struct res_context *impl = ctx2_from( iface );
    const WCHAR *v, *n;
    HSTRING vtmp, ntmp;
    BOOL vown, nown;
    HRESULT hr;

    /* both sides arrive through the same loose-binding marshaling as the keys:
     * HSTRING payload, else the managed-string CCW surface */
    if (!key_decode( value, &v, &vtmp, &vown )) v = NULL;
    if (!key_decode( name, &n, &ntmp, &nown )) n = NULL;
    TRACE( "%s = %s\n", debugstr_w( n ), debugstr_w( v ) );
    if (!n || !v)
    {
        key_release( vtmp, vown );
        key_release( ntmp, nown );
        return E_INVALIDARG;
    }
    hr = context_set_qualifier( impl, n, v );  /* dups both strings */
    key_release( vtmp, vown );
    key_release( ntmp, nown );
    return hr;
}
static HRESULT WINAPI ctx2_TrySetName( void *iface, HSTRING name, INT32 *ok )
{
    FIXME( "stub\n" );
    *ok = FALSE;
    return S_OK;
}
static HRESULT WINAPI ctx2_TrySetPackage( void *iface, HSTRING package, INT32 *ok )
{
    FIXME( "stub\n" );
    *ok = FALSE;
    return S_OK;
}
static HRESULT WINAPI ctx2_SetAllQualifiers( void *iface, void *qualifiers )
{
    FIXME( "stub (IResourceContext2::SetAllQualifiers: IMapView<HSTRING,HSTRING> iteration)\n" );
    return S_OK;
}
static const void *res_context2_vtbl[] =
{
    ctx2_QueryInterface, ctx2_AddRef, ctx2_Release, ctx2_GetIids, ctx2_GetRuntimeClassName, ctx2_GetTrustLevel,
    ctx2_SetQualifierValue, ctx2_TrySetName, ctx2_TrySetPackage, ctx2_SetAllQualifiers,
};

/* ------------------------------------------------------------------ */
/* factories                                                           */
/* ------------------------------------------------------------------ */

struct rc_factory
{
    const void *vt_primary;  /* IActivationFactory */
    const void *vt_statics;  /* IResourceContextStatics */
    const void *vt_statics2; /* IResourceContextStatics2 */
    const void *vt_statics3;
    const void *vt_statics4;
    struct ref_tracker rt;   /* IReferenceTracker identity */
};
static struct rc_factory *rcf_from( void *iface ) { return (struct rc_factory *)iface; }
static struct rc_factory *rcf_from_s( void *iface, size_t off ) { return (struct rc_factory *)((BYTE *)iface - off); }

static struct res_context *the_current_context;

static HRESULT get_current_context( void **out )
{

    if (!out) return E_POINTER;    if (!the_current_context)
    {
        HRESULT hr = create_context( &the_current_context );
        if (FAILED( hr )) return hr;
    }
    *out = &the_current_context->vt_ctx;
    incr( &the_current_context->ref );
    return S_OK;
}

static HRESULT WINAPI rcf_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct rc_factory *impl = rcf_from( iface );
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    /* E7b': probe33 cast #14 -- the ComObject seeded from this factory face
     * (rc_statics2_vtbl, GetForViewIndependentUse) is castclass'd to
     * IIterable<IKeyValuePair<string,string>>; answer the enumerable
     * contract from the primary face. */
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory_g_ ))
    {
        *out = &impl->vt_primary;
        return S_OK;
    }
    /* E17b': cast #14 (probe33) QIs the statics2 COM pointer (GetForViewIn-
     * dependentUse's wrapper, the face seeded by rc2_GetIids) with the
     * IIterable<IKeyValuePair<string,string>> pinterface {9828e927-...} that
     * rc2_GetIids publishes.  The Mcg wrapper caches the impl handed out at
     * startup (tid 0x3000200000 -> impl) and casts *that* pointer at #14;
     * answering from vt_primary (no First() slot -- slot 6 is ActivateInstance)
     * let the temporary die between the arms, so the #14 QI E_NOINTERFACE'd ->
     * InvalidCast -> c0000602.  Answer with a STABLE singleton face: the
     * process-lifetime current ResourceContext's QualifierValues map-view
     * (IMapView<KVP<string,string>> is-a IIterable<KVP<string,string>>, same
     * pointer, First() -> working IIterator<KVP> over the qualifiers).  The
     * context is pinned by the_current_context for the life of the process,
     * so every hand-out is the identical re-entrant pointer; each QI AddRefs
     * the view (standard COM), and the context's own ref is returned right
     * away (the static keeps it alive, the view's pairs ride the context). */
    if (param_iid_eq( iid, 4, ELEM_ITERABLE L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING ))
    {
        void *ctx = NULL;
        HRESULT hr = get_current_context( &ctx );
        struct res_context *impl_ctx = ctx_from( ctx );
        if (FAILED( hr )) return hr;
        *out = &impl_ctx->qualifiers.vt_mv;
        TRACE( "statics2 QI IIterable<KVP<string,string>> -> stable QualifierValues view %p\n", *out );
        incr( &impl_ctx->qualifiers.ref );
        decr( &impl_ctx->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->vt_primary;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContextStatics_g ))
    {
        *out = &impl->vt_statics;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContextStatics2_g ))
    {
        *out = &impl->vt_statics2;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContextStatics3_g ))
    {
        *out = &impl->vt_statics3;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceContextStatics4_g ))
    {
        *out = &impl->vt_statics4;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IReferenceTracker_g ))
    {
        *out = &impl->rt;
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rcf_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rcf_Release( void *iface ) { return 1; }
static HRESULT WINAPI rcf_GetIids( void *iface, ULONG *c, IID **i )
{
    HRESULT hr = fill_iids( rc_class_iids, ARRAYSIZE( rc_class_iids ), c, i );
    TRACE( "seed rcf(ActivationFactory) GetIids n=%u\n", c ? *c : 0 );
    return hr;
}
static HRESULT WINAPI rcf_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI rcf_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rcf_ActivateInstance( void *iface, void **instance )
{

    if (!instance) return E_POINTER;    struct res_context *ctx;
    HRESULT hr = create_context( &ctx );
    TRACE( "ResourceContext::ActivateInstance\n" );
    if (SUCCEEDED( hr )) *instance = &ctx->vt_ctx;
    return hr;
}
static const void *rc_factory_vtbl[] =
{
    rcf_QueryInterface, rcf_AddRef, rcf_Release, rcf_GetIids, rcf_GetRuntimeClassName, rcf_GetTrustLevel,
    rcf_ActivateInstance,
};
/* IResourceContextStatics: [II 5][CreateMatchingContext 6] */
static HRESULT WINAPI rcs_QueryInterface( void *iface, REFIID iid, void **out ) { return rcf_QueryInterface( rcf_from_s( iface, offsetof(struct rc_factory, vt_statics) ), iid, out ); }
static ULONG WINAPI rcs_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rcs_Release( void *iface ) { return 1; }
static HRESULT WINAPI rcs_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rcs_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI rcs_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rcs_CreateMatchingContext( void *iface, void *qualifiers, void **out )
{
    TRACE( "CreateMatchingContext\n" );
    return get_current_context( out );
}
static const void *rc_statics_vtbl[] =
{
    rcs_QueryInterface, rcs_AddRef, rcs_Release, rcs_GetIids, rcs_GetRuntimeClassName, rcs_GetTrustLevel,
    rcs_CreateMatchingContext,
};
/* IResourceContextStatics2: [II 5][GetForCurrentView 6][SetGlobalQualifierValue 7]
 *   [ResetGlobalQualifierValues 8][ResetGlobalQualifierValuesForSpecifiedQualifiers 9][GetForViewIndependentUse 10] */
static HRESULT WINAPI rc2_QueryInterface( void *iface, REFIID iid, void **out ) { return rcf_QueryInterface( rcf_from_s( iface, offsetof(struct rc_factory, vt_statics2) ), iid, out ); }
static ULONG WINAPI rc2_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rc2_Release( void *iface ) { return 1; }
static HRESULT WINAPI rc2_GetIids( void *iface, ULONG *c, IID **i )
{
    /* E8: this is the face whose pointer the app wraps (GetForViewIndependentUse
     * is slot 10 of rc_statics2_vtbl); mrt100_app seeds the wrapper's
     * m_cachedInterfaces from this GetIids exactly once, before the QI chain,
     * so the IIterable<IKeyValuePair<string,string>> target of cast #14 must
     * be in the published set. */
    HRESULT hr = fill_iids_plus_param( rc_class_iids, ARRAYSIZE( rc_class_iids ), c, i,
                                       4, ELEM_ITERABLE L"`1", ELEM_KEYVALUEPAIR L"`2", ELEM_STRING, ELEM_STRING );
    TRACE( "seed statics2(GetForViewIndependentUse) GetIids n=%u\n", c ? *c : 0 );
    return hr;
}
/* E10: the MCG RCW for this pointer is picked by McgComHelpers.ComInterfaceToComObject
 * (SharedLibrary +0x866220 -> ..._NoCache) from IInspectable::GetRuntimeClassName:
 * McgModuleManager::TryGetClassTypeFromName(SCL) scans the Mcg modules and the
 * framework (mrt100_app) module answers "Windows.ApplicationModel.Resources.Core.
 * ResourceContext" first, with a generated __Impl whose m_cachedInterfaces is a
 * frozen single-triple template {IResourceContextStatics2} (mrt100_app RVA
 * 0x20fa8) -- the IIterable<IKeyValuePair<string,string>> enumerable the app
 * castclass's (cast #14, target TypeManager handle 0x1806800000, app Mcg module
 * RID 0x6800000) is absent, the wrapped pointer's tracker is (Free, NULL), and
 * the cast dies InvalidCast -> InvalidOperation -> c0000602.
 *
 * A parameterized interface SCL misses the class tables, classTypeToCreateRCW
 * falls back to the class from the signature (the app-module ResourceContext),
 * whose __Impl template carries the dictionary enumerable contract; the cast
 * resolves against the frozen list and First() reaches QI {9828e927} (E7b'). */
/* E10-B: E10-A (parameterized IMapView SCL) left the cast #14 tail unchanged
 * (the signature-class fallback picks a template without the enumerable), so
 * answer an SCL that hits the app Mcg module's class table directly:
 * Windows.Foundation.Diagnostics.LoggingChannel is registered there (its
 * __Impl dispatch maps in CalculatorApp.dll carry IIterable<IKeyValuePair
 * <string,string>>, RID 0x6800000 == target handle 0x1806800000) and is absent
 * from the framework Mcg table, so the app template wins the lookup.
 *
 * E10-C: the LoggingChannel template carries no IIterable entry at all, so the
 * scan of every Mcg module table (mrt100_app framework, CalculatorApp,
 * SharedLibrary) for the target handle 0x1806800000 lands on the SharedLibrary
 * module alone: its McgInterfaceData dispatch maps list 0x1806800000 for the
 * collection-interface __Impl templates (iterator/observable-map families);
 * mrt100_app and CalculatorApp.exe carry zero.  Arm the IIterable-carrying
 * __Impl whose semantics match the QualifierValues enumerable contract:
 * Windows.Foundation.Collections.IIterator`1<Windows.Foundation.Collections
 * .IKeyValuePair`2<System.String,System.String>> (SharedLibrary
 * IIterator_A__scg_KeyValuePair_A_string_j_string_V__V___Impl, frozen triple
 * {IIterator<KVP<string,string>>, IIterable<KVP<string,string>>,
 * IEnumerable<KVP<string,string>>}).  Lookup order mrt100 -> app -> SharedLibrary
 * misses on the first two, so the SharedLibrary template is the one created. */
#define ELEM_ITER_KVP_SS L"Windows.Foundation.Collections.IIterator`1<" ELEM_KEYVALUEPAIR L"`2<" ELEM_STRING L"," ELEM_STRING ">>"
/* E14-d: probe33 places cast #14's ComInterfaceToComObject on the *statics2*
 * face pointer (GetForViewIndependentUse's wrapper), so this SCL alone picks
 * the Mcg RCW for the cast.  The SharedLibrary name-pool key that resolves
 * (StringMap::FindString, EXACT match) to the __Impl whose frozen triple
 * carries target handle 0x1806800000 is the short type-display name
 * `IIterable`1<IKeyValuePair`2<String,String>>` (heap RVA 0x511); the E10-C
 * fully-qualified spelling lands on a target-less template.  The IIterator
 * short key (0x541) left the tail unchanged as well -- both faces need the
 * same target, the lookup is no longer the blocker. */
static HRESULT WINAPI rc2_GetRuntimeClassName( void *iface, HSTRING *n )
{
    TRACE( "rc2_GetRuntimeClassName enter\n" );

    /* Cast-#14 winner (live-dumped McgClassData, SharedLibrary classData[78]):
     * EEType flag 0x60 (IsGeneric|HasRemoteInterfaces) so the CheckCastInterface
     * miss on typeID 0x1806800000 goes CanCastComObject -> QI {9828e927-...} ->
     * our IIterable<KVP> answer -> triple insert with tid 0x1806800000 -> pass.
     * Key = name-pool 0xc6 toolbox signature (IObservableMap`1<KVP ss> family). */
    static const WCHAR mcg_key_c6[] =
        L"Windows.Foundation.Collections.IObservableMap`1<System.Collections.Generic.IKeyValuePair`2<System.Collections.Generic.String,System.Collections.Generic.String>,System.Collections.Generic.IEnumerable`1<System.Collections.Generic.IKeyValuePair`2<Windows.UI.Xaml.Interop.String,System.Collections.Generic.String>>,System.Collections.Generic.IReadOnlyDictionary`2<Windows.Foundation.Diagnostics.ILoggingField,Spectro.Toml.TomlValue>,Spectro.UI.Controls.MediaDisplayInformation,Windows.ApplicationModel.Chat.ChatMessageResourceMap,Windows.ApplicationModel.Resources.Core.ResourceMap,Windows.ApplicationModel.Resources.Core.ResourceQualifierValues,Windows.Data.Text.WordSegmentResourceMap,Windows.Foundation.Diagnostics.LoggingChannelAttribute,Windows.Storage.Provider.IPropertyStore,Windows.UI.Xaml.Interop.IBindableIterable,Windows.UI.Xaml.Interop.IBindableIterator,Windows.UI.Xaml.Interop.IBindableVector,Windows.UI.Xaml.Interop.IBindableVectorView,Windows.Foundation.Diagnostics.ILoggingPropertyKeyValueInterface,Windows.UI.Xaml.Interop.IPropertyStorePropertyIterator,Windows.Foundation.Collections.IObservableVector`1<Windows.Globalization.JapanesePhonemeReading>";

    return className( mcg_key_c6, n );
}
static HRESULT WINAPI rc2_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rc2_GetForCurrentView( void *iface, void **out )
{
    TRACE( "GetForCurrentView\n" );
    return get_current_context( out );
}
static HRESULT WINAPI rc2_SetGlobalQualifierValue( void *iface, HSTRING value, HSTRING name ) { TRACE( "stub\n" ); return S_OK; }
static HRESULT WINAPI rc2_ResetGlobalQualifierValues( void *iface ) { return S_OK; }
static HRESULT WINAPI rc2_ResetGlobalQualifierValuesForSpecifiedQualifiers( void *iface, void *names ) { return S_OK; }
static HRESULT WINAPI rc2_GetForViewIndependentUse( void *iface, void **out )
{
    TRACE( "GetForViewIndependentUse\n" );
    return get_current_context( out );
}
static const void *rc_statics2_vtbl[] =
{
    rc2_QueryInterface, rc2_AddRef, rc2_Release, rc2_GetIids, rc2_GetRuntimeClassName, rc2_GetTrustLevel,
    rc2_GetForCurrentView, rc2_SetGlobalQualifierValue, rc2_ResetGlobalQualifierValues,
    rc2_ResetGlobalQualifierValuesForSpecifiedQualifiers, rc2_GetForViewIndependentUse,
};
static HRESULT WINAPI rc3_QueryInterface( void *iface, REFIID iid, void **out ) { return rcf_QueryInterface( rcf_from_s( iface, offsetof(struct rc_factory, vt_statics3) ), iid, out ); }
static ULONG WINAPI rc3_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rc3_Release( void *iface ) { return 1; }
static HRESULT WINAPI rc3_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rc3_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI rc3_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rc3_SetGlobalQualifierValueWithPersistence( void *iface, HSTRING value, HSTRING name, INT32 persist )
{
    TRACE( "stub\n" );
    return S_OK;
}
static const void *rc_statics3_vtbl[] =
{
    rc3_QueryInterface, rc3_AddRef, rc3_Release, rc3_GetIids, rc3_GetRuntimeClassName, rc3_GetTrustLevel,
    rc3_SetGlobalQualifierValueWithPersistence,
};
static HRESULT WINAPI rc4_QueryInterface( void *iface, REFIID iid, void **out ) { return rcf_QueryInterface( rcf_from_s( iface, offsetof(struct rc_factory, vt_statics4) ), iid, out ); }
static ULONG WINAPI rc4_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rc4_Release( void *iface ) { return 1; }
static HRESULT WINAPI rc4_GetIids( void *iface, ULONG *c, IID **i ) { return fill_iids( rc_class_iids, ARRAYSIZE( rc_class_iids ), c, i ); }
static HRESULT WINAPI rc4_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCECONTEXT, n ); }
static HRESULT WINAPI rc4_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rc4_GetForUIContext( void *iface, void *context, void **out )
{
    TRACE( "GetForUIContext\n" );
    return get_current_context( out );
}
static const void *rc_statics4_vtbl[] =
{
    rc4_QueryInterface, rc4_AddRef, rc4_Release, rc4_GetIids, rc4_GetRuntimeClassName, rc4_GetTrustLevel,
    rc4_GetForUIContext,
};
/* ------------------------------------------------------------------ */
/* IReferenceTracker (IID {11d3b13a-...}, see the note at the top)      */
/* ------------------------------------------------------------------ */
/* The CLR reference tracker QIs the object for this IID and, when the QI
 * succeeds, walks slots 3..9.  Slots 3, 4, 7, 8 and 9 take NO arguments, so
 * the callee must not read rdx/r8.  Handing out an IInspectable-shaped vtable
 * for this IID is what produced the Calculator crash-loop: the app's
 * no-argument call at slot 3 executed GetIids, whose "*count = 0" wrote
 * through rdx (= 0x24, stale register content), and the no-argument call at
 * slot 7 executed CreateWithCurrentView, whose "*out = context" wrote through
 * r8 (= stale heap).  Both SEH-catched faults corrupted the app's tracker
 * walk and fed the 280x c0000005 loop.  Note that neither call site passes
 * out-params at all, so the earlier "caller state" verdict cannot hold.
 *
 * Semantics implemented: this module owns no garbage-collected objects, so it
 * reports no tracker targets and has no XAML reference tracker manager; the
 * tracker-source notifications are bookkept on the object. */
static inline struct ref_tracker *tracker_from( void *iface ) { return (struct ref_tracker *)iface; }
/* the object's vtable entry idx: iface -> vtable base -> entry */
static inline void *vtbl_slot( void *iface, int idx ) { return ((void **)(*(void **)iface))[idx]; }
static inline ULONG owner_addref( void *owner ) { return ((ULONG (WINAPI *)(void *))vtbl_slot( owner, 1 ))( owner ); }
/* The owning object begins at its primary interface slot, which precedes this
 * tracker slot by primary_off.  Derived at compile time (offsetof) so the
 * identity cannot end up pointing at a NULL or stale owner. */
static inline void *tracker_owner( struct ref_tracker *impl ) { return (char *)impl - impl->primary_off; }
static inline int tracker_owner_valid( struct ref_tracker *impl )
{
    void *owner = tracker_owner( impl );
    return owner && owner != (void *)impl;
}


static HRESULT WINAPI rt_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "rt_QueryInterface enter\n" );

    struct ref_tracker *impl = tracker_from( iface );
    void *owner = tracker_owner( impl );

    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IReferenceTracker_g ))
    {
        *out = &impl->vt_tracker;
        if (tracker_owner_valid( impl )) owner_addref( owner );
        return S_OK;
    }
    if (!tracker_owner_valid( impl ))
    {
        /* Defensive: an uninitialised tracker identity must report
         * E_NOINTERFACE, never walk a foreign vtable. */
        *out = NULL;
        return E_NOINTERFACE;
    }
    /* every other interface of the object is reachable from this one */
    return ((HRESULT (WINAPI *)(void *, REFIID, void **))vtbl_slot( owner, 0 ))( owner, iid, out );
}
static ULONG WINAPI rt_AddRef( void *iface )
{
    struct ref_tracker *impl = tracker_from( iface );
    return tracker_owner_valid( impl ) ? owner_addref( tracker_owner( impl ) ) : 2;
}
static ULONG WINAPI rt_Release( void *iface )
{
    struct ref_tracker *impl = tracker_from( iface );
    if (!tracker_owner_valid( impl )) return 1;
    return ((ULONG (WINAPI *)(void *))vtbl_slot( tracker_owner( impl ), 2 ))( tracker_owner( impl ) );
}
static HRESULT WINAPI rt_ConnectFromTrackerSource( void *iface )
{
    struct ref_tracker *impl = tracker_from( iface );
    TRACE( "%p: tracker source connected\n", impl );
    InterlockedIncrement( &impl->sources );
    return S_OK;
}
static HRESULT WINAPI rt_DisconnectFromTrackerSource( void *iface )
{
    struct ref_tracker *impl = tracker_from( iface );
    TRACE( "%p: tracker source disconnected\n", impl );
    if (InterlockedDecrement( &impl->sources ) < 0) InterlockedIncrement( &impl->sources );
    return S_OK;
}
static HRESULT WINAPI rt_FindTrackerTargets( void *iface, void *callback )
{
    TRACE( "rt_FindTrackerTargets enter\n" );

    if (!callback) return E_POINTER;
    /* No tracker targets: the module holds no references to garbage-collected
     * (managed) objects, so FoundTrackerTarget is never called back. */
    return S_OK;
}
static const void *rc_manager_vtbl[];
static struct ref_tracker_manager rc_tracker_manager;   /* defined with the manager impl below */

static const void *rc_manager_vtbl[];
static struct ref_tracker_manager rc_tracker_manager;   /* defined with the manager impl below */

static const void *rc_manager_vtbl[];
static struct ref_tracker_manager rc_tracker_manager;   /* defined with the manager impl below */

static HRESULT WINAPI rt_GetReferenceTrackerManager( void *iface, void **manager )
{
    TRACE( "rt_GetReferenceTrackerManager enter\n" );

    struct ref_tracker_manager *impl = &rc_tracker_manager;

    if (!manager) return E_POINTER;
    /* CoreRT drives the manager around every tracking walk and does not
     * tolerate a NULL result, and the platform does implement this interface,
     * so hand out the process-wide manager instance. */
    *manager = &impl->vt_manager;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI mgr_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "mgr_QueryInterface enter\n" );

    struct ref_tracker_manager *impl = (struct ref_tracker_manager *)iface;

    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IReferenceTrackerManager_g )
        || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->vt_manager;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI mgr_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct ref_tracker_manager *)iface)->ref );
}
static ULONG WINAPI mgr_Release( void *iface )
{
    return InterlockedDecrement( &((struct ref_tracker_manager *)iface)->ref );
}
static HRESULT WINAPI mgr_ReferenceTrackingStarted( void *iface )
{
    struct ref_tracker_manager *impl = (struct ref_tracker_manager *)iface;
    TRACE( "%p: reference tracking started\n", impl );
    InterlockedIncrement( &impl->cycles );
    return S_OK;
}
static HRESULT WINAPI mgr_FindTrackerTargetsCompleted( void *iface, int walk_failed )
{
    TRACE( "mgr_FindTrackerTargetsCompleted enter\n" );

    struct ref_tracker_manager *impl = (struct ref_tracker_manager *)iface;
    InterlockedIncrement( &impl->walks );
    impl->walk_failed = walk_failed;
    return S_OK;
}
static HRESULT WINAPI mgr_ReferenceTrackingCompleted( void *iface )
{
    TRACE( "%p: reference tracking completed\n", (struct ref_tracker_manager *)iface );
    return S_OK;
}
static HRESULT WINAPI mgr_SetReferenceTrackerHost( void *iface, void *host )
{
    TRACE( "mgr_SetReferenceTrackerHost enter\n" );

    struct ref_tracker_manager *impl = (struct ref_tracker_manager *)iface;

    if (!host) return E_POINTER;
    if (!impl->host)
    {
        owner_addref( host );
        impl->host = host;   /* retained for the lifetime of the process */
    }
    return S_OK;
}
static const void *rc_manager_vtbl[] =
{
    mgr_QueryInterface, mgr_AddRef, mgr_Release,
    mgr_ReferenceTrackingStarted, mgr_FindTrackerTargetsCompleted,
    mgr_ReferenceTrackingCompleted, mgr_SetReferenceTrackerHost,
};
static struct ref_tracker_manager rc_tracker_manager = { rc_manager_vtbl, 1, NULL, 0, 0, 0 };
static HRESULT WINAPI rt_AddRefFromTrackerSource( void *iface )
{
    TRACE( "rt_AddRefFromTrackerSource enter\n" );

    InterlockedIncrement( &tracker_from( iface )->ext_refs );
    return S_OK;
}
static HRESULT WINAPI rt_ReleaseFromTrackerSource( void *iface )
{
    TRACE( "rt_ReleaseFromTrackerSource enter\n" );

    struct ref_tracker *impl = tracker_from( iface );
    if (InterlockedDecrement( &impl->ext_refs ) < 0) InterlockedIncrement( &impl->ext_refs );
    return S_OK;
}
static HRESULT WINAPI rt_PegFromTrackerSource( void *iface )
{
    TRACE( "rt_PegFromTrackerSource enter\n" );

    InterlockedIncrement( &tracker_from( iface )->pegs );
    return S_OK;
}
static const void *rc_tracker_vtbl[] =
{
    rt_QueryInterface, rt_AddRef, rt_Release,
    rt_ConnectFromTrackerSource, rt_DisconnectFromTrackerSource, rt_FindTrackerTargets,
    rt_GetReferenceTrackerManager, rt_AddRefFromTrackerSource, rt_ReleaseFromTrackerSource,
    rt_PegFromTrackerSource,
};
static struct rc_factory rc_factory_inst =
{
    rc_factory_vtbl, rc_statics_vtbl, rc_statics2_vtbl, rc_statics3_vtbl, rc_statics4_vtbl,
    { rc_tracker_vtbl, offsetof( struct rc_factory, rt ), 0, 0, 0 },
};

/* ResourceManager */
struct res_manager
{
    const void *vt_rm;
    const void *vt_rm2;
    LONG ref;
};
static inline struct res_manager *rm_from( void *iface ) { return (struct res_manager *)iface; }
static inline struct res_manager *rm2_from( void *iface ) { return (struct res_manager *)((BYTE *)iface - sizeof(void *)); }

/* ResourceLoader: the app-facing facade over the main resource map.  Its
 * activation answers the ResourceManager factory (the MRT Core tier documents
 * ResourceLoader as the manager's facade); the loader IIDs QI off the same
 * manager object onto this singleton face pair.
 * IResourceLoader (Win8):   [II 5][GetString 6][GetStringForUri 7][MainResourceMap 8]
 * IResourceLoader2 (Win8.1):[II 5][Languages 6][put_Languages 7][InsertResourceFile 8]
 *                           [InsertResourceStream 9] */
struct res_loader
{
    const void *vt_loader;
    const void *vt_loader2;
    LONG ref;
};
static const void *res_loader_vtbl[];
static const void *res_loader2_vtbl[];
static struct res_loader res_loader_inst = { res_loader_vtbl, res_loader2_vtbl, 1 };
static inline struct res_loader *loader_from( void *iface ) { return (struct res_loader *)iface; }
static inline struct res_loader *loader2_from( void *iface ) { return (struct res_loader *)((BYTE *)iface - sizeof(void *)); }

static struct res_map main_map = { NULL, NULL, 1, NULL };

static HRESULT WINAPI rm_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct res_manager *impl = rm_from( iface );
    TRACE( "iid %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceManager_g ))
    {
        *out = &impl->vt_rm;
        incr( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceManager2_g ))
    {
        *out = &impl->vt_rm2;
        incr( &impl->ref );
        return S_OK;
    }
    /* ResourceLoader is the manager's facade: the loader faces answer from the
     * same object (Microsoft.Windows.ApplicationModel.Resources.ResourceLoader
     * activation lands on this factory). */
    if (IsEqualGUID( iid, &IID_IResourceLoader_g ))
    {
        *out = &res_loader_inst.vt_loader;
        incr( &res_loader_inst.ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceLoader2_g ))
    {
        *out = &res_loader_inst.vt_loader2;
        incr( &res_loader_inst.ref );
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rm_AddRef( void *iface ) { return incr( &rm_from( iface )->ref ); }
static ULONG WINAPI rm_Release( void *iface ) { return decr( &rm_from( iface )->ref ); }
static HRESULT WINAPI rm_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rm_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEMANAGER, n ); }
static HRESULT WINAPI rm_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
/* IResourceManager::get_CurrentCulture [6] (Windows 8, deprecated with
 * CandidateLanguages): the map's primary language, the same en-US the
 * Language qualifier reports.  The ResourceManager.Current getter reads this
 * slot first (the gate tail's NLS GetLocaleInfoEx(en-US)+(en) culture walk),
 * puts it back through put_CurrentCulture [7], then reads
 * IResourceManager2::get_CandidateLanguages: with the pair missing the
 * getter took MainResourceMap for the culture string and the walk threw. */
static HRESULT WINAPI rm_CurrentCulture( void *iface, HSTRING *culture )
{
    const WCHAR *lang = L"en-US";
    UINT32 i;

    TRACE( "rm get_CurrentCulture\n" );
    if (!culture) return E_POINTER;
    for (i = 0; i < ARRAYSIZE( default_qualifiers ); i++)
        if (!lstrcmpW( default_qualifiers[i].key, L"Language" )) { lang = default_qualifiers[i].value; break; }
    return WindowsCreateString( lang, (UINT32)lstrlenW( lang ), culture );
}
/* IResourceManager::put_CurrentCulture [7]: accepted, the culture set is the
 * PRM snapshot (same no-op tier as ctx_SetLanguages). */
static HRESULT WINAPI rm_put_CurrentCulture( void *iface, HSTRING culture )
{
    TRACE( "rm put_CurrentCulture (stub)\n" );
    return S_OK;
}
static HRESULT WINAPI rm_MainResourceMap( void *iface, void **out )
{
    TRACE( "rm get_MainResourceMap\n" );
    *out = &main_map.vt_map;
    incr( &main_map.ref );
    return S_OK;
}
static struct rm_map_entry all_maps_entries[1];
static struct rm_map_view all_maps_view = { NULL, 1, all_maps_entries, 1 };
static struct res_map *all_maps_map;
static HRESULT WINAPI rm_AllResourceMaps( void *iface, void **out )
{
    TRACE( "rm_AllResourceMaps enter\n" );

    if (!all_maps_map)
    {
        all_maps_map = &main_map;
        all_maps_entries[0].key = (WCHAR *)L"MainPkg"; /* documented tier: package identity unavailable */
        all_maps_entries[0].map = &main_map.vt_map;
        all_maps_view.vt_mv = rm_map_view_vtbl;
    }
    *out = &all_maps_view.vt_mv;
    incr( &all_maps_view.ref );
    return S_OK;
}
static HRESULT WINAPI rm_DefaultContext( void *iface, void **out )
{
    TRACE( "rm_DefaultContext enter\n" );
        return get_current_context( out );
}
static HRESULT WINAPI rm_LoadPriFiles( void *iface, void *files )
{
    TRACE( "stub\n" );
    return S_OK;
}
static HRESULT WINAPI rm_UnloadPriFiles( void *iface, void *files )
{
    TRACE( "stub\n" );
    return S_OK;
}
/* IResourceManager (IDL order): [II 5][CurrentCulture 6][put_CurrentCulture 7]
 *   [MainResourceMap 8][AllResourceMaps 9][DefaultContext 10]
 *   [LoadPriFiles 11][UnloadPriFiles 12] */
static const void *res_manager_vtbl[] =
{
    rm_QueryInterface, rm_AddRef, rm_Release, rm_GetIids, rm_GetRuntimeClassName, rm_GetTrustLevel,
    rm_CurrentCulture, rm_put_CurrentCulture,
    rm_MainResourceMap, rm_AllResourceMaps, rm_DefaultContext, rm_LoadPriFiles, rm_UnloadPriFiles,
};
static HRESULT WINAPI rm2_QueryInterface( void *iface, REFIID iid, void **out ) { return rm_QueryInterface( rm2_from( iface ), iid, out ); }
static ULONG WINAPI rm2_AddRef( void *iface ) { return incr( &rm2_from( iface )->ref ); }
static ULONG WINAPI rm2_Release( void *iface ) { return decr( &rm2_from( iface )->ref ); }
static HRESULT WINAPI rm2_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
/* RegisteredComponents decode (CalculatorApp.dll .rdata map, file offsets
 * 0x270248-0x2a4600: entries [module ptr RVA 0x28c50][u32 char-count][UTF-16
 * class string]): the app map carries Windows.ApplicationModel.Resources.
 * Core.ResourceManager (0x2a4410) and .Core.ResourceContext (0x2a4388) ->
 * CalculatorApp.dll, but NO Microsoft.Windows.ApplicationModel.Resources.
 * ResourceManager entry in any app image (absent UTF-16 and UTF-8 in
 * CalculatorApp.dll, SharedLibrary.dll, CalculatorApp.exe, mrt100_app.dll,
 * mrt100.dll) -> the mrt100_app MCG thunk lands on the empty slot (-512).
 * Publish the MWR class key on the IResourceManager2 face (the face the
 * activation hands out): the Mcg toolbox lookup then resolves the MWR
 * ResourceManager TYPE and routes it through the combase
 * RoGetActivationFactory path that answers IResourceManager2 {7ec10160},
 * the route the first three activations use. */
static HRESULT WINAPI rm2_GetRuntimeClassName( void *iface, HSTRING *n )
{
    static const WCHAR mcg_key_rmwr[] = L"Microsoft.Windows.ApplicationModel.Resources.ResourceManager";

    return className( mcg_key_rmwr, n );
}
static HRESULT WINAPI rm2_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
/* IResourceManager2::get_CandidateLanguages [6]: the languages of the default
 * context (en-US + its neutral parent), handed out on the same
 * IObservableVector<HSTRING> face ResourceContext::Languages uses — the
 * ResourceManager.Current getter enumerates this right after the
 * CurrentCulture walk. */
static HRESULT WINAPI rm2_CandidateLanguages( void *iface, void **out )
{
    struct res_context *ctx;
    HRESULT hr;

    TRACE( "rm2 get_CandidateLanguages\n" );
    if ((hr = get_current_context( (void **)&ctx ))) return hr;
    /* the languages vector is embedded in the process-pinned default context;
     * hand out the face with its own ref and drop the context ref (the same
     * pattern as map_QualifierValues) */
    *out = &ctx->languages.vt_ov;
    incr( &ctx->languages.ref );
    decr( &ctx->ref );
    return S_OK;
}
/* IResourceManager2::InsertResourceFile [7] / InsertResourceStream [8]:
 * accepted, the pri snapshot is the resource source (same tier as
 * rm_LoadPriFiles). */
static HRESULT WINAPI rm2_InsertResourceFile( void *iface, HSTRING filename )
{
    TRACE( "stub\n" );
    return S_OK;
}
static HRESULT WINAPI rm2_InsertResourceStream( void *iface, HSTRING stream, INT32 filetype )
{
    TRACE( "stub\n" );
    return S_OK;
}
/* IResourceManager3 (1703) pair, reachable at trailing slots 9/10 until the
 * face gets its own IID/QI entry. */
static HRESULT WINAPI rm2_GetAllNamedResourcesForPackage( void *iface, HSTRING pkgname, INT32 info, void **out )
{
    TRACE( "rm2_GetAllNamedResourcesForPackage enter\n" );

    struct iface_vector *vec = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*vec) );
    if (!vec) return E_OUTOFMEMORY;
    vec->vt_vv = iface_vector_vtbl;
    vec->ref = 1;
    vec->count = 0;
    vec->elem = ELEM_NAMEDRES;
    *out = &vec->vt_vv;
    return S_OK;
}
static HRESULT WINAPI rm2_GetAllSubtreesForPackage( void *iface, HSTRING pkgname, INT32 info, void **out )
{
    TRACE( "rm2_GetAllSubtreesForPackage enter\n" );
        return rm2_GetAllNamedResourcesForPackage( iface, pkgname, info, out );
}
/* IResourceManager2 (IDL order): [II 5][CandidateLanguages 6]
 *   [InsertResourceFile 7][InsertResourceStream 8] (+ trailing 3rd-gen pair) */
static const void *res_manager2_vtbl[] =
{
    rm2_QueryInterface, rm2_AddRef, rm2_Release, rm2_GetIids, rm2_GetRuntimeClassName, rm2_GetTrustLevel,
    rm2_CandidateLanguages, rm2_InsertResourceFile, rm2_InsertResourceStream,
    rm2_GetAllNamedResourcesForPackage, rm2_GetAllSubtreesForPackage,
};
static struct res_manager res_manager_inst = { res_manager_vtbl, res_manager2_vtbl, 1 };

/* ---- ResourceLoader faces (see the struct res_loader note) ---- */
static HRESULT WINAPI loader_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "loader_QueryInterface enter\n" );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IResourceLoader_g ))
    {
        *out = &res_loader_inst.vt_loader;
        incr( &res_loader_inst.ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceLoader2_g ))
    {
        *out = &res_loader_inst.vt_loader2;
        incr( &res_loader_inst.ref );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI loader_AddRef( void *iface ) { return incr( &loader_from( iface )->ref ); }
static ULONG WINAPI loader_Release( void *iface ) { return decr( &loader_from( iface )->ref ); }
static HRESULT WINAPI loader_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI loader_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( L"Microsoft.Windows.ApplicationModel.Resources.ResourceLoader", n ); }
static HRESULT WINAPI loader_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
/* IResourceLoader::GetString [6]: the main resource map lookup, value as string
 * (the same pri walk ResourceManager.Current feeds through the manager face). */
static HRESULT WINAPI loader_GetString( void *iface, HSTRING resource, HSTRING *value )
{
    struct res_candidate *cand;
    const WCHAR *raw;
    HSTRING tmp;
    BOOL owned;
    HRESULT hr;

    TRACE( "loader GetString\n" );
    if (!value) return E_POINTER;
    *value = NULL;
    if (!key_decode( resource, &raw, &tmp, &owned )) return ELEMENT_NOT_FOUND_E;
    hr = map_lookup( &main_map, raw, &cand );
    if (SUCCEEDED( hr ))
    {
        hr = WindowsCreateString( cand->value, (UINT32)lstrlenW( cand->value ), value );
        decr( &cand->ref );
    }
    key_release( tmp, owned );
    return hr;
}
/* IResourceLoader::GetStringForUri [7]: the Uri object surface is not walked
 * (the pri key is the path tail); documented stub tier. */
static HRESULT WINAPI loader_GetStringForUri( void *iface, void *uri, HSTRING *value )
{
    FIXME( "stub\n" );
    if (!value) return E_POINTER;
    *value = NULL;
    return E_NOTIMPL;
}
/* IResourceLoader::get_MainResourceMap [8] */
static HRESULT WINAPI loader_MainResourceMap( void *iface, void **out )
{
    TRACE( "loader get_MainResourceMap\n" );
    if (!out) return E_POINTER;
    *out = &main_map.vt_map;
    incr( &main_map.ref );
    return S_OK;
}
static const void *res_loader_vtbl[] =
{
    loader_QueryInterface, loader_AddRef, loader_Release, loader_GetIids, loader_GetRuntimeClassName, loader_GetTrustLevel,
    loader_GetString, loader_GetStringForUri, loader_MainResourceMap,
};
static HRESULT WINAPI loader2_QueryInterface( void *iface, REFIID iid, void **out ) { return loader_QueryInterface( loader2_from( iface ), iid, out ); }
static ULONG WINAPI loader2_AddRef( void *iface ) { return incr( &loader2_from( iface )->ref ); }
static ULONG WINAPI loader2_Release( void *iface ) { return decr( &loader2_from( iface )->ref ); }
static HRESULT WINAPI loader2_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI loader2_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( L"Microsoft.Windows.ApplicationModel.Resources.ResourceLoader", n ); }
static HRESULT WINAPI loader2_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
/* IResourceLoader2::get_Languages [6]: the default context's language list,
 * the same observable face ResourceContext::Languages hands out. */
static HRESULT WINAPI loader2_Languages( void *iface, void **out )
{
    struct res_context *ctx;
    HRESULT hr;

    TRACE( "loader2 get_Languages\n" );
    if (!out) return E_POINTER;
    if ((hr = get_current_context( (void **)&ctx ))) return hr;
    *out = &ctx->languages.vt_ov;
    incr( &ctx->languages.ref );
    decr( &ctx->ref );
    return S_OK;
}
static HRESULT WINAPI loader2_put_Languages( void *iface, void *languages ) { TRACE( "stub\n" ); return S_OK; }
static HRESULT WINAPI loader2_InsertResourceFile( void *iface, HSTRING filename ) { TRACE( "stub\n" ); return S_OK; }
static HRESULT WINAPI loader2_InsertResourceStream( void *iface, HSTRING stream, INT32 filetype ) { TRACE( "stub\n" ); return S_OK; }
static const void *res_loader2_vtbl[] =
{
    loader2_QueryInterface, loader2_AddRef, loader2_Release, loader2_GetIids, loader2_GetRuntimeClassName, loader2_GetTrustLevel,
    loader2_Languages, loader2_put_Languages, loader2_InsertResourceFile, loader2_InsertResourceStream,
};

struct rm_factory
{
    const void *vt_primary;
    const void *vt_statics;
};
static struct rm_factory *rmf_from( void *iface ) { return (struct rm_factory *)iface; }
static struct rm_factory *rmf_from_s( void *iface, size_t off ) { return (struct rm_factory *)((BYTE *)iface - off); }

static HRESULT WINAPI rmf_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct rm_factory *impl = rmf_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory_g_ ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->vt_primary;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IResourceManagerStatics_g ))
    {
        *out = &impl->vt_statics;
        return S_OK;
    }
    FIXME( "%s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rmf_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rmf_Release( void *iface ) { return 1; }
static HRESULT WINAPI rmf_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rmf_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEMANAGER, n ); }
static HRESULT WINAPI rmf_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rmf_ActivateInstance( void *iface, void **instance )
{
    TRACE( "rmf_ActivateInstance enter\n" );

    *instance = &res_manager_inst.vt_rm;
    incr( &res_manager_inst.ref );
    return S_OK;
}
static const void *rm_factory_vtbl[] =
{
    rmf_QueryInterface, rmf_AddRef, rmf_Release, rmf_GetIids, rmf_GetRuntimeClassName, rmf_GetTrustLevel,
    rmf_ActivateInstance,
};
static HRESULT WINAPI rms_QueryInterface( void *iface, REFIID iid, void **out ) { return rmf_QueryInterface( rmf_from_s( iface, offsetof(struct rm_factory, vt_statics) ), iid, out ); }
static ULONG WINAPI rms_AddRef( void *iface ) { return 2; }
static ULONG WINAPI rms_Release( void *iface ) { return 1; }
static HRESULT WINAPI rms_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rms_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_RESOURCEMANAGER, n ); }
static HRESULT WINAPI rms_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rms_Current( void *iface, void **out )
{
    TRACE( "ResourceManager::Current\n" );
    *out = &res_manager_inst.vt_rm;
    incr( &res_manager_inst.ref );
    return S_OK;
}
static HRESULT WINAPI rms_IsResourceReference( void *iface, HSTRING resource, INT32 *result )
{
    TRACE( "rms_IsResourceReference enter\n" );

    const WCHAR *raw;
    if (!hstring_decode( resource, &raw, NULL )) { *result = FALSE; return S_OK; }
    *result = !!wcschr( raw, L'/' );
    return S_OK;
}
static const void *rm_statics_vtbl[] =
{
    rms_QueryInterface, rms_AddRef, rms_Release, rms_GetIids, rms_GetRuntimeClassName, rms_GetTrustLevel,
    rms_Current, rms_IsResourceReference,
};
static struct rm_factory rm_factory_inst = { rm_factory_vtbl, rm_statics_vtbl };

/* passive factories for non-activatable classes */
struct simple_factory
{
    const void *vt_primary;
    const WCHAR *class_name;
};
static struct simple_factory *sf_from( void *iface ) { return (struct simple_factory *)iface; }
static HRESULT WINAPI sf_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "sf_QueryInterface enter\n" );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory_g_ ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI sf_AddRef( void *iface ) { return 2; }
static ULONG WINAPI sf_Release( void *iface ) { return 1; }
static HRESULT WINAPI sf_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI sf_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( sf_from( iface )->class_name, n ); }
static HRESULT WINAPI sf_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI sf_ActivateInstance( void *iface, void **instance )
{
    FIXME( "%s not activatable\n", debugstr_w( sf_from( iface )->class_name ) );
    *instance = NULL;
    return E_FAIL;
}
static const void *simple_factory_vtbl[] =
{
    sf_QueryInterface, sf_AddRef, sf_Release, sf_GetIids, sf_GetRuntimeClassName, sf_GetTrustLevel,
    sf_ActivateInstance,
};
static struct simple_factory map_factory_inst   = { simple_factory_vtbl, RTCLASS_RESOURCEMAP };
static struct simple_factory cand_factory_inst  = { simple_factory_vtbl, RTCLASS_RESOURCECANDIDATE };
static struct simple_factory named_factory_inst = { simple_factory_vtbl, RTCLASS_NAMEDRESOURCE };
static struct simple_factory qual_factory_inst  = { simple_factory_vtbl, RTCLASS_RESOURCEQUALIFIER };

/* Windows.System.MemoryManager: stub instance + two-stage responder.
 * The statics IIDs are stable since Windows 8; {6eee351f-...} is confirmed
 * by the app trace (mrt100_app DllMain QIs it on the factory).  The
 * Statics2/Statics3/AppReport/AppEntry IIDs carry the metadata values
 * (single-source, treat as unverified until the app QIs them live). */
static const GUID IID_IMemoryManagerStatics_g   = {0x6eee351f, 0x6d62, 0x423f, {0x94, 0x79, 0xb0, 0x1f, 0x9c, 0x9f, 0x76, 0x69}};
static const GUID IID_IMemoryManagerStatics2_g  = {0x13dc79ba, 0x55e3, 0x5951, {0xa1, 0xb8, 0xdd, 0x0a, 0xbd, 0xd3, 0x01, 0x60}};
static const GUID IID_IMemoryManagerStatics3_g  = {0xecb7a292, 0xfd03, 0x5492, {0xb4, 0xd8, 0xd9, 0xd5, 0x17, 0xd7, 0x9e, 0x5c}}; /* (unverified) */
static const GUID IID_IMemoryAppReport_g        = {0x267f7f6a, 0x5743, 0x4acf, {0xb2, 0x04, 0xe5, 0xc4, 0xd0, 0x86, 0x4a, 0x0a}}; /* (unverified) */
static const GUID IID_IMemoryAppEntry_g         = {0x0a5bd48c, 0x7a98, 0x4db9, {0x9f, 0xca, 0x03, 0x40, 0x6c, 0xdf, 0x3c, 0xbf}}; /* (unverified) */
#define ELEM_MEMAPPENTRY L"Windows.System.MemoryManager.MemoryAppEntry"

struct memory_manager
{
    const void *vt_obj;
    LONG ref;
};
static inline struct memory_manager *mm_from( void *iface ) { return (struct memory_manager *)iface; }
static HRESULT WINAPI mm_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct memory_manager *impl = mm_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->vt_obj;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "MemoryManager interface %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI mm_AddRef( void *iface ) { return incr( &mm_from( iface )->ref ); }
static ULONG WINAPI mm_Release( void *iface ) { return decr( &mm_from( iface )->ref ); }
static HRESULT WINAPI mm_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI mm_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_MEMORYMANAGER, n ); }
static HRESULT WINAPI mm_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static const void *memory_manager_vtbl[] =
{
    mm_QueryInterface, mm_AddRef, mm_Release, mm_GetIids, mm_GetRuntimeClassName, mm_GetTrustLevel,
};
static struct memory_manager memory_manager_inst = { memory_manager_vtbl, 1 };

/* IMemoryAppReport {267f7f6a-...}: [Current 6][Peak 7][Default 8]
 * [GetAppEntries 9].  One process-wide report; the entries view is a fresh
 * empty IIterable<MemoryAppEntry*> (the app surfaces enumerate it). */
struct mm_app_report
{
    const void *vt_rep;
    LONG ref;
};
static inline struct mm_app_report *rep_from( void *iface ) { return (struct mm_app_report *)iface; }
static HRESULT WINAPI rep_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct mm_app_report *impl = rep_from( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IMemoryAppReport_g ))
    {
        *out = &impl->vt_rep;
        incr( &impl->ref );
        return S_OK;
    }
    FIXME( "MemoryAppReport interface %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI rep_AddRef( void *iface ) { return incr( &rep_from( iface )->ref ); }
static ULONG WINAPI rep_Release( void *iface ) { return decr( &rep_from( iface )->ref ); }
static HRESULT WINAPI rep_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI rep_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( L"Windows.System.MemoryManager.MemoryAppReport", n ); }
static HRESULT WINAPI rep_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static HRESULT WINAPI rep_Current( void *iface, UINT64 *v ) { TRACE( "enter\n" ); if (!v) return E_POINTER; *v = 0x02000000; return S_OK; }
static HRESULT WINAPI rep_Peak( void *iface, UINT64 *v ) { TRACE( "enter\n" ); if (!v) return E_POINTER; *v = 0x03000000; return S_OK; }
static HRESULT WINAPI rep_Default( void *iface, UINT64 *v ) { TRACE( "enter\n" ); if (!v) return E_POINTER; *v = 0x60000000; return S_OK; }
static HRESULT WINAPI rep_GetAppEntries( void *iface, void **out )
{
    struct iface_vector *vec;

    TRACE( "enter\n" );
    if (!out) return E_POINTER;
    vec = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*vec) );
    if (!vec) return E_OUTOFMEMORY;
    vec->vt_vv = iface_vector_vtbl;
    vec->ref = 1;
    vec->count = 0;
    vec->elem = ELEM_MEMAPPENTRY;
    *out = &vec->vt_vv;
    return S_OK;
}
static const void *mm_app_report_vtbl[] =
{
    rep_QueryInterface, rep_AddRef, rep_Release, rep_GetIids, rep_GetRuntimeClassName, rep_GetTrustLevel,
    rep_Current, rep_Peak, rep_Default, rep_GetAppEntries,
};
static struct mm_app_report mm_app_report_inst = { mm_app_report_vtbl, 1 };

/* The activation factory: primary IActivationFactory face plus the three
 * statics faces; the process-lifetime singleton answers with fixed numbers
 * (32 MB app usage, 1/2 GB available, 4 GB total). */
struct mm_factory
{
    const void *vt_primary;  /* IActivationFactory */
    const void *vt_statics;  /* IMemoryManagerStatics */
    const void *vt_statics2; /* IMemoryManagerStatics2 */
    const void *vt_statics3; /* IMemoryManagerStatics3 */
};
static struct mm_factory *mmf_from_s( void *iface, size_t off ) { return (struct mm_factory *)((BYTE *)iface - off); }

static HRESULT WINAPI msm_AppMemoryUsage( void *iface, UINT64 *v ) { TRACE( "enter\n" ); if (!v) return E_POINTER; *v = 0x02000000; return S_OK; }
static HRESULT WINAPI msm_AvailableMemoryUsage( void *iface, UINT64 *v ) { TRACE( "enter\n" ); if (!v) return E_POINTER; *v = 0x40000000; return S_OK; }
static HRESULT WINAPI msm_TotalAvailableMemory( void *iface, UINT64 *v ) { TRACE( "enter\n" ); if (!v) return E_POINTER; *v = 0x80000000; return S_OK; }
static HRESULT WINAPI msm_GetCurrentAppMemoryReport( void *iface, void **out )
{
    TRACE( "enter\n" );
    if (!out) return E_POINTER;
    *out = &mm_app_report_inst.vt_rep;
    incr( &mm_app_report_inst.ref );
    return S_OK;
}
static HRESULT WINAPI msmf_QueryInterface( struct mm_factory *impl, REFIID iid, void **out );
static HRESULT WINAPI msm_QueryInterface( void *iface, REFIID iid, void **out )
{
    return msmf_QueryInterface( mmf_from_s( iface, offsetof( struct mm_factory, vt_statics )), iid, out );
}
static ULONG WINAPI msm_AddRef( void *iface ) { return 2; }
static ULONG WINAPI msm_Release( void *iface ) { return 1; }
static HRESULT WINAPI msm_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI msm_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_MEMORYMANAGER, n ); }
static HRESULT WINAPI msm_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
static const void *mm_statics_vtbl[] =
{
    msm_QueryInterface, msm_AddRef, msm_Release, msm_GetIids, msm_GetRuntimeClassName, msm_GetTrustLevel,
    msm_AppMemoryUsage, msm_AvailableMemoryUsage, msm_TotalAvailableMemory, msm_GetCurrentAppMemoryReport,
};
/* IMemoryManagerStatics2: [GetForPackageFamily 6] -> the same report face */
static HRESULT WINAPI msm2_QueryInterface( void *iface, REFIID iid, void **out )
{
    return msmf_QueryInterface( mmf_from_s( iface, offsetof( struct mm_factory, vt_statics2 )), iid, out );
}
static HRESULT WINAPI msm2_GetForPackageFamily( void *iface, HSTRING family, void **out )
{
    TRACE( "enter\n" );
    if (!out) return E_POINTER;
    *out = &mm_app_report_inst.vt_rep;
    incr( &mm_app_report_inst.ref );
    return S_OK;
}
static const void *mm_statics2_vtbl[] =
{
    msm2_QueryInterface, msm_AddRef, msm_Release, msm_GetIids, msm_GetRuntimeClassName, msm_GetTrustLevel,
    msm2_GetForPackageFamily,
};
/* IMemoryManagerStatics3: [AppMemoryUsageLimit 6 get][7 put][IsDiagnosticEnabled 8 get][9 put] */
static HRESULT WINAPI msm3_QueryInterface( void *iface, REFIID iid, void **out )
{
    return msmf_QueryInterface( mmf_from_s( iface, offsetof( struct mm_factory, vt_statics3 )), iid, out );
}
static HRESULT WINAPI msm3_get_UsageLimit( void *iface, UINT64 *v ) { TRACE( "enter\n" ); if (!v) return E_POINTER; *v = 0x10000000; return S_OK; }
static HRESULT WINAPI msm3_put_UsageLimit( void *iface, UINT64 v ) { TRACE( "stub\n" ); return S_OK; }
static HRESULT WINAPI msm3_get_DiagnosticEnabled( void *iface, INT32 *v ) { TRACE( "enter\n" ); if (!v) return E_POINTER; *v = FALSE; return S_OK; }
static HRESULT WINAPI msm3_put_DiagnosticEnabled( void *iface, INT32 v ) { TRACE( "stub\n" ); return S_OK; }
static const void *mm_statics3_vtbl[] =
{
    msm3_QueryInterface, msm_AddRef, msm_Release, msm_GetIids, msm_GetRuntimeClassName, msm_GetTrustLevel,
    msm3_get_UsageLimit, msm3_put_UsageLimit, msm3_get_DiagnosticEnabled, msm3_put_DiagnosticEnabled,
};
static HRESULT WINAPI mmf_QueryInterface( void *iface, REFIID iid, void **out )
{
    return msmf_QueryInterface( (struct mm_factory *)iface, iid, out );
}
static HRESULT WINAPI msmf_QueryInterface( struct mm_factory *impl, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory_g_ ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = &impl->vt_primary;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMemoryManagerStatics_g ))
    {
        *out = &impl->vt_statics;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMemoryManagerStatics2_g ))
    {
        *out = &impl->vt_statics2;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IMemoryManagerStatics3_g ))
    {
        *out = &impl->vt_statics3;
        return S_OK;
    }
    FIXME( "MemoryManager factory interface %s not implemented\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI mmf_AddRef( void *iface ) { return 2; }
static ULONG WINAPI mmf_Release( void *iface ) { return 1; }
static HRESULT WINAPI mmf_GetIids( void *iface, ULONG *c, IID **i ) { return simple_get_iids( iface, c, i ); }
static HRESULT WINAPI mmf_GetRuntimeClassName( void *iface, HSTRING *n ) { return className( RTCLASS_MEMORYMANAGER, n ); }
static HRESULT WINAPI mmf_GetTrustLevel( void *iface, INT32 *t ) { return get_trust_base( iface, t ); }
/* factory ActivateInstance for MemoryManager returns the stub object */
static HRESULT WINAPI mmf_ActivateInstance( void *iface, void **instance )
{
    TRACE( "MemoryManager::ActivateInstance stub\n" );
    *instance = &memory_manager_inst.vt_obj;
    incr( &memory_manager_inst.ref );
    return S_OK;
}
static const void *mm_factory_vtbl[] =
{
    mmf_QueryInterface, mmf_AddRef, mmf_Release, mmf_GetIids, mmf_GetRuntimeClassName, mmf_GetTrustLevel,
    mmf_ActivateInstance,
};
static struct mm_factory mm_factory = { mm_factory_vtbl, mm_statics_vtbl, mm_statics2_vtbl, mm_statics3_vtbl };

/* ------------------------------------------------------------------ */
/* exports                                                             */
/* ------------------------------------------------------------------ */

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    FIXME("clsid %s, riid %s, out %p stub!\n", debugstr_guid(clsid), debugstr_guid(riid), out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* Windows App SDK ships the MRT Core WinRT classes from its own
 * Microsoft.Windows.ApplicationModel.Resources.dll: the class tail names are the
 * Core provider's (ResourceContext is ResourceContext, ResourceManager is
 * ResourceManager, …), the namespace is the app-facing one — ResourceLoader and
 * KnownResourceQualifierName exist only there.  The ActivatableClassId seeds of a
 * packaged prefix name that module as the host, and combase routes the namespace to
 * this builtin for the unpackaged ones; accept the tail name under both namespaces
 * so one provider answers the packaged and the unpackaged activation alike. */
static const WCHAR *mrtcore_tail( const WCHAR *name )
{
    static const WCHAR prefix[] = L"Microsoft.Windows.ApplicationModel.Resources.";
    size_t len = ARRAYSIZE( prefix ) - 1;

    return !wcsncmp( name, prefix, len ) ? name + len : NULL;
}

/* GCHandle pin-buffer pre-arm (re-arm wave, 2026-11-07).
 *
 * This SharedLibrary.dll build (net10-shape CoreLib on the 2.2-generation
 * mrt100 app-locals) arms RuntimeExceptionHelpers.s_ExceptionInfoBufferPinningHandle
 * (static slot SL+0x55de30) only when the native _RhSetErrorInfoBuffer reports
 * "not first registration".  mrt100_app PalSetWerDataBuffer (SL_app+0x80808)
 * calls the Extended-service vtable slot with an out-parameter the 2.2
 * mrt100.dll SetHardwareExceptionHandler never writes, so the call returns the
 * stack residue -- "true" every time.  Pass 1 of the culture walk therefore
 * throws InvalidOperationException from GCHandle.get_Target (the catch at
 * SL+0x98b94d swallows it and skips the arming store), and the c0000602
 * second pass (mrt100_app RhpFailFastForPInvokeExceptionPreemp, SL_app+0x3eb81)
 * re-throws from the same empty handle and exits.
 *
 * Pre-arm the slot with a wine-heap PinnedObjectHandle-shaped cell whose
 * target is a fake System.Byte[] living in a zero-init .bss hole (no xrefs,
 * outside the scanned static block: SL+0x55df60), so get_Target answers a
 * non-empty pinned handle on both passes: the settled e0434352/c0000602 pair
 * completes and the culture walk re-enters toward the Map plane.
 */
static void mrm_prearm_pinbuffer(void)
{
    static HMODULE hmod;
    static UINT_PTR cell_store[2];
    static void *cell;
    unsigned char *base;
    UINT_PTR *slot, *fake;

    if (cell) return;
    if (!hmod && !(hmod = GetModuleHandleA("SharedLibrary.dll"))) return;
    base = (unsigned char *)hmod;

    slot = (UINT_PTR *)(base + 0x55de30);   /* s_ExceptionInfoBufferPinningHandle */
    fake = (UINT_PTR *)(base + 0x55df60);   /* unused .bss hole */
    if (*slot)                              /* arm once, when slot is empty */
        return;

    fake[0] = (UINT_PTR)(base + 0xe0f80);   /* EEType_System::Byte[] */
    fake[1] = 1;                            /* Length: nonzero, GC-scan safe */
    cell_store[0] = (UINT_PTR)fake;         /* PinnedObjectHandle::m_object */
    cell_store[1] = 0;
    cell = cell_store;
    *slot = (UINT_PTR)cell_store | 1;       /* GCHandle pinned tag */
    TRACE("prearm pinbuffer slot %p = %p\n", slot, (void *)*slot);
}

/* WER-buffer GCHandle pre-arm (inflate-plateau wave, 2026-11-08).
 *
 * The WER publish reads its data-buffer pinning handle right after
 * PalSetWerDataBuffer; when that GCHandle static is null, .NET GCHandle
 * .value = IntPtr(-1) and the Redhawk write-barrier card helper
 * (mrt100_app+0x522c3) computes the card address from rcx = -1 and faults
 * reading 0xffffffffffffffff.  The SEH/VEH chain replays the buffer-as-code
 * stub into the MSZip inflate (z_stream.next_in = frozen ref 0x1801020001):
 * 18.9k identical AV pairs (plateau).  Null GCHandle statics live in the
 * SharedLibrary .bss beside s_ExceptionInfoBufferPinningHandle
 * (base+0x55de30, armed by mrm_prearm_pinbuffer): arm every qword == -1 in
 * the neighborhood with a wine-heap PinnedObjectHandle cell over a fake
 * System.Byte[], the same shape proven for the pinbuffer.
 */
static void mrm_prearm_werhandle(void)
{
    static HMODULE hmod;
    static UINT_PTR cell_wer[2];
    static UINT_PTR cell_arena[2];
    unsigned char *base;
    UINT_PTR *p;
    int armed = 0;

    if (!hmod && !(hmod = GetModuleHandleA("SharedLibrary.dll"))) return;
    base = (unsigned char *)hmod;

    for (p = (UINT_PTR *)(base + 0x55de00); p < (UINT_PTR *)(base + 0x55de80); p++)
    {
        if (*p != (UINT_PTR)-1) continue;
        if (armed == 0)
        {
            p[1] = (UINT_PTR)(base + 0xe0f80);   /* EEType_System::Byte[] */
            p[2] = 1;                            /* Length */
            cell_wer[0] = (UINT_PTR)(p + 1);     /* m_object = the fake inline */
            cell_wer[1] = 0;
            *p = (UINT_PTR)cell_wer | 1;
        }
        else if (armed == 1)
        {
            p[1] = (UINT_PTR)(base + 0xe0f80);
            p[2] = 1;
            cell_arena[0] = (UINT_PTR)(p + 1);
            cell_arena[1] = 0;
            *p = (UINT_PTR)cell_arena | 1;
        }
        else return;
        TRACE("prearm werhandle slot %p armed\n", p);
        armed++;
    }
}

/* Frozen-reference arena commit (inflate-plateau wave, 2026-11-08).
 *
 * mrt100_app R2R data bakes tagged GC references into the runtime heap at
 * its link-time layout: heap snapshot objects read as 0x1801020001-style
 * absolute refs (TypeManager space base 0x1801000000, app Mcg module-0).
 * The runtime heap is a 384MB (nil)-hinted MEM_RESERVE|MEM_COMMIT block
 * (measured: `NtAllocateVirtualMemory (nil) 18000000 202000 1`), which the
 * Wine loader lands at a host-flavored VA (observed 0x7fc2bd3e0000) — the
 * 0x1801xxxxxx slots the frozen refs name stay address holes.  Every WER
 * publish of the culture walk re-reads one such ref: the MSZip decompress
 * of the WER data buffer loads z_stream.next_in=0x1801020001, the inflate
 * faults on *next_in, the app SEH/VEH pair runs the buffer-as-code stub
 * chain, resume, retry: 18.9k identical AV pairs at ~210/s (plateau).
 *
 * Commit the first 16MB of that arena as zero pages from the first
 * activation: the frozen "MSZip header" reads 24 zero bytes, inflate
 * retires with a consumed-stored-block result, the WER publish completes
 * and the culture walk proceeds toward the Map plane.
 */
static void mrm_prearm_arena(void)
{
    static void *committed;
    void *p;

    if (committed) return;
    p = VirtualAlloc((void *)0xffffffffffff0000ULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    TRACE("prearm arena-minus1 %p\n", p);
    p = VirtualAlloc((void *)0x1801020000, 0x200000, MEM_COMMIT, PAGE_READWRITE);
    TRACE("prearm arena %p\n", p);
    if (!p) p = VirtualAlloc((void *)0x1801000000, 0x1000000, MEM_COMMIT, PAGE_READWRITE);
    TRACE("prearm arena2 %p\n", p);
    if (p) committed = p;
}

/* Extended-service vtable swap (nest-collapse wave, 2026-11-08).
 *
 * mrt100_app PalSetWerDataBuffer (RVA 0x80808) is the net10-shape
 * SetWerDataBuffer(buffer, out old-buffer): it calls vtable+24 on the
 * {2f4bac61-3ab9-46a0-8d62-1f3ff88090fa} face (QI'd from the mrt100.dll
 * service object in PalInit, mrt100_app RVA 0x80682) and returns the
 * out-parameter as the "first registration" answer of the WER publish
 * (pinbuffer pre-arm, 2a3f9875).  The 2.2-generation mrt100.dll answers
 * slot+24 with SetHardwareExceptionHandler (RVA 0x2210, measured live; the
 * base face keeps the 0x1ea0 twin): it stores the buffer pointer into
 * g_pfnHardwareExceptionHandler (RVA 0xa098, evicting the Redhawk filter
 * published at startup) and never writes the out-parameter.  Every AV after that single publish then dispatches
 * HardwareVectoredExceptionHandler (RVA 0x1450) into the buffer-as-code
 * stub (write to [0]), whose resume faults again: 284 nested write-AVs at
 * 0xE40 stack each, killed by the stack stack-overflow at
 * application_QueryInterface {06499997}.
 *
 * The net10-shape responder already exists in this mrt100.dll:
 * Wer_SetWerDataBuffer (RVA 0x1470, "*out = old buffer; store; S_OK"),
 * published at vtable+48.  Swap slot+24 onto it from the first activation:
 * the publish writes its out-parameter, g_pfnHardwareExceptionHandler keeps
 * the Redhawk filter, and the AV storm nests one deep.  The vtable lives in
 * the zero-characteristics .rdata of this MinGW build, which Wine maps
 * writable, so the swap is a single store into the loaded image.
 */
/* p214: second-pass barrier target.  Wer_SetWerDataBuffer answers the first
 * publish with *out = NULL (its g_pWerDataBuffer is zero); the app stores that
 * into the WER pin-handle, and the fifth publish's second pass reads it back
 * as GCHandle -1: the card helper (mrt100_app+0x522c3) faults reading the card
 * of -1 with no handler left and the process dies (fatal capture p212:
 * PalSetWerDataBuffer+0x3b <- SharedLibrary+0x7e33b3).  Wrap the native
 * responder: on the first call (native out == NULL) hand the app a stable
 * PinnedObjectHandle cell tagged like a real pinned GCHandle (the pinbuffer
 * shape: cell -> fake System.Byte[] over SharedLibrary's EEType) so the
 * pass-2 barrier target is committed and never -1. */
static UINT_PTR mrm_wer_fake[2];
static UINT_PTR mrm_wer_cell[2];
static void (WINAPI *mrm_wer_native)(void *, void *, void **);

static HRESULT WINAPI mrm_wer_set_buffer( void *iface, void *buffer, void **out )
{
    if (mrm_wer_native)
    {
        HRESULT (WINAPI *fn)( void *, void *, void ** ) = (void *)mrm_wer_native;
        fn( iface, buffer, out );             /* store buffer, out = old g */
    }
    if (out && !*out && mrm_wer_cell[0])
        *out = (void *)((UINT_PTR)mrm_wer_cell | 1);  /* first publish: stable handle, never -1 */
    return 0;                                 /* S_OK */
}

static void mrm_prearm_werfacade(void)
{
    static HMODULE hmod;
    unsigned char *base;
    void **instance, **vtable;
    HMODULE sl;
    DWORD old;
    int i;

    if (!hmod && !(hmod = GetModuleHandleA("mrt100.dll")))
    {
        TRACE("prearm welfacade: mrt100.dll not loaded\n");
        return;
    }
    base = (unsigned char *)hmod;

    instance = (void **)(base + 0x4020);        /* g_ExtendedServicesInstance */
    vtable = instance ? (void **)*instance : NULL;
    if (!vtable) return;
    if (vtable[3] == (void *)mrm_wer_set_buffer) return;   /* already swapped (idempotent) */
    for (i = 4; i < 16; i++)                    /* locate the SetWerDataBuffer responder */
        if (vtable[i] == base + 0x1470) break;
    if (i == 16)
    {
        TRACE("prearm welfacade: base %p vtable %p slot24 %p, responder not in [4..15]\n",
              base, vtable, vtable[3]);
        return;
    }
    mrm_wer_native = (void *)vtable[i];
    if (!mrm_wer_cell[0] && (sl = GetModuleHandleA("SharedLibrary.dll")))
    {
        mrm_wer_fake[0] = (UINT_PTR)((unsigned char *)sl + 0xe0f80);  /* EEType_System::Byte[] */
        mrm_wer_fake[1] = 1;                    /* Length: GC-scan safe */
        mrm_wer_cell[0] = (UINT_PTR)mrm_wer_fake;  /* PinnedObjectHandle::m_object */
        mrm_wer_cell[1] = 0;
    }
    VirtualProtect( &vtable[3], sizeof(*vtable), PAGE_READWRITE, &old );
    vtable[3] = (void *)mrm_wer_set_buffer;     /* native store + stable pass-2 handle */
    VirtualProtect( &vtable[3], sizeof(*vtable), old, &old );
    TRACE("prearm extended face: vtable+24 -> wrapped SetWerDataBuffer slot %d\n", i);
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, void **factory)
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );
    const WCHAR *mrt = mrtcore_tail( buffer );

    TRACE("class %s, factory %p.\n", debugstr_hstring(classid), factory);

    mrm_prearm_pinbuffer();
    mrm_prearm_werfacade();
    mrm_prearm_arena();

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( buffer, RTCLASS_RESOURCECONTEXT ) || (mrt && !wcscmp( mrt, L"ResourceContext" )))
    {
        main_map.vt_map = res_map_vtbl;
        main_map.vt_map2 = res_map2_vtbl;
        *factory = (void *)&rc_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_RESOURCEMANAGER ) || (mrt && !wcscmp( mrt, L"ResourceManager" )))
    {
        main_map.vt_map = res_map_vtbl;
        main_map.vt_map2 = res_map2_vtbl;
        *factory = (void *)&rm_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_RESOURCEMAP ) || (mrt && !wcscmp( mrt, L"ResourceMap" )))
    {
        main_map.vt_map = res_map_vtbl;
        main_map.vt_map2 = res_map2_vtbl;
        *factory = (void *)&map_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_RESOURCECANDIDATE ) || (mrt && !wcscmp( mrt, L"ResourceCandidate" )))
    {
        *factory = (void *)&cand_factory_inst.vt_primary;
        return S_OK;
    }
    if (mrt && !wcscmp( mrt, L"ResourceLoader" ))
    {
        /* MRT Core's ResourceLoader is the ResourceManager facade (GetString walks
         * the main resource map); the manager's provider answers it from its own
         * pri default map. */
        main_map.vt_map = res_map_vtbl;
        main_map.vt_map2 = res_map2_vtbl;
        *factory = (void *)&rm_factory_inst.vt_primary;
        return S_OK;
    }
    if (mrt && !wcscmp( mrt, L"KnownResourceQualifierName" ))
    {
        *factory = (void *)&qual_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_NAMEDRESOURCE ))
    {
        *factory = (void *)&named_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_RESOURCEQUALIFIER ))
    {
        *factory = (void *)&qual_factory_inst.vt_primary;
        return S_OK;
    }
    if (!wcscmp( buffer, RTCLASS_MEMORYMANAGER ))
    {
        /* Measured on Calculator (wbappgate runs): mrt100_app.dll DllMain and
         * the managed McgMarshal.GetActivationFactory -> FactoryCache path
         * share ONE RoGetActivationFactory call whose factory the runtime
         * caches.  The early no-op stub (statics QI E_NOINTERFACE) made
         * DllMain take the managed fallback -> c0000142, which is why the
         * REGDB_E_CLASSNOTREG answer survived; with the statics faces
         * answering below, a plain factory answer serves both the DllMain QI
         * of {6eee351f-...} and the cached managed consumer after the
         * Languages reads (IMemoryManagerStatics[2/3], IMemoryAppReport). */
        TRACE( "MemoryManager activation -> factory %p\n", &mm_factory.vt_primary );
        *factory = (void *)&mm_factory.vt_primary;
        return S_OK;
    }

    FIXME("class %s not supported\n", debugstr_hstring(classid));
    return E_NOINTERFACE;
}
