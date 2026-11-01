/*
 * Windows Calculator Resources.Core (PRM/MRT) layer tests
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
 * Tests for the Windows.ApplicationModel.Resources.Core activation surface
 * consumed by .NET Native / MRT apps (Windows Calculator).
 *
 * The interface IIDs and vtable slot ordering below were derived from the
 * official Windows.WinRT metadata (Windows 10 19041 generation, via the
 * windows-rs generated bindings):
 *
 *   IResourceContext          {2fa22f4b-707e-4b27-ad0d-d0d8cd468fd2}
 *     slots after IInspectable: QualifierValues, Reset, ResetQualifierValues,
 *                               OverrideToMatch, Clone, Languages, SetLanguages
 *   IResourceContextStatics2  {41f752ef-12af-41b9-ab36-b1eb4b512460}
 *     slots on factory after ActivateInstance: GetForCurrentView,
 *       SetGlobalQualifierValue, ResetGlobalQualifierValues,
 *       ResetGlobalQualifierValuesForSpecifiedQualifiers, GetForViewIndependentUse
 *   IResourceManagerStatics   {1cc0fdfc-69ee-4e43-9901-47f12687baf7}
 *     slots on factory after ActivateInstance: Current, IsResourceReference
 *   IResourceManager          {f744d97b-9988-44fb-abd6-5378844cfa8b}
 *     slots: MainResourceMap, AllResourceMaps, DefaultContext,
 *            LoadPriFiles, UnloadPriFiles
 *   IResourceMap              {72284824-db8c-42f8-b08c-53ff357dad82}
 *     slots: Uri, GetValue, GetValueForContext, GetSubtree
 *   IResourceCandidate        {af5207d9-c433-4764-b3fd-8fa6bfbcbadc}
 *     slots: Qualifiers, IsMatch, IsMatchAsDefault, IsDefault,
 *            ValueAsString, GetValueAsFileAsync, GetQualifierValue
 *
 * The slot numbers embedded in the FailFast stack of the Calculator run
 * (qword 41b912af41f752ef at failfast_stack[14]) match
 * IResourceContextStatics2, confirming these are the interfaces the
 * .NET Native MRT engine queries.
 */

#define COBJMACROS
#include "initguid.h"
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "ole2.h"

#include "roapi.h"

#include "wine/test.h"

static const IID IID_IActivationFactory_loc = {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID IID_IResourceContext_loc = {0x2fa22f4b, 0x707e, 0x4b27, {0xad, 0x0d, 0xd0, 0xd8, 0xcd, 0x46, 0x8f, 0xd2}};
static const IID IID_IResourceContextStatics2_loc = {0x41f752ef, 0x12af, 0x41b9, {0xab, 0x36, 0xb1, 0xeb, 0x4b, 0x51, 0x24, 0x60}};
static const IID IID_IResourceManagerStatics_loc = {0x1cc0fdfc, 0x69ee, 0x4e43, {0x99, 0x01, 0x47, 0xf1, 0x26, 0x87, 0xba, 0xf7}};
static const IID IID_IResourceManager_loc = {0xf744d97b, 0x9988, 0x44fb, {0xab, 0xd6, 0x53, 0x78, 0x84, 0x4c, 0xfa, 0x8b}};
static const IID IID_IResourceMap_loc = {0x72284824, 0xdb8c, 0x42f8, {0xb0, 0x8c, 0x53, 0xff, 0x35, 0x7d, 0xad, 0x82}};
static const IID IID_IResourceCandidate_loc = {0xaf5207d9, 0xc433, 0x4764, {0xb3, 0xfd, 0x8f, 0xa6, 0xbf, 0xbc, 0xba, 0xdc}};
static const IID IID_IInspectable_loc = {0xaf86e2e0, 0xbcdf, 0x11ce, {0x9c, 0x40, 0x00, 0xaa, 0x00, 0x3d, 0xd9, 0x21}};
/* v1-era interface identities, (unverified) like the module's 8.1 entries:
 * IResourceMap2 [II 5][GetByIndex 6][GetCount 7][GetFullCount 8][FullGetByIndex 9]
 * IResourceContext2 [II 5][SetQualifierValue 6][TrySetName 7][TrySetPackage 8][SetAllQualifiers 9]
 * The module and this test agree on these constants; the app-facing surfaces
 * (IResourceMap GetValue/GetValueForContext, IResourceContext) are verified. */
static const IID IID_IResourceMap2_loc = {0xd4a52cd1, 0xc6a5, 0x4824, {0x94, 0x53, 0xcc, 0x86, 0x18, 0x5b, 0x6f, 0xd4}};
/* objidl!IAgileObject (free-threaded marker, no methods beyond IUnknown);
 * mrt100_app's QI chain probes it on every WinRT object it touches */
static const IID IID_IAgileObject_loc = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const IID IID_IResourceContext2_loc = {0xb6d312f1, 0xbc62, 0x4208, {0xb8, 0xa1, 0xb8, 0x42, 0xc0, 0xc4, 0xbb, 0x54}};

/* vtable slot helpers; IInspectable occupies slots 0..5 (3 IUnknown + 3 extra) */
#define VT(obj, idx) (((void ***)((char *)(obj)))[0][(idx)])
#define CALL(obj, idx, ...) ((HRESULT (STDMETHODCALLTYPE *)(void *, ...))VT(obj, idx))((void *)(obj), __VA_ARGS__)

static HRESULT get_factory(const WCHAR *classid, void **factory)
{
    HSTRING_HEADER hdr;
    HSTRING hcls;
    HRESULT hr = WindowsCreateStringReference(classid, (UINT32)lstrlenW(classid), &hdr, &hcls);
    if (FAILED(hr)) return hr;
    hr = RoGetActivationFactory(hcls, &IID_IActivationFactory_loc, factory);
    WindowsDeleteString(hcls);
    return hr;
}

static void test_resourcecontext_activation(void)
{
    void *factory = NULL, *ctx = NULL, *ctx2 = NULL;
    HSTRING lang = NULL, qualmap = NULL;
    HRESULT hr;

    hr = get_factory(L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory);
    ok(hr == S_OK, "RoGetActivationFactory(ResourceContext) hr=%08x\n", hr);
    if (hr != S_OK) return;

    /* the .NET Native MRT engine QIs the factory for IResourceContextStatics2
     * (confirmed: its GUID appears in the failfast stack) and calls GetForCurrentView */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceContextStatics2_loc, &ctx);
    if (hr == S_OK)
    {
        /* IResourceContextStatics2 layout on the QI'd pointer: [0..5 IInspectable][6 GetForCurrentView] */
        hr = CALL(ctx, 6, &ctx2);
        ok(SUCCEEDED(hr), "IResourceContextStatics2::GetForCurrentView hr=%08x\n", hr);
    }
    else
    {
        /* fall back to plain activation */
        trace("no IResourceContextStatics2 on factory (hr=%08x), falling back to ActivateInstance\n", hr);
        hr = CALL(factory, 6, &ctx2);
        ok(SUCCEEDED(hr), "IActivationFactory::ActivateInstance(ResourceContext) hr=%08x\n", hr);
    }
    ok(ctx2 != NULL, "ResourceContext instance NULL\n");
    if (!ctx2) { if (ctx) CALL(ctx, 2, (ULONG*)&hr); return; }

    /* the instance must answer QI for IResourceContext */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(ctx2, 0))(ctx2, &IID_IResourceContext_loc, &ctx);
    ok(hr == S_OK && ctx, "QI IResourceContext hr=%08x ctx=%p\n", hr, ctx);

    if (ctx)
    {
        /* IResourceContext slot 6: QualifierValues (IMapView<HSTRING,HSTRING>) */
        hr = CALL(ctx, 6, &qualmap);
        ok(SUCCEEDED(hr) && qualmap, "get_QualifierValues hr=%08x\n", hr);
        if (qualmap)
        {
            UINT32 size = 0;
            /* IMapView slots: [0..5 II][6 Get][7 HasKey][8 Size][9 GetView] */
            hr = CALL(qualmap, 8, &size);
            ok(SUCCEEDED(hr) && size > 0, "QualifierValues size=%u hr=%08x\n", size, hr);
            CALL(qualmap, 2, (ULONG*)&hr);
        }
        /* slot 11: Languages (IVectorView<HSTRING>) */
        hr = CALL(ctx, 11, &lang);
        ok(SUCCEEDED(hr) && lang, "get_Languages hr=%08x\n", hr);
        if (lang)
        {
            UINT32 size = 0;
            /* IVectorView slots: [0..5 II][6 GetAt][7 Size][8 GetView] */
            hr = CALL(lang, 7, &size);
            ok(SUCCEEDED(hr) && size >= 1, "Languages size=%u hr=%08x\n", size, hr);
            CALL(lang, 2, (ULONG*)&hr);
        }
        CALL(ctx, 2, (ULONG*)&hr);
    }
    if (ctx2) CALL(ctx2, 2, (ULONG*)&hr);
    if (ctx) {}
    CALL(factory, 2, (ULONG*)&hr);
}

/* Point the module's PRI reader at the tests/resources.pri fixture.
 * __FILE__ is recorded by the compiler relative to the compile directory
 * (the module's build directory), which is also the working directory the
 * test runner uses, so GetFullPathNameA resolves it to the source tree. */
static void set_pri_env(void)
{
    char path[MAX_PATH * 2];
    char full[MAX_PATH * 2];
    char *slash;

    lstrcpyA(path, __FILE__);
    slash = strrchr(path, '/');
    if (!slash) slash = strrchr(path, '\\');
    if (slash) slash[1] = 0;
    lstrcatA(path, "resources.pri");
    if (GetFullPathNameA(path, sizeof(full), full, NULL))
        SetEnvironmentVariableA("WINE_MRT_PRI_PATH", full);
}

static void test_resourcemanager_lookup(void)
{
    void *factory = NULL, *rm = NULL, *map = NULL, *cand = NULL;
    HSTRING_HEADER khdr;
    HSTRING hkey, value = NULL;
    HRESULT hr;

    hr = get_factory(L"Windows.ApplicationModel.Resources.Core.ResourceManager", &factory);
    ok(hr == S_OK, "RoGetActivationFactory(ResourceManager) hr=%08x\n", hr);
    if (hr != S_OK) return;

    /* factory QI for IResourceManagerStatics -> slot 6 Current */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceManagerStatics_loc, &rm);
    ok(hr == S_OK && rm, "QI IResourceManagerStatics hr=%08x\n", hr);
    if (hr != S_OK) goto done_factory;
    hr = CALL(rm, 6, &rm);
    ok(SUCCEEDED(hr) && rm, "IResourceManagerStatics::Current hr=%08x\n", hr);
    if (FAILED(hr) || !rm) goto done_factory;

    /* ResourceManager QI IResourceManager, slot 6 MainResourceMap */
    {
        void *rm2 = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(rm, 0))(rm, &IID_IResourceManager_loc, &rm2);
        ok(hr == S_OK && rm2, "QI IResourceManager hr=%08x\n", hr);
        if (hr != S_OK) goto done_rm;
        hr = CALL(rm2, 6, &map);
        ok(SUCCEEDED(hr) && map, "get_MainResourceMap hr=%08x\n", hr);
        CALL(rm2, 2, (ULONG*)&hr);
    }
    if (!map) goto done_rm;

    /* IResourceMap slot 7: GetValue(key) -> IResourceCandidate */
    WindowsCreateStringReference(L"Calculator", 10, &khdr, &hkey);
    hr = CALL(map, 7, hkey, &cand);
    ok(SUCCEEDED(hr) && cand, "MainResourceMap::GetValue(\"Calculator\") hr=%08x\n", hr);
    if (SUCCEEDED(hr) && cand)
    {
        void *cand2 = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(cand, 0))(cand, &IID_IResourceCandidate_loc, &cand2);
        ok(hr == S_OK && cand2, "QI IResourceCandidate hr=%08x\n", hr);
        if (SUCCEEDED(hr) && cand2)
        {
            /* slot 10: get_ValueAsString */
            hr = CALL(cand2, 10, &value);
            ok(SUCCEEDED(hr) && value, "get_ValueAsString hr=%08x\n", hr);
            if (SUCCEEDED(hr) && value)
            {
                UINT32 len = WindowsGetStringLen(value);
                ok(len > 0, "value string empty\n");
                trace("value for Calculator: %s\n", debugstr_w(WindowsGetStringRawBuffer(value, NULL)));
                WindowsDeleteString(value);
            }
            CALL(cand2, 2, (ULONG*)&hr);
        }
        CALL(cand, 2, (ULONG*)&hr);
    }
    WindowsDeleteString(hkey);
    CALL(map, 2, (ULONG*)&hr);
done_rm:
    CALL(rm, 2, (ULONG*)&hr);
done_factory:
    CALL(factory, 2, (ULONG*)&hr);
}

/* read a candidate's ValueAsString into buf; returns string length or -1 */
static int cand_get_value( void *cand, WCHAR *buf, DWORD buflen )
{
    void *cand2 = NULL;
    HSTRING value = NULL;
    HRESULT hr;
    int len = -1;

    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(cand, 0))(cand, &IID_IResourceCandidate_loc, &cand2);
    if (FAILED( hr ) || !cand2) return -1;
    hr = CALL( cand2, 10, &value );  /* get_ValueAsString */
    if (SUCCEEDED( hr ) && value)
    {
        const WCHAR *raw = WindowsGetStringRawBuffer( value, NULL );
        len = raw ? lstrlenW( raw ) : 0;
        if (buf && (DWORD)len < buflen) memcpy( buf, raw, (len + 1) * sizeof(*buf) );
        WindowsDeleteString( value );
    }
    CALL( cand2, 2, (ULONG*)&hr );
    return len;
}

/* read a candidate qualifier value; returns length or -1 */
static int cand_qualifier( void *cand, const WCHAR *qual, WCHAR *buf, DWORD buflen )
{
    void *cand2 = NULL;
    HSTRING hname = NULL, hvalue = NULL;
    HRESULT hr;
    int len = -1;

    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(cand, 0))(cand, &IID_IResourceCandidate_loc, &cand2);
    if (FAILED( hr ) || !cand2) return -1;
    WindowsCreateString( qual, (UINT32)lstrlenW( qual ), &hname );
    hr = CALL( cand2, 12, hname, &hvalue );  /* GetQualifierValue */
    if (SUCCEEDED( hr ) && hvalue)
    {
        const WCHAR *raw = WindowsGetStringRawBuffer( hvalue, NULL );
        len = raw ? lstrlenW( raw ) : 0;
        if (buf && (DWORD)len < buflen) memcpy( buf, raw, (len + 1) * sizeof(*buf) );
        WindowsDeleteString( hvalue );
    }
    WindowsDeleteString( hname );
    CALL( cand2, 2, (ULONG*)&hr );
    return len;
}

static BOOL contains( const WCHAR *hay, const WCHAR *needle )
{
    return !!wcsstr( hay, needle );
}

/*
 * Real-lookups test against the Microsoft.WindowsCalculator resources.pri
 * fixture (608760 bytes).  Ground-truth expectations decoded from the
 * fixture's res_map2 / hschemaex / decn_info / dataitem sections:
 *   - main resource map holds 1323 items (661 scopes);
 *   - "Resources\CalculatorModeText" is a string item with EN-US/EN-GB
 *     candidates, value "Calculator";
 *   - subtree "Resources" holds 1166 items;
 *   - "Files\Assets\CalculatorAppList.png" has 129 file-path candidates
 *     qualified by Contrast / TargetSize / AltForm.
 */
static void test_real_pri_lookup( void )
{
    void *factory = NULL, *rm = NULL, *map = NULL, *map2 = NULL;
    void *cand = NULL, *ctx = NULL, *ctx2 = NULL, *sub = NULL;
    WCHAR buf[2048], val[4096];
    UINT32 count = 0;
    HRESULT hr;

    set_pri_env();

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceManager", &factory );
    ok( hr == S_OK, "fixture: RoGetActivationFactory(ResourceManager) hr=%08x\n", hr );
    if (hr != S_OK) return;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceManagerStatics_loc, &rm);
    if (FAILED( hr )) goto done;
    hr = CALL( rm, 6, &rm );  /* IResourceManagerStatics::Current */
    if (FAILED( hr ) || !rm) goto done;
    {
        void *rm2 = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(rm, 0))(rm, &IID_IResourceManager_loc, &rm2);
        if (FAILED( hr )) goto done;
        hr = CALL( rm2, 6, &map );  /* get_MainResourceMap */
        CALL( rm2, 2, (ULONG*)&hr );
    }
    ok( SUCCEEDED( hr ) && map, "fixture: get_MainResourceMap hr=%08x\n", hr );
    if (FAILED( hr ) || !map) goto done;

    /* IResourceMap2: GetCount over the real map */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(map, 0))(map, &IID_IResourceMap2_loc, &map2);
    ok( hr == S_OK && map2, "fixture: QI IResourceMap2 hr=%08x\n", hr );
    if (FAILED( hr ) || !map2) goto done;
    hr = CALL( map2, 7, &count );  /* GetCount */
    ok( SUCCEEDED( hr ) && count == 1323, "fixture: MainResourceMap::GetCount = %u hr=%08x (expect 1323)\n", count, hr );

    /* GetByIndex(0): CEngineStrings\10 (first item by index property) */
    cand = NULL;
    hr = CALL( map2, 6, 0, &cand );  /* GetByIndex */
    ok( SUCCEEDED( hr ) && cand, "fixture: GetByIndex(0) hr=%08x\n", hr );
    if (SUCCEEDED( hr ) && cand)
    {
        int len = cand_get_value( cand, val, ARRAYSIZE( val ) );
        ok( len > 0, "fixture: GetByIndex(0) value length %d\n", len );
        CALL( cand, 2, (ULONG*)&hr );
    }
    /* out-of-range index */
    cand = (void *)0x1;
    hr = CALL( map2, 6, count + 10, &cand );
    ok( hr == E_BOUNDS || FAILED( hr ), "fixture: GetByIndex(oob) hr=%08x\n", hr );
    if (cand && cand != (void *)0x1) CALL( cand, 2, (ULONG*)&hr );

    /* GetByKey via IResourceMap::GetValue on a verified key */
    {
        HSTRING hkey;
        HSTRING_HEADER hdr;
        WindowsCreateStringReference( L"Resources\\CalculatorModeText", 28, &hdr, &hkey );
        cand = NULL;
        hr = CALL( map, 7, hkey, &cand );  /* GetValue */
        ok( SUCCEEDED( hr ) && cand, "fixture: GetValue(Resources\\CalculatorModeText) hr=%08x\n", hr );
        WindowsDeleteString( hkey );
        if (SUCCEEDED( hr ) && cand)
        {
            int len = cand_get_value( cand, val, ARRAYSIZE( val ) );
            ok( len == 10 && !wcscmp( val, L"Calculator" ), "fixture: CalculatorModeText value '%s' len %d\n",
                len > 0 ? val : L"(none)", len );
            len = cand_qualifier( cand, L"Language", buf, ARRAYSIZE( buf ) );
            ok( len > 0, "fixture: candidate Language qualifier len %d\n", len );
            CALL( cand, 2, (ULONG*)&hr );
        }
    }

    /* GetSubtree("Resources") -> scoped count + relative lookup */
    {
        HSTRING hname;
        HSTRING_HEADER hdr;
        void *sub2 = NULL;
        WindowsCreateStringReference( L"Resources", 9, &hdr, &hname );
        hr = CALL( map, 9, hname, &sub );  /* GetSubtree */
        WindowsDeleteString( hname );
        ok( SUCCEEDED( hr ) && sub, "fixture: GetSubtree(Resources) hr=%08x\n", hr );
        if (SUCCEEDED( hr ) && sub)
        {
            hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(sub, 0))(sub, &IID_IResourceMap2_loc, &sub2);
            if (SUCCEEDED( hr ) && sub2)
            {
                UINT32 subcount = 0;
                hr = CALL( sub2, 7, &subcount );
                ok( SUCCEEDED( hr ) && subcount == 1166, "fixture: Resources subtree count = %u (expect 1166) hr=%08x\n",
                    subcount, hr );
                CALL( sub2, 2, (ULONG*)&hr );
            }
            {
                HSTRING hkey;
                HSTRING_HEADER khdr;
                WindowsCreateStringReference( L"CalculatorModeText", 18, &khdr, &hkey );
                cand = NULL;
                hr = CALL( sub, 7, hkey, &cand );
                WindowsDeleteString( hkey );
                ok( SUCCEEDED( hr ) && cand, "fixture: subtree GetValue(CalculatorModeText) hr=%08x\n", hr );
                if (SUCCEEDED( hr ) && cand)
                {
                    int len = cand_get_value( cand, val, ARRAYSIZE( val ) );
                    ok( len == 10 && !wcscmp( val, L"Calculator" ), "fixture: subtree value '%s' len %d\n",
                        len > 0 ? val : L"(none)", len );
                    CALL( cand, 2, (ULONG*)&hr );
                }
            }
        }
    }

    /* ResourceContext qualifier filtering: create a context, set Language=en-GB,
     * and the resolved candidate must carry the EN-GB qualifier. */
    {
        void *cfactory = NULL;
        HSTRING hval, hname;
        hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &cfactory );
        ok( hr == S_OK, "fixture: RoGetActivationFactory(ResourceContext) hr=%08x\n", hr );
        if (hr == S_OK)
        {
            hr = CALL( cfactory, 6, &ctx );  /* ActivateInstance */
            ok( SUCCEEDED( hr ) && ctx, "fixture: ResourceContext ActivateInstance hr=%08x\n", hr );
        }
        if (ctx)
        {
            hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(ctx, 0))(ctx, &IID_IResourceContext2_loc, &ctx2);
            ok( hr == S_OK && ctx2, "fixture: QI IResourceContext2 hr=%08x\n", hr );
            if (SUCCEEDED( hr ) && ctx2)
            {
                WindowsCreateString( L"en-GB", 5, &hval );
                WindowsCreateString( L"Language", 8, &hname );
                hr = CALL( ctx2, 6, hval, hname );  /* SetQualifierValue(value, name) */
                ok( SUCCEEDED( hr ), "fixture: SetQualifierValue(en-GB, Language) hr=%08x\n", hr );
                WindowsDeleteString( hval );
                WindowsDeleteString( hname );
                CALL( ctx2, 2, (ULONG*)&hr );
            }
            {
                HSTRING hkey;
                HSTRING_HEADER khdr;
                WindowsCreateStringReference( L"Resources\\CalculatorModeText", 28, &khdr, &hkey );
                cand = NULL;
                hr = CALL( map, 8, hkey, ctx, &cand );  /* GetValueForContext */
                WindowsDeleteString( hkey );
                ok( SUCCEEDED( hr ) && cand, "fixture: GetValueForContext(en-GB) hr=%08x\n", hr );
                if (SUCCEEDED( hr ) && cand)
                {
                    int len = cand_qualifier( cand, L"Language", buf, ARRAYSIZE( buf ) );
                    ok( len == 5 && !wcsicmp( buf, L"en-GB" ), "fixture: EN-GB candidate selected, qualifier '%s' len %d\n",
                        len > 0 ? buf : L"(none)", len );
                    CALL( cand, 2, (ULONG*)&hr );
                }
            }
        }
    }

    /* TargetSize/Contrast/AltForm filtering on the Files subtree */
    if (ctx)
    {
        void *cfactory2 = NULL;
        hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &cfactory2 );
        if (ctx2) ctx2 = NULL;
        {
            void *ctx3 = NULL;
            HSTRING hval, hname;
            hr = CALL( cfactory2, 6, &ctx3 );
            if (SUCCEEDED( hr ) && ctx3)
            {
                hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(ctx3, 0))(ctx3, &IID_IResourceContext2_loc, &ctx2);
                if (SUCCEEDED( hr ) && ctx2)
                {
                    WindowsCreateString( L"White", 5, &hval );
                    WindowsCreateString( L"Contrast", 8, &hname );
                    CALL( ctx2, 6, hval, hname );
                    WindowsDeleteString( hval );
                    WindowsDeleteString( hname );
                    WindowsCreateString( L"96", 2, &hval );
                    WindowsCreateString( L"TargetSize", 10, &hname );
                    CALL( ctx2, 6, hval, hname );
                    WindowsDeleteString( hval );
                    WindowsDeleteString( hname );
                    WindowsCreateString( L"UNPLATED", 8, &hval );
                    WindowsCreateString( L"AltForm", 7, &hname );
                    CALL( ctx2, 6, hval, hname );
                    WindowsDeleteString( hval );
                    WindowsDeleteString( hname );
                }
                {
                    HSTRING hkey;
                    HSTRING_HEADER khdr;
                    static const WCHAR key[] = L"Files\\Assets\\CalculatorAppList.png";
                    WindowsCreateStringReference( key, (UINT32)(sizeof(key)/sizeof(WCHAR) - 1), &khdr, &hkey );
                    cand = NULL;
                    hr = CALL( map, 8, hkey, ctx3, &cand );
                    WindowsDeleteString( hkey );
                    ok( SUCCEEDED( hr ) && cand, "fixture: GetValueForContext(Files item) hr=%08x\n", hr );
                    if (SUCCEEDED( hr ) && cand)
                    {
                        int len = cand_get_value( cand, val, ARRAYSIZE( val ) );
                        ok( len > 0 && contains( val, L"targetsize-96" ) && contains( val, L"contrast-white" )
                           && contains( val, L"altform-unplated" ),
                           "fixture: filtered path value '%s' len %d\n", len > 0 ? val : L"(none)", len );
                        CALL( cand, 2, (ULONG*)&hr );
                    }
                }
                CALL( ctx3, 2, (ULONG*)&hr );
            }
        }
    }

done:
    if (ctx2) CALL( ctx2, 2, (ULONG*)&hr );
    if (ctx) CALL( ctx, 2, (ULONG*)&hr );
    if (sub) CALL( sub, 2, (ULONG*)&hr );
    if (map2) CALL( map2, 2, (ULONG*)&hr );
    if (map) CALL( map, 2, (ULONG*)&hr );
    CALL( factory, 2, (ULONG*)&hr );
}

/* mrt100_app QIs IAgileObject {94ea2b94-e9cc-49e0-c0ff-ee64ca8f5b90} on the
 * ResourceContext factory during startup (observed live QI chain); a miss
 * makes combase apartment-marshal the factory.  Every module object answers
 * it with its primary interface. */
static void test_iagile( void )
{
    void *factory = NULL, *obj = NULL, *cand = NULL;
    HRESULT hr;

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "iagile: ResourceContext factory hr=%08x\n", hr );
    if (hr != S_OK) return;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IAgileObject_loc, &obj);
    ok( hr == S_OK && obj, "iagile: QI factory hr=%08x obj=%p\n", hr, obj );
    if (obj) CALL( obj, 2, (ULONG*)&hr );
    obj = NULL;

    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceContextStatics2_loc, &obj);
    if (SUCCEEDED( hr ) && obj)
    {
        void *obj2 = NULL;
        hr = CALL( obj, 6, &cand );  /* GetForCurrentView */
        if (SUCCEEDED( hr ) && cand)
        {
            void *c2 = NULL;
            hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(cand, 0))(cand, &IID_IAgileObject_loc, &c2);
            ok( hr == S_OK && c2, "iagile: QI context hr=%08x\n", hr );
            if (c2) CALL( c2, 2, (ULONG*)&hr );
            CALL( cand, 2, (ULONG*)&hr );
            cand = NULL;
        }
        CALL( obj, 2, (ULONG*)&hr );
    }
    CALL( factory, 2, (ULONG*)&hr );
}

/* mrt100_app probes IInspectable::GetIids with NULL out-params; the module
 * must answer E_POINTER, not write through NULL (measured: an access
 * violation here starts mrt100's catch/rethrow loop -> managed stack
 * overflow right after the second ResourceContext activation). */
static void test_getiids_nullargs( void )
{
    static const IID IID_IResourceContext_v81_loc = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
    void *factory = NULL, *v81 = NULL;
    ULONG count = 0xdead;
    IID *iids = (void *)0x1;
    HRESULT hr;

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "getiids: factory hr=%08x\n", hr );
    if (hr != S_OK) return;

    /* normal call succeeds */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, ULONG *, IID **))VT(factory, 3))(factory, &count, &iids);
    ok( hr == S_OK, "getiids: normal GetIids hr=%08x\n", hr );
    CoTaskMemFree( iids );

    /* probe call: NULL count + NULL array must not crash */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, ULONG *, IID **))VT(factory, 3))(factory, NULL, NULL);
    ok( hr == E_POINTER, "getiids: factory GetIids(NULL,NULL) hr=%08x (expect E_POINTER)\n", hr );

    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceContext_v81_loc, &v81);
    if (SUCCEEDED( hr ) && v81)
    {
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, ULONG *, IID **))VT(v81, 3))(v81, NULL, NULL);
        ok( hr == E_POINTER, "getiids: IResourceContext(v81) GetIids(NULL,NULL) hr=%08x (expect E_POINTER)\n", hr );
        /* mrt100_app measured calling CreateWithCurrentView(view, NULL)
         * (AV at rc81_CreateWithCurrentView+0x28 -> catch/rethrow loop) */
        hr = CALL( v81, 7, (void *)0x1, NULL );  /* CreateWithCurrentView(view, NULL) */
        ok( hr == E_POINTER, "getiids: CreateWithCurrentView(view,NULL) hr=%08x (expect E_POINTER)\n", hr );
        hr = CALL( v81, 6, NULL );  /* Create(NULL) */
        ok( hr == E_POINTER, "getiids: Create(NULL) hr=%08x (expect E_POINTER)\n", hr );
        CALL( v81, 2, (ULONG*)&hr );
    }
    hr = CALL( factory, 6, NULL );  /* IActivationFactory::ActivateInstance(NULL) */
    ok( hr == E_POINTER, "getiids: ActivateInstance(NULL) hr=%08x (expect E_POINTER)\n", hr );
    CALL( factory, 2, (ULONG*)&hr );

    /* same probe pattern on collection count slots: mrt100_app measured
     * calling IResourceMap2::GetCount(NULL) (AV at pri_map_count) */
    set_pri_env();
    {
        void *rmfactory = NULL, *rm = NULL, *map = NULL, *map2 = NULL, *ctx = NULL, *lang = NULL;
        hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceManager", &rmfactory );
        if (hr == S_OK)
        {
            hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(rmfactory, 0))(rmfactory, &IID_IResourceManagerStatics_loc, &rm);
            if (SUCCEEDED( hr )) CALL( rm, 6, &rm );  /* Current */
            if (rm)
            {
                void *rm2 = NULL;
                hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(rm, 0))(rm, &IID_IResourceManager_loc, &rm2);
                if (SUCCEEDED( hr )) CALL( rm2, 6, &map );  /* get_MainResourceMap */
                CALL( rm2, 2, (ULONG*)&hr );
            }
            if (map)
            {
                hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(map, 0))(map, &IID_IResourceMap2_loc, &map2);
                if (SUCCEEDED( hr ) && map2)
                {
                    hr = CALL( map2, 7, NULL );  /* GetCount(NULL) */
                    ok( hr == E_POINTER, "getiids: IResourceMap2::GetCount(NULL) hr=%08x (expect E_POINTER)\n", hr );
                    CALL( map2, 2, (ULONG*)&hr );
                }
                CALL( map, 2, (ULONG*)&hr );
            }
            CALL( rm, 2, (ULONG*)&hr );
            CALL( rmfactory, 2, (ULONG*)&hr );
        }
        /* IVectorView::Size(NULL) on the context Languages view */
        hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
        if (hr == S_OK)
        {
            hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceContextStatics2_loc, &ctx);
            if (SUCCEEDED( hr ) && ctx)
            {
                void *view = NULL;
                hr = CALL( ctx, 6, &view );  /* GetForCurrentView */
                if (SUCCEEDED( hr ) && view)
                {
                    hr = CALL( view, 11, &lang );  /* get_Languages: IVectorView<HSTRING> */
                    if (SUCCEEDED( hr ) && lang)
                    {
                        hr = CALL( lang, 7, NULL );  /* Size(NULL) */
                        ok( hr == E_POINTER, "getiids: Languages Size(NULL) hr=%08x (expect E_POINTER)\n", hr );
                        CALL( lang, 2, (ULONG*)&hr );
                    }
                    CALL( view, 2, (ULONG*)&hr );
                }
                CALL( ctx, 2, (ULONG*)&hr );
            }
            CALL( factory, 2, (ULONG*)&hr );
        }
    }
}

static void test_memorymanager_activation(void)
{
    void *factory = NULL, *obj = NULL;
    HRESULT hr;

    /* The module intentionally answers REGDB_E_CLASSNOTREG for this class:
     * .NET Native mrt100_app.dll's DllMain only survives the not-registered
     * failure code (measured: a stub factory answering the activation makes
     * mrt100's DllMain throw -> STATUS_DLL_INIT_FAILED c0000142, aborting
     * Calculator startup).  The no-op stub path below documents the
     * activation surface for callers that do not depend on that quirk. */
    hr = get_factory(L"Windows.System.MemoryManager", &factory);
    todo_wine
    ok(hr == S_OK, "RoGetActivationFactory(Windows.System.MemoryManager) hr=%08x\n", hr);
    if (hr != S_OK) return;

    hr = CALL(factory, 6, &obj);
    ok(hr == S_OK && obj, "MemoryManager ActivateInstance hr=%08x obj=%p\n", hr);
    if (obj) CALL(obj, 2, (ULONG*)&hr);
    CALL(factory, 2, (ULONG*)&hr);
}

START_TEST(core)
{
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize hr=%08x\n", hr);

    test_resourcecontext_activation();
    set_pri_env();
    test_resourcemanager_lookup();
    test_real_pri_lookup();
    test_iagile();
    test_getiids_nullargs();
    test_memorymanager_activation();

    RoUninitialize();
}
