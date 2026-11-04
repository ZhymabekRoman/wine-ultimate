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
#include <stddef.h>
#include <string.h>

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
/* IInspectable as declared in this tree's include/inspectable.idl
 * (uuid af86e2e0-b12d-4c6a-9c5a-d7aa65101e90); the module's QI compares the
 * same constant -- a local copy with the DCOM-template tail (bcdf-11ce...)
 * QIs E_NOINTERFACE against the deployed module (verified). */
static const IID IID_IInspectable_loc = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
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

/* IMapView::Get miss: same constant as the module's ELEMENT_NOT_FOUND_E */
#define ELEMENT_NOT_FOUND_HR HRESULT_FROM_WIN32(0x490L)

/* vtable slot helpers; IInspectable occupies slots 0..5 (3 IUnknown + 3 extra) */
#define VT(obj, idx) (((void ***)((char *)(obj)))[0][(idx)])
#define CALL(obj, idx, ...) ((HRESULT (STDMETHODCALLTYPE *)(void *, ...))VT(obj, idx))((void *)(obj), __VA_ARGS__)
#define QI_SLOT(obj, iid, out) ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(obj, 0))((void *)(obj), iid, out)

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
            /* IMapView slots (idl:201-205 + delegated IIterable):
             * [0..5 II][6 Get][7 Size][8 HasKey][9 Split][10 First] */
            hr = CALL(qualmap, 7, &size);
            ok(SUCCEEDED(hr) && size > 0, "QualifierValues size=%u hr=%08x\n", size, hr);
            CALL(qualmap, 2, (ULONG*)&hr);
        }
        /* slot 11: Languages (IObservableVector<HSTRING> primary face) */
        hr = CALL(ctx, 11, &lang);
        ok(SUCCEEDED(hr) && lang, "get_Languages hr=%08x\n", hr);
        if (lang)
        {
            UINT32 size = 0;
            /* IObservableVector slots: [0..5 II][6 add][7 remove][8 GetAt][9 Size] */
            hr = CALL(lang, 9, &size);
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
    static const IID IID_IReferenceTracker_loc = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
    void *factory = NULL, *rt = NULL;
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

    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IReferenceTracker_loc, &rt);
    if (SUCCEEDED( hr ) && rt)
    {
        /* IReferenceTracker slots carry no out-params except
         * FindTrackerTargets/GetReferenceTrackerManager, whose NULL probes
         * must be E_POINTER.  The no-argument notifications (slots 3/7 -- the
         * two measured AV sites) must ignore the caller's argument registers
         * entirely, so passing junk here is both legal and harmless. */
        hr = CALL( rt, 5, NULL );  /* FindTrackerTargets(NULL) */
        ok( hr == E_POINTER, "getiids: FindTrackerTargets(NULL) hr=%08x (expect E_POINTER)\n", hr );
        hr = CALL( rt, 6, NULL );  /* GetReferenceTrackerManager(NULL) */
        ok( hr == E_POINTER, "getiids: GetReferenceTrackerManager(NULL) hr=%08x (expect E_POINTER)\n", hr );
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *))VT(rt, 3))( rt, (void *)0x1, (void *)0x2 );
        ok( hr == S_OK, "getiids: ConnectFromTrackerSource hr=%08x (expect S_OK, no writes)\n", hr );
        CALL( rt, 2, (ULONG*)&hr );
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
                    hr = CALL( view, 11, &lang );  /* get_Languages: the IObservableVector<HSTRING> face */
                    if (SUCCEEDED( hr ) && lang)
                    {
                        void *langs = NULL;
                        /* the observable face is [add 6][remove 7] then the delegated
                         * IVector block (idl:265-269), so GetView is slot 10 and the
                         * IVectorView face it returns has its own Size at slot 7 */
                        hr = CALL( lang, 10, &langs );
                        if (SUCCEEDED( hr ) && langs)
                        {
                            hr = CALL( langs, 7, NULL );  /* IVectorView::Size(NULL) */
                            ok( hr == E_POINTER, "getiids: IVectorView Size@7(NULL) hr=%08x (expect E_POINTER)\n", hr );
                            CALL( langs, 2, (ULONG*)&hr );
                        }
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

/* ResourceContext.QualifierValues documented core set (MSDN
 * Windows.ApplicationModel.Resources.Core.ResourceContext):
 * Language, Contrast, TargetSize, Scale, LayoutDirection, HomeRegion,
 * DXLanguage, AltForm, UserLanguage, UserHomeRegion, UserLocaleName,
 * DeviceFamily.  mrt100_app's context pipeline reads these keys. */
static void test_qualifier_set( void )
{
    static const WCHAR *core[] = { L"Language", L"Contrast", L"TargetSize", L"Scale",
                                   L"LayoutDirection", L"HomeRegion", L"DXLanguage", L"AltForm",
                                   L"UserLanguage", L"UserHomeRegion", L"UserLocaleName", L"DeviceFamily" };
    void *factory = NULL, *statics = NULL, *ctx = NULL, *mv = NULL;
    HRESULT hr;
    UINT32 i;

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "quals: factory hr=%08x\n", hr );
    if (hr != S_OK) return;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceContextStatics2_loc, &statics);
    if (FAILED( hr )) goto done;
    hr = CALL( statics, 6, &ctx );  /* GetForCurrentView */
    if (FAILED( hr ) || !ctx) goto done;
    hr = CALL( ctx, 6, &mv );  /* get_QualifierValues: IMapView<HSTRING,HSTRING> */
    if (FAILED( hr ) || !mv) goto done;
    for (i = 0; i < ARRAYSIZE( core ); i++)
    {
        HSTRING hkey;
        HSTRING_HEADER hdr;
        INT32 has = 0;
        WindowsCreateStringReference( core[i], (UINT32)lstrlenW( core[i] ), &hdr, &hkey );
        hr = CALL( mv, 8, hkey, &has );  /* HasKey (idl order: Get 6, Size 7, HasKey 8) */
        ok( SUCCEEDED( hr ) && has, "quals: QualifierValues has %s (has=%d hr=%08x)\n",
            wine_dbgstr_w( core[i] ), has, hr );
        WindowsDeleteString( hkey );
    }
done:
    if (mv) CALL( mv, 2, (ULONG*)&hr );
    if (ctx) CALL( ctx, 2, (ULONG*)&hr );
    if (statics) CALL( statics, 2, (ULONG*)&hr );
    CALL( factory, 2, (ULONG*)&hr );
}

/* The Calculator crash-loop (280x c0000005, sr2.log 47150+) is a module-side
 * interface-identity defect, not caller state: {11d3b13a-...} is
 * IReferenceTracker, and the two AV sites are the module's own writes through
 * registers of a NO-ARGUMENT call (see test_reference_tracker_identity).
 * What is pinned here:
 *  (a) the lazy the_current_context singleton is immune to the
 *      create_context re-entrancy window (DeviceFamily registry read inside
 *      creation): N threads race the cold path, all get the one valid
 *      context, no corruption, no AV;
 *  (b) the app's entry shape (QI IResourceContextStatics2 -> GetForCurrentView
 *      on the cold singleton) survives 400 concurrent drives.
 */
#define HAMMER_THREADS 8
#define HAMMER_ITERS   50

static HANDLE hammer_gate;
static volatile LONG hammer_fail;
static volatile LONG hammer_done;
static volatile LONG hammer_f_statics, hammer_f_gfcv, hammer_f_insp, hammer_f_ident, hammer_f_factory;
static void *hammer_first_ctx;
static void *volatile hammer_seen[HAMMER_THREADS];

static DWORD WINAPI hammer_thread( void *arg )
{
    void *factory = NULL, *statics = NULL;
    ULONG idx = (ULONG)(UINT_PTR)arg;
    HRESULT hr;
    unsigned int i;

    WaitForSingleObject( hammer_gate, 10000 );
    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    if (FAILED( hr )) { InterlockedIncrement( &hammer_f_factory ); InterlockedIncrement( &hammer_fail ); return 0; }

    for (i = 0; i < HAMMER_ITERS; i++)
    {
        void *ctx = NULL, *inspectable = NULL;

        statics = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceContextStatics2_loc, &statics);
        if (FAILED( hr ) || !statics) { InterlockedIncrement( &hammer_f_statics ); InterlockedIncrement( &hammer_fail ); break; }

        /* slot 6 = GetForCurrentView -> get_current_context cold path on the
         * very first call: this is the exact entry the .NET Native engine
         * drives; the create_context window is widest at process start */
        hr = CALL( statics, 6, &ctx );
        if (FAILED( hr ) || !ctx) { InterlockedIncrement( &hammer_f_gfcv ); InterlockedIncrement( &hammer_fail ); CALL( statics, 2, &hr ); break; }

        /* prove the returned context is a real functional object */
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(ctx, 0))(ctx, &IID_IInspectable_loc, &inspectable);
        if (FAILED( hr ) || !inspectable) { InterlockedIncrement( &hammer_f_insp ); InterlockedIncrement( &hammer_fail ); }
        if (inspectable) CALL( inspectable, 2, &hr );

        /* singleton identity: every thread must see the same context pointer */
        InterlockedCompareExchangePointer( &hammer_first_ctx, ctx, NULL );
        hammer_seen[idx] = ctx;
        if (ctx != hammer_first_ctx) { InterlockedIncrement( &hammer_f_ident ); InterlockedIncrement( &hammer_fail ); }

        CALL( ctx, 2, &hr );
        CALL( statics, 2, &hr );
        InterlockedIncrement( &hammer_done );
    }
    CALL( factory, 2, &hr );
    return 0;
}

static void test_current_context_reentrancy( void )
{
    void *factory = NULL, *statics = NULL, *ctx1 = NULL, *ctx2 = NULL;
    HANDLE threads[HAMMER_THREADS];
    unsigned int i, started = 0;
    HRESULT hr;

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "reentrancy: factory hr=%08x\n", hr );
    if (hr != S_OK) return;

    hammer_gate = CreateEventW( NULL, TRUE, FALSE, NULL );
    hammer_fail = 0;
    hammer_done = 0;
    hammer_f_statics = hammer_f_gfcv = hammer_f_insp = hammer_f_ident = hammer_f_factory = 0;
    hammer_first_ctx = NULL;
    for (i = 0; i < HAMMER_THREADS; i++)
    {
        threads[i] = CreateThread( NULL, 0, hammer_thread, (void *)(UINT_PTR)i, 0, NULL );
        if (threads[i]) started++;
    }
    ok( started == HAMMER_THREADS, "reentrancy: started %u/%u threads\n", started, HAMMER_THREADS );
    SetEvent( hammer_gate );
    for (i = 0; i < HAMMER_THREADS; i++)
        if (threads[i])
        {
            WaitForSingleObject( threads[i], 30000 );
            CloseHandle( threads[i] );
        }
    CloseHandle( hammer_gate );

    ok( hammer_fail == 0, "reentrancy: %u failures across %u iterations "
        "(factory=%u statics=%u gfcv=%u insp=%u ident=%u first=%p)\n",
        (unsigned)hammer_fail, (unsigned)hammer_done, (unsigned)hammer_f_factory,
        (unsigned)hammer_f_statics, (unsigned)hammer_f_gfcv, (unsigned)hammer_f_insp,
        (unsigned)hammer_f_ident, hammer_first_ctx );
    ok( hammer_done == HAMMER_THREADS * HAMMER_ITERS, "reentrancy: completed %u/%u iterations\n",
        (unsigned)hammer_done, HAMMER_THREADS * HAMMER_ITERS );
    ok( hammer_first_ctx != NULL, "reentrancy: singleton context published\n" );

    /* warm path: repeated GetForCurrentView + GetForViewIndependentUse
     * (slot 10) must keep handing out the identical singleton */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceContextStatics2_loc, &statics);
    if (SUCCEEDED( hr ) && statics)
    {
        hr = CALL( statics, 6, &ctx1 );
        ok( SUCCEEDED( hr ) && ctx1, "reentrancy: warm GetForCurrentView hr=%08x\n", hr );
        hr = CALL( statics, 10, &ctx2 );  /* GetForViewIndependentUse */
        ok( SUCCEEDED( hr ) && ctx2, "reentrancy: GetForViewIndependentUse hr=%08x\n", hr );
        if (ctx1 && ctx2)
            ok( ctx1 == ctx2, "reentrancy: singleton stable warm (ctx1=%p ctx2=%p)\n", ctx1, ctx2 );
        if (ctx1) CALL( ctx1, 2, &hr );
        if (ctx2) CALL( ctx2, 2, &hr );
        CALL( statics, 2, &hr );
    }
    CALL( factory, 2, &hr );
}

/* The CLR reference tracker walks IReferenceTracker
 * {11d3b13a-180e-4789-a8be-7712882893e6} (windows.ui.xaml.hosting.referencetracker.idl,
 * IUnknown-derived) on every WinRT object .NET Native wraps:
 *   [3] ConnectFromTrackerSource   [4] DisconnectFromTrackerSource
 *   [5] FindTrackerTargets         [6] GetReferenceTrackerManager
 *   [7] AddRefFromTrackerSource    [8] ReleaseFromTrackerSource
 *   [9] PegFromTrackerSource
 * Slots 3, 4, 7, 8 and 9 take NO arguments.  The module used to answer this
 * QI with an IInspectable + Create/CreateWithCurrentView shaped vtable, so the
 * app's no-argument call at slot 3 ran GetIids ("movl $0,(%rdx)") and the
 * no-argument call at slot 7 ran CreateWithCurrentView ("mov %rax,(%rbx)"):
 * both wrote through registers the caller never set (rdx = 0x24, r8 = stale
 * heap) -- exactly the two AVs at 0x1800085ac and 0x18000854d.  A call site
 * that passes no out-params cannot be "caller state", which is what the
 * earlier verdict in this file claimed.
 *
 * The probes below are those two write sites made safe and observable: the
 * arguments point at mapped sentinels, so the old vtable clobbers them (red)
 * and the tracker contract leaves them untouched (green).
 */
static void test_reference_tracker_identity( void )
{
    static const IID IID_IReferenceTracker_loc = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};
    static const IID IID_IReferenceTrackerManager_loc = {0x3cf184b4, 0x7ccb, 0x4dda, {0x84, 0x55, 0x7e, 0x6c, 0xe9, 0x9a, 0x32, 0x98}};
    void *factory = NULL, *rt = NULL, *rt2 = NULL, *inspectable = NULL;
    void *ctx1 = NULL, *ctx2 = NULL, *mgr = NULL, *cb = NULL;
    IID *iids = NULL, *arr;
    ULONG count = 0, i, found_tracker = 0, found_statics2 = 0, found_context = 0;
    ULONG probe = 0x24;                 /* the exact rdx value of AV1 */
    void *sentinel = (void *)0x5b5b5b5b;/* the exact r8 value class of AV2 */
    HSTRING name = NULL;
    const WCHAR *raw;
    HRESULT hr;

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "tracker: factory hr=%08x\n", hr );
    if (hr != S_OK) return;

    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT( factory, 0 ))( factory, &IID_IReferenceTracker_loc, &rt );
    ok( hr == S_OK && rt, "tracker: QI(IReferenceTracker) hr=%08x obj=%p\n", hr, rt );
    if (FAILED( hr ) || !rt) { CALL( factory, 2, &hr ); return; }

    /* AV1 site: slot 3 must be the no-argument ConnectFromTrackerSource, so it
     * may not write through the ULONG pointer / IID pointer pair that the old
     * GetIids slot wrote. */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, ULONG *, IID **))VT( rt, 3 ))( rt, &probe, &iids );
    ok( hr == S_OK, "tracker: ConnectFromTrackerSource hr=%08x\n", hr );
    ok( probe == 0x24, "tracker: slot 3 wrote the caller's ULONG* (probe=%u, expect 0x24 untouched)\n", probe );
    ok( iids == NULL, "tracker: slot 3 wrote the caller's IID** (iids=%p, expect untouched)\n", iids );

    /* AV2 site: slot 7 must be the no-argument AddRefFromTrackerSource, so it
     * may not write the out-param the old CreateWithCurrentView slot wrote. */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))VT( rt, 7 ))( rt, (void *)0x1, &sentinel );
    ok( hr == S_OK, "tracker: AddRefFromTrackerSource hr=%08x\n", hr );
    ok( sentinel == (void *)0x5b5b5b5b, "tracker: slot 7 wrote the caller's out param (got %p)\n", sentinel );

    /* the rest of the walk: all notifications are no-argument and S_OK */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *))VT( rt, 4 ))( rt );
    ok( hr == S_OK, "tracker: DisconnectFromTrackerSource hr=%08x\n", hr );
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *))VT( rt, 8 ))( rt );
    ok( hr == S_OK, "tracker: ReleaseFromTrackerSource hr=%08x\n", hr );
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *))VT( rt, 9 ))( rt );
    ok( hr == S_OK, "tracker: PegFromTrackerSource hr=%08x\n", hr );

    /* FindTrackerTargets: NULL is E_POINTER, a callback is accepted and no
     * tracker target is reported (the module owns no managed objects). */
    hr = CALL( rt, 5, NULL );
    ok( hr == E_POINTER, "tracker: FindTrackerTargets(NULL) hr=%08x\n", hr );
    cb = (void *)0x1234; /* never dereferenced: the object reports no targets */
    hr = CALL( rt, 5, cb );
    ok( hr == S_OK, "tracker: FindTrackerTargets(callback) hr=%08x\n", hr );

    /* GetReferenceTrackerManager: CoreRT calls through the result without
     * checking the HRESULT, so the object has to hand out a real
     * IReferenceTrackerManager.  Its slots are the GC cycle notifications. */
    hr = CALL( rt, 6, NULL );
    ok( hr == E_POINTER, "tracker: GetReferenceTrackerManager(NULL) hr=%08x\n", hr );
    mgr = NULL;
    hr = CALL( rt, 6, &mgr );
    ok( hr == S_OK && mgr, "tracker: GetReferenceTrackerManager hr=%08x mgr=%p\n", hr, mgr );
    if (SUCCEEDED( hr ) && mgr)
    {
        void *mgr2 = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *))VT( mgr, 3 ))( mgr );  /* ReferenceTrackingStarted */
        ok( hr == S_OK, "tracker: ReferenceTrackingStarted hr=%08x\n", hr );
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, int))VT( mgr, 4 ))( mgr, 0 );  /* FindTrackerTargetsCompleted */
        ok( hr == S_OK, "tracker: FindTrackerTargetsCompleted hr=%08x\n", hr );
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *))VT( mgr, 5 ))( mgr );  /* ReferenceTrackingCompleted */
        ok( hr == S_OK, "tracker: ReferenceTrackingCompleted hr=%08x\n", hr );
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void *))VT( mgr, 6 ))( mgr, NULL );  /* SetReferenceTrackerHost */
        ok( hr == E_POINTER, "tracker: SetReferenceTrackerHost(NULL) hr=%08x\n", hr );
        /* a real host: the manager keeps a reference to it */
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void *))VT( mgr, 6 ))( mgr, factory );
        ok( hr == S_OK, "tracker: SetReferenceTrackerHost(factory) hr=%08x\n", hr );
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT( mgr, 0 ))( mgr, &IID_IReferenceTrackerManager_loc, &mgr2 );
        ok( hr == S_OK && mgr2 == mgr, "tracker: manager QI identity hr=%08x mgr2=%p\n", hr, mgr2 );
        if (mgr2) CALL( mgr2, 2, &hr );
        CALL( mgr, 2, &hr );
    }

    /* COM identity: the tracker interface is the object, so it QIs back to
     * itself stably and reaches the object's IInspectable (RoGetActivationFactory
     * callers and the CLR tracker both rely on that). */
    rt2 = NULL;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT( rt, 0 ))( rt, &IID_IReferenceTracker_loc, &rt2 );
    ok( hr == S_OK && rt2 == rt, "tracker: QI back hr=%08x rt2=%p rt=%p\n", hr, rt2, rt );
    if (rt2) CALL( rt2, 2, &hr );
    inspectable = NULL;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT( rt, 0 ))( rt, &IID_IInspectable_loc, &inspectable );
    ok( hr == S_OK && inspectable, "tracker: QI(IInspectable) from tracker hr=%08x obj=%p\n", hr, inspectable );
    if (inspectable)
    {
        hr = CALL( inspectable, 4, &name );  /* GetRuntimeClassName */
        ok( hr == S_OK, "tracker: GetRuntimeClassName hr=%08x\n", hr );
        raw = WindowsGetStringRawBuffer( name, NULL );
        ok( raw && !lstrcmpW( raw, L"Windows.ApplicationModel.Resources.Core.ResourceContext" ),
            "tracker: class name %s\n", wine_dbgstr_w( raw ) );
        WindowsDeleteString( name );
        CALL( inspectable, 2, &hr );
    }

    /* IInspectable::GetIids must fill a real count and a CoTaskMemFree-able
     * array carrying the class's interface set (the old slot reported an
     * empty set, which desyncs the caller's interface map). */
    hr = CALL( factory, 3, &count, &iids );
    ok( hr == S_OK && count > 1 && iids, "tracker: GetIids hr=%08x count=%u iids=%p\n", hr, count, iids );
    arr = iids;
    for (i = 0; arr && i < count; i++)
    {
        if (!memcmp( &arr[i], &IID_IReferenceTracker_loc, sizeof(IID) )) found_tracker++;
        if (!memcmp( &arr[i], &IID_IResourceContextStatics2_loc, sizeof(IID) )) found_statics2++;
        if (!memcmp( &arr[i], &IID_IResourceContext_loc, sizeof(IID) )) found_context++;
    }
    ok( found_tracker == 1, "tracker: GetIids array carries IReferenceTracker (n=%u)\n", found_tracker );
    ok( found_statics2 == 1, "tracker: GetIids array carries IResourceContextStatics2 (n=%u)\n", found_statics2 );
    ok( found_context == 1, "tracker: GetIids array carries IResourceContext (n=%u)\n", found_context );
    CoTaskMemFree( iids );
    hr = CALL( factory, 3, NULL, NULL );
    ok( hr == E_POINTER, "tracker: GetIids(NULL,NULL) hr=%08x\n", hr );

    /* per-activation identity: activating twice hands out distinct objects,
     * each of which carries its own tracker interface */
    hr = CALL( factory, 6, &ctx1 );  /* IActivationFactory::ActivateInstance */
    ok( hr == S_OK && ctx1, "tracker: ActivateInstance#1 hr=%08x\n", hr );
    hr = CALL( factory, 6, &ctx2 );
    ok( hr == S_OK && ctx2, "tracker: ActivateInstance#2 hr=%08x\n", hr );
    if (ctx1 && ctx2)
        ok( ctx1 != ctx2, "tracker: per-activation distinct objects (ctx1=%p ctx2=%p)\n", ctx1, ctx2 );
    if (ctx1)
    {
        rt2 = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT( ctx1, 0 ))( ctx1, &IID_IReferenceTracker_loc, &rt2 );
        ok( hr == S_OK && rt2 && rt2 != rt, "tracker: instance carries its own tracker hr=%08x obj=%p\n", hr, rt2 );
        if (rt2)
        {
            hr = ((HRESULT (STDMETHODCALLTYPE *)(void *))VT( rt2, 3 ))( rt2 );
            ok( hr == S_OK, "tracker: instance ConnectFromTrackerSource hr=%08x\n", hr );
            CALL( rt2, 2, &hr );
        }
        CALL( ctx1, 2, &hr );
    }
    if (ctx2) CALL( ctx2, 2, &hr );

    /* the factory singleton is undamaged by the whole walk */
    {
        void *statics = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT( factory, 0 ))( factory, &IID_IResourceContextStatics2_loc, &statics );
        if (SUCCEEDED( hr ) && statics)
        {
            hr = CALL( statics, 6, &ctx1 );  /* GetForCurrentView */
            ok( SUCCEEDED( hr ) && ctx1, "tracker: singleton intact after walk hr=%08x\n", hr );
            if (ctx1) CALL( ctx1, 2, &hr );
            CALL( statics, 2, &hr );
        }
    }
    CALL( rt, 2, &hr );
    CALL( factory, 2, &hr );
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

/* IInspectable::GetRuntimeClassName of the parameterized collection objects
 * must answer the .NET-spelling runtime class name: the full 'Windows.…'
 * prefix, the backtick-N arity and the '<…>' argument list, spelling the
 * HSTRING element as 'System.String' (the 'Windows.Foundation.String' spelling
 * belongs to the parameterized-IID signature grammar only).
 *
 * .NET Native's loose-binding RCW path parses this string
 * (IInspectable::GetRuntimeClassName -> Internal.NativeFormat.NativeParser) to
 * pick the marshaling of the parameterized interface's parameters.  Measured on
 * the Calculator run: with a name carrying no arity and no arguments the
 * shared-reference-types adapter stays in charge, so
 * McgMarshal.ObjectToComInterface marshals the string key as an IInspectable
 * CCW instead of an HSTRING; the callee reads a null raw buffer, answers
 * ELEMENT_NOT_FOUND, the app retries five times and RaiseFailFastException
 * (c0000602) kills the process.  The length must also be the text itself: a
 * literal count larger than the literal copies the tail of the following
 * literal (measured: "Foundation.Collections.IMapView\0Win" for count 35).
 */
static void check_class_name( void *obj, const WCHAR *expected )
{
    HSTRING name = NULL;
    HRESULT hr = CALL( obj, 4, &name );  /* IInspectable::GetRuntimeClassName */
    const WCHAR *raw;
    UINT32 len = 0;

    ok( hr == S_OK && name, "GetRuntimeClassName hr=%08x name=%p\n", hr, name );
    if (FAILED( hr ) || !name) return;

    raw = WindowsGetStringRawBuffer( name, &len );
    ok( !lstrcmpW( raw, expected ), "class name %s, want %s\n", wine_dbgstr_w( raw ), wine_dbgstr_w( expected ) );
    ok( len == (UINT32)lstrlenW( expected ), "class name length %u, want %u\n", len, (UINT32)lstrlenW( expected ) );
    WindowsDeleteString( name );
}

static void test_parameterized_class_names(void)
{
    static const WCHAR name_mapview_ss[] = L"Windows.Foundation.Collections.IMapView`2<System.String,System.String>";
    static const WCHAR name_mapview_sm[] = L"Windows.Foundation.Collections.IMapView`2<System.String,Windows.ApplicationModel.Resources.Core.ResourceMap>";
    /* The Languages GetView face names itself with the SharedLibrary Mcg
     * name-pool key (short type-display spelling) that resolves, via
     * StringMap::FindString -> McgClassData, to the __Impl whose frozen
     * m_cachedInterfaces triple carries IIterable<IKeyValuePair<string,string>>
     * (interface RID 0x1806800000, cast #14).  The fully-qualified
     * `Windows.Foundation.Collections.IVectorView`1<System.String>` spelling
     * misses that exact key and falls back to a target-less closest class. */
    static const WCHAR name_vector_s[]   = L"IIterable`1<IKeyValuePair`2<String,String>>";
    static const WCHAR name_vector_observable_s[] = L"Windows.Foundation.Collections.IObservableVector`1<System.String>";
    /* The str_vector view impl (svv_QueryInterface vt_vv) is shared by the
     * Languages GetView and the Qualifiers faces; both name themselves with the
     * same SharedLibrary Mcg name-pool key so cast #14 resolves the triple. */
    static const WCHAR name_vector_rq[]  = L"IIterable`1<IKeyValuePair`2<String,String>>";
    static const WCHAR name_kvp_ss[]     = L"Windows.Foundation.Collections.IKeyValuePair`2<System.String,System.String>";
    static const WCHAR name_kvp_sm[]     = L"Windows.Foundation.Collections.IKeyValuePair`2<System.String,Windows.ApplicationModel.Resources.Core.ResourceMap>";
    static const WCHAR name_iter_s[]     = L"Windows.Foundation.Collections.IIterator`1<System.String>";
    static const WCHAR name_iter_kvp_ss[] = L"Windows.Foundation.Collections.IIterator`1<Windows.Foundation.Collections.IKeyValuePair`2<System.String,System.String>>";
    static const WCHAR name_iter_kvp_sm[] = L"Windows.Foundation.Collections.IIterator`1<Windows.Foundation.Collections.IKeyValuePair`2<System.String,Windows.ApplicationModel.Resources.Core.ResourceMap>>";
    void *factory = NULL, *ctx = NULL, *map = NULL, *vec = NULL, *it = NULL, *pair = NULL;
    void *rmfactory = NULL, *rm = NULL, *rm2 = NULL, *rmap = NULL, *it2 = NULL, *pair2 = NULL;
    void *cand = NULL, *cand2 = NULL, *quals = NULL;
    HSTRING_HEADER khdr;
    HSTRING hkey = NULL;
    INT32 has = 0;
    HRESULT hr;

    set_pri_env();

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "class names: ResourceContext factory hr=%08x\n", hr );
    if (hr != S_OK) return;
    hr = CALL( factory, 6, &ctx );  /* IActivationFactory::ActivateInstance */
    ok( hr == S_OK && ctx, "class names: ActivateInstance hr=%08x\n", hr );
    if (!ctx) goto done_factory;

    /* QualifierValues: IMapView<HSTRING,HSTRING> + its pair iterator chain */
    hr = CALL( ctx, 6, &map );  /* IResourceContext::get_QualifierValues */
    ok( hr == S_OK && map, "class names: get_QualifierValues hr=%08x\n", hr );
    if (map)
    {
        check_class_name( map, name_mapview_ss );
        hr = CALL( map, 10, &it );  /* IIterable::First */
        ok( hr == S_OK && it, "class names: QualifierValues First hr=%08x\n", hr );
        if (it)
        {
            check_class_name( it, name_iter_kvp_ss );
            hr = CALL( it, 6, &has );  /* IIterator::MoveNext */
            ok( hr == S_OK && has, "class names: QualifierValues MoveNext hr=%08x has=%d\n", hr, has );
            hr = CALL( it, 7, &pair );  /* IIterator::get_Current -> IKeyValuePair */
            ok( hr == S_OK && pair, "class names: QualifierValues Current hr=%08x\n", hr );
            if (pair)
            {
                check_class_name( pair, name_kvp_ss );
                CALL( pair, 2, &hr );
            }
            CALL( it, 2, &hr );
        }
        CALL( map, 2, &hr );
    }

    /* Languages: IVectorView<HSTRING> + its string iterator */
    hr = CALL( ctx, 11, &vec );  /* IResourceContext::get_Languages */
    ok( hr == S_OK && vec, "class names: get_Languages hr=%08x\n", hr );
    if (vec)
    {
        /* the property is declared IObservableVector<string> and the face it
         * hands out names itself as such (the loose-binding RCW parses the name
         * to learn what it holds); the IVectorView name belongs to the read-only
         * face GetView below returns */
        check_class_name( vec, name_vector_observable_s );
        /* Languages hands out the IObservableVector face: own members add 6 /
         * remove 7, then the delegated IVector block (idl:265-269), so GetView
         * -- an IVector member -- sits at 10, and the face it returns is the
         * IVectorView, laid out [GetAt 6][Size 7][IndexOf 8][GetMany 9]
         * [First 10] (idl:249-256). */
        hr = CALL( vec, 10, &it );  /* IVector::GetView -> the IVectorView face */
        ok( hr == S_OK && it, "class names: Languages GetView@10 hr=%08x\n", hr );
        if (SUCCEEDED( hr ) && it)
        {
            check_class_name( it, name_vector_s );
            hr = CALL( it, 10, &pair );  /* IVectorView::First, slot 10 */
            ok( hr == S_OK && pair, "class names: Languages First@10 hr=%08x\n", hr );
            if (pair)
            {
                check_class_name( pair, name_iter_s );
                CALL( pair, 2, &hr );
                pair = NULL;
            }
            CALL( it, 2, &hr );
            it = NULL;
        }
        CALL( vec, 2, &hr );
    }
    CALL( ctx, 2, &hr );
    CALL( factory, 2, &hr );
    factory = NULL;

    /* AllResourceMaps: IMapView<HSTRING,IResourceMap*> + its pair iterator chain */
    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceManager", &rmfactory );
    ok( hr == S_OK, "class names: ResourceManager factory hr=%08x\n", hr );
    if (hr != S_OK) return;
    hr = CALL( rmfactory, 0, &IID_IResourceManagerStatics_loc, &rm );
    ok( hr == S_OK && rm, "class names: QI IResourceManagerStatics hr=%08x\n", hr );
    if (hr != S_OK) goto done_rmfactory;
    hr = CALL( rm, 6, &rm );  /* IResourceManagerStatics::get_Current */
    ok( SUCCEEDED( hr ) && rm, "class names: ResourceManager Current hr=%08x\n", hr );
    if (FAILED( hr ) || !rm) goto done_rm;
    hr = CALL( rm, 0, &IID_IResourceManager_loc, &rm2 );
    ok( hr == S_OK && rm2, "class names: QI IResourceManager hr=%08x\n", hr );
    if (hr == S_OK)
    {
        hr = CALL( rm2, 7, &rmap );  /* IResourceManager::get_AllResourceMaps */
        ok( hr == S_OK && rmap, "class names: get_AllResourceMaps hr=%08x\n", hr );
        if (rmap)
        {
            check_class_name( rmap, name_mapview_sm );
            hr = CALL( rmap, 10, &it2 );  /* IIterable::First */
            ok( hr == S_OK && it2, "class names: AllResourceMaps First hr=%08x\n", hr );
            if (it2)
            {
                check_class_name( it2, name_iter_kvp_sm );
                has = 0;
                hr = CALL( it2, 6, &has );  /* MoveNext */
                ok( hr == S_OK && has, "class names: AllResourceMaps MoveNext hr=%08x has=%d\n", hr, has );
                hr = CALL( it2, 7, &pair2 );  /* get_Current */
                ok( hr == S_OK && pair2, "class names: AllResourceMaps Current hr=%08x\n", hr );
                if (pair2)
                {
                    check_class_name( pair2, name_kvp_sm );
                    CALL( pair2, 2, &hr );
                }
                CALL( it2, 2, &hr );
            }
            CALL( rmap, 2, &hr );
        }
        /* MainResourceMap -> a candidate -> its Qualifiers vector */
        hr = CALL( rm2, 6, &map );
        ok( hr == S_OK && map, "class names: get_MainResourceMap hr=%08x\n", hr );
        if (SUCCEEDED( hr ) && map)
        {
            WindowsCreateStringReference( L"Calculator", 10, &khdr, &hkey );
            hr = CALL( map, 7, hkey, &cand );  /* IResourceMap::GetValue */
            ok( SUCCEEDED( hr ) && cand, "class names: GetValue hr=%08x\n", hr );
            if (SUCCEEDED( hr ) && cand)
            {
                hr = CALL( cand, 0, &IID_IResourceCandidate_loc, &cand2 );
                ok( hr == S_OK && cand2, "class names: QI IResourceCandidate hr=%08x\n", hr );
                if (hr == S_OK)
                {
                    hr = CALL( cand2, 6, &quals );  /* IResourceCandidate::get_Qualifiers */
                    ok( hr == S_OK && quals, "class names: get_Qualifiers hr=%08x\n", hr );
                    if (quals)
                    {
                        check_class_name( quals, name_vector_rq );
                        CALL( quals, 2, &hr );
                    }
                    CALL( cand2, 2, &hr );
                }
                CALL( cand, 2, &hr );
            }
            if (hkey) WindowsDeleteString( hkey );
            CALL( map, 2, &hr );
        }
        CALL( rm2, 2, &hr );
    }
done_rm:
    CALL( rm, 2, &hr );
done_rmfactory:
    CALL( rmfactory, 2, &hr );
    return;

done_factory:
    CALL( factory, 2, &hr );
}

/*
 * Virtualized HSTRINGs (.NET Native / CoreRT): the AOT'd app (CalculatorApp.dll)
 * passes managed GC strings directly as HSTRING arguments -- the low 3 bits of
 * the handle carry a type tag, the payload is 8-byte aligned (tags 1/2/7 need
 * 3 free low bits):
 *   tag 0 = plain WindowsCreateString handle (low bits 0)
 *   tag 1 = atom-table string (no local table: not resolvable here, not tested)
 *   tag 2 = inline buffer {UINT32 cch; WCHAR chars[cch+1]} at handle & ~7
 *   tag 7 = managed string object at handle & ~7; x64 GC String ABI:
 *           MethodTable*@+0x00, int32 cch@+0x08, chars inline right after the
 *           length (coreclr: +0x0C; the RTString layout note says +0x10).
 *           Both candidate char offsets are exercised; the module must decode
 *           the NUL-terminated one (offsets proven at runtime, not assumed).
 * The p49 Calculator trace showed mv_Get Lookup (null) x5 from exactly these
 * keys (QualifierValues Get("Language")), i.e. the module read the tagged
 * handle as a plain hstring_private and got a NULL/garbage raw buffer.
 */
typedef struct { void *mt; UINT32 cch; WCHAR chars[16]; } virtstr_0c;  /* chars @ +0x0C */
typedef struct { void *mt; UINT32 cch; UINT32 pad; WCHAR chars[16]; } virtstr_10;  /* chars @ +0x10 */
typedef struct { UINT32 cch; WCHAR chars[16]; } virtbuf_2;             /* chars @ +0x04 */

C_ASSERT(offsetof(virtstr_0c, cch) == 0x08);
C_ASSERT(offsetof(virtstr_0c, chars) == 0x0C);
C_ASSERT(offsetof(virtstr_10, cch) == 0x08);
C_ASSERT(offsetof(virtstr_10, chars) == 0x10);
C_ASSERT(offsetof(virtbuf_2, chars) == 0x04);

static void test_virtualized_hstring(void)
{
    void *factory = NULL, *ctx = NULL, *ctx2 = NULL, *mv = NULL;
    void *rmfactory = NULL, *rms = NULL, *rm2 = NULL, *map = NULL, *cand = NULL;
    HSTRING hval = NULL;
    HRESULT hr;
    DECLSPEC_ALIGN(8) virtstr_0c s0c, s0c_miss;
    DECLSPEC_ALIGN(8) virtstr_10 s10, s10calc;
    DECLSPEC_ALIGN(8) virtbuf_2 b2;
    HSTRING h7c, h7t, h2, hmiss, hcalc;

    C_ASSERT(sizeof(virtstr_0c) % 8 == 0);
    C_ASSERT(sizeof(virtstr_10) % 8 == 0);
    /* virtualized tags need 8-byte-aligned payloads (low 3 bits free) */
    ok(((UINT_PTR)&s0c % 8) == 0, "virt-hstring: s0c alignment\n");
    ok(((UINT_PTR)&s10 % 8) == 0, "virt-hstring: s10 alignment\n");
    ok(((UINT_PTR)&b2 % 8) == 0, "virt-hstring: b2 alignment\n");

    hr = get_factory(L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory);
    ok(hr == S_OK, "virt-hstring: RoGetActivationFactory(ResourceContext) hr=%08x\n", hr);
    if (hr != S_OK) return;
    hr = CALL(factory, 6, &ctx2);  /* ActivateInstance */
    ok(SUCCEEDED(hr) && ctx2, "virt-hstring: ActivateInstance hr=%08x\n", hr);
    if (FAILED(hr) || !ctx2) goto done_factory;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(ctx2, 0))(ctx2, &IID_IResourceContext_loc, &ctx);
    ok(hr == S_OK && ctx, "virt-hstring: QI IResourceContext hr=%08x\n", hr);
    if (FAILED(hr) || !ctx) goto done_factory;
    hr = CALL(ctx, 6, &mv);  /* get_QualifierValues: IMapView<HSTRING,HSTRING> */
    ok(hr == S_OK && mv, "virt-hstring: get_QualifierValues hr=%08x\n", hr);
    if (FAILED(hr) || !mv) goto done_factory;

    /* tag 7 over a coreclr-layout GC string: cch@+0x08, chars@+0x0C ("Language") */
    s0c.mt = NULL; s0c.cch = 8; lstrcpyW(s0c.chars, L"Language");
    h7c = (HSTRING)((char *)&s0c + 7);
    hr = CALL(mv, 6, h7c, &hval);  /* IMapView::Get */
    ok(SUCCEEDED(hr) && hval, "virt-hstring tag7(+0x0c): QualifierValues Get(Language) hr=%08x\n", hr);
    if (SUCCEEDED(hr) && hval)
    {
        const WCHAR *raw = WindowsGetStringRawBuffer(hval, NULL);
        ok(raw && *raw, "virt-hstring tag7(+0x0c): value is empty\n");
        WindowsDeleteString(hval);
        hval = NULL;
    }

    /* tag 7 over the +0x10 char offset (RTString layout note) */
    s10.mt = NULL; s10.cch = 8; s10.pad = 0; lstrcpyW(s10.chars, L"Language");
    h7t = (HSTRING)((char *)&s10 + 7);
    hr = CALL(mv, 6, h7t, &hval);
    ok(SUCCEEDED(hr) && hval, "virt-hstring tag7(+0x10): QualifierValues Get(Language) hr=%08x\n", hr);
    if (SUCCEEDED(hr) && hval)
    {
        const WCHAR *raw = WindowsGetStringRawBuffer(hval, NULL);
        ok(raw && *raw, "virt-hstring tag7(+0x10): value is empty\n");
        WindowsDeleteString(hval);
        hval = NULL;
    }

    /* tag 2 inline length-prefixed buffer */
    b2.cch = 8; lstrcpyW(b2.chars, L"Language");
    h2 = (HSTRING)((char *)&b2 + 2);
    hr = CALL(mv, 6, h2, &hval);
    ok(SUCCEEDED(hr) && hval, "virt-hstring tag2: QualifierValues Get(Language) hr=%08x\n", hr);
    if (SUCCEEDED(hr) && hval)
    {
        const WCHAR *raw = WindowsGetStringRawBuffer(hval, NULL);
        ok(raw && *raw, "virt-hstring tag2: value is empty\n");
        WindowsDeleteString(hval);
        hval = NULL;
    }

    /* HasKey must see the decoded key too */
    {
        INT32 has = -1;
        hr = CALL(mv, 8, h7c, &has);  /* IMapView::HasKey, slot 8 (idl: Get 6, Size 7, HasKey 8) */
        ok(hr == S_OK && has, "virt-hstring tag7: HasKey(Language) hr=%08x has=%d\n", hr, has);
    }

    /* negative: real virtualized key that is NOT in the map -> ELEMENT_NOT_FOUND,
     * not (null)-lookup, not a crash */
    s0c_miss.mt = NULL; s0c_miss.cch = 7; lstrcpyW(s0c_miss.chars, L"NoSuchQ");
    hmiss = (HSTRING)((char *)&s0c_miss + 7);
    /* the miss writes the retval (a null HSTRING *is* the empty string in the
     * Windows.String projection, combase/string.c:221: WindowsCreateString of a
     * zero-length source yields the null handle), so the sentinel shows whether
     * the callee touched the [out] parameter at all */
    hval = (HSTRING)0x1;
    hr = CALL(mv, 6, hmiss, &hval);
    ok(hr == ELEMENT_NOT_FOUND_HR, "virt-hstring tag7: Get(NoSuchQual) hr=%08x (expect ELEMENT_NOT_FOUND)\n", hr);
    ok(hval != (HSTRING)0x1, "virt-hstring tag7: Get(NoSuchQual) left the retval unwritten (%p)\n", (void *)hval);
    ok(WindowsGetStringLen(hval) == 0, "virt-hstring tag7: Get(NoSuchQual) retval length %u, want the empty string\n",
       WindowsGetStringLen(hval));
    if (hval && hval != (HSTRING)0x1) WindowsDeleteString(hval);
    hval = NULL;

    /* the same blind spot on IResourceMap::GetValue (map_lookup path):
     * ResourceManager.Current -> MainResourceMap -> GetValue(Calculator|tag7) */
    hr = get_factory(L"Windows.ApplicationModel.Resources.Core.ResourceManager", &rmfactory);
    ok(hr == S_OK, "virt-hstring: RoGetActivationFactory(ResourceManager) hr=%08x\n", hr);
    if (hr == S_OK)
    {
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(rmfactory, 0))(rmfactory, &IID_IResourceManagerStatics_loc, &rms);
        if (SUCCEEDED(hr))
        {
            void *rm = NULL;
            hr = CALL(rms, 6, &rm);  /* IResourceManagerStatics::Current */
            if (SUCCEEDED(hr) && rm)
            {
                hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(rm, 0))(rm, &IID_IResourceManager_loc, &rm2);
                if (SUCCEEDED(hr) && rm2)
                {
                    hr = CALL(rm2, 6, &map);  /* get_MainResourceMap */
                    if (SUCCEEDED(hr) && map)
                    {
                        s10calc.mt = NULL; s10calc.cch = 10; s10calc.pad = 0; lstrcpyW(s10calc.chars, L"Calculator");
                        hcalc = (HSTRING)((char *)&s10calc + 7);
                        hr = CALL(map, 7, hcalc, &cand);  /* IResourceMap::GetValue */
                        ok(SUCCEEDED(hr) && cand, "virt-hstring tag7: MainResourceMap::GetValue(Calculator) hr=%08x\n", hr);
                        if (cand) CALL(cand, 2, (ULONG *)&hr);
                    }
                    CALL(rm2, 2, (ULONG *)&hr);
                }
                CALL(rm, 2, (ULONG *)&hr);
            }
            CALL(rms, 2, (ULONG *)&hr);
        }
        CALL(rmfactory, 2, (ULONG *)&hr);
    }

    CALL(mv, 2, (ULONG *)&hr);
done_factory:
    if (ctx) CALL(ctx, 2, (ULONG *)&hr);
    if (ctx2) CALL(ctx2, 2, (ULONG *)&hr);
    CALL(factory, 2, (ULONG *)&hr);
}

/*
 * CCW (COM Callable Wrapper) string keys.
 *
 * IMapView<HSTRING,HSTRING>::Get/::HasKey, IResourceMap::GetValue/
 * GetValueForContext and IResourceContext2::SetQualifierValue all take an
 * HSTRING key.  .NET Native's loose-binding RCW (mrt100_app/SharedLibrary,
 * Internal.NativeFormat + McgMarshal.ObjectToComInterface) hands over a CCW of
 * a managed System.String instead: an 8-byte-aligned IUnknown pointer whose
 * low 3 tag bits are 0, i.e. tag 0, the plain-handle tag.  The pointer was
 * never produced by any combase string API, so combase reads it as an
 * hstring_private (dlls/combase/string.c hstring_get_data returns
 * priv->header.str, the qword at +0x20) and hands back that wild buffer; the
 * module compares garbage, answers ELEMENT_NOT_FOUND (0x490) and the app
 * failfasts c0000602 at SharedLibrary 0x6ffffff9f262 (gate sr12: c0000602 x4,
 * AV 0).
 *
 * The documented CoreCLR String CCW surface is IStringable, declared in this
 * tree:
 *   include/windows.foundation.idl:270  uuid(96369f54-8eb6-48f0-abce-c1b211e627c3)
 *   include/windows.foundation.idl:272  interface IStringable : IInspectable
 *                                       { HRESULT ToString([out,retval] HSTRING *value) }
 * widl emits the identity as IID___x_ABI_CWindows_CFoundation_CIStringable
 * (generated include/windows.foundation.h:2283, alias IID_IStringable at :2384)
 * and the vtable (generated :2304ff) as
 *   [0 QI][1 AddRef][2 Release][3 GetIids][4 GetRuntimeClassName]
 *   [5 GetTrustLevel][6 ToString]
 *
 * IReference`1<Windows.Foundation.String> is a parameterized interface: this
 * tree carries no static IID for it (no DEFINE_GUID for FIReference_1_HSTRING
 * in the generated headers), so the test computes it with the tree's own
 * RoGetParameterizedTypeInstanceIID (dlls/combase/roparameterizediid.c,
 * combase.spec:301) and pins the value that algorithm produces; get_Value
 * shares IStringable::ToString's slot (6) and HSTRING* signature because both
 * interfaces declare exactly one method after the IInspectable quintet.
 */
static const IID IID_IStringable_loc = {0x96369f54, 0x8eb6, 0x48f0, {0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3}};

/* implemented in dlls/combase/roparameterizediid.c, exported by combase; not
 * declared by the generated rparameterizediid.h (module-side prototype in
 * dlls/windows.applicationmodel.resources.core/main.c) */
#include "roparameterizediid.h"
HRESULT WINAPI RoGetParameterizedTypeInstanceIID( UINT32 name_element_count, const WCHAR **name_elements,
                                                  const IRoMetaDataLocator *locator, GUID *iid,
                                                  ROPARAMIIDHANDLE *handle );

enum ccw_flavor { CCW_ISTRINGABLE, CCW_IREFERENCE, CCW_NONE };

/* A mini CCW: plain vtable object (no combase, no aggregation) standing in for
 * the managed String wrapper the .NET Native RCW hands over.
 *
 * The layout is chosen so that reading the object as a combase winrtstring is
 * *provably* not a string.  combase's payload header is
 *   dlls/combase/string.c:32 struct hstring_header { UINT32 flags, length,
 *   padding1, padding2; const WCHAR *str; }   ->  length at +0x04, str at +0x10
 * and hstring_get_data() returns priv->header.str for any handle whose low 3
 * bits are 0.  So:
 *   +0x04 = the high half of the vtable pointer (an arbitrary "length")
 *   +0x10 = raw_slot, a CoreCLR-CCW-shaped slot: NOT a WCHAR*, pointing at a
 *           64-unit buffer of non-NUL code units (no qualifier key can match
 *           its first character, and no index 1..63 is NUL, so the
 *           "is this a winrtstring payload" probe cannot accept it)
 * The string's real value lives at +0x18 and is reachable only through the CCW
 * surface (IStringable::ToString / IReference<String>::get_Value, slot 6). */
struct mini_ccw
{
    const void *vtbl;        /* +0x00 */
    LONG ref;                /* +0x08 */
    const WCHAR *raw_slot;   /* +0x10 = what combase reads as header.str */
    const WCHAR *text;       /* +0x18 the managed string's value */
    enum ccw_flavor flavor;  /* which key surface this CCW answers */
    GUID iref_iid;           /* +0x28 IReference`1<String> pinterface to answer */
};
C_ASSERT( offsetof( struct mini_ccw, ref ) == 0x08 );
C_ASSERT( offsetof( struct mini_ccw, raw_slot ) == 0x10 );
C_ASSERT( offsetof( struct mini_ccw, text ) == 0x18 );

/* 64 non-NUL code units: a wild 'str' slot stays readable (the RED-phase
 * module compares it with lstrcmpiW and must not fault) while never being a
 * plausibly NUL-terminated winrtstring payload */
static const WCHAR ccw_raw_slot[64] =
{
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};
static LONG ccw_qi_stringable, ccw_qi_ireference, ccw_tostring_calls;

static HRESULT WINAPI ccw_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct mini_ccw *impl = (struct mini_ccw *)iface;

    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable_loc ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (impl->flavor == CCW_ISTRINGABLE && IsEqualGUID( iid, &IID_IStringable_loc ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        InterlockedIncrement( &ccw_qi_stringable );
        return S_OK;
    }
    if (impl->flavor == CCW_IREFERENCE && IsEqualGUID( iid, &impl->iref_iid ))
    {
        *out = iface;
        InterlockedIncrement( &impl->ref );
        InterlockedIncrement( &ccw_qi_ireference );
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG WINAPI ccw_AddRef( void *iface ) { return InterlockedIncrement( &((struct mini_ccw *)iface)->ref ); }
static ULONG WINAPI ccw_Release( void *iface ) { return InterlockedDecrement( &((struct mini_ccw *)iface)->ref ); }
static HRESULT WINAPI ccw_GetIids( void *iface, ULONG *c, IID **i ) { if (c) *c = 0; if (i) *i = NULL; return S_OK; }
static HRESULT WINAPI ccw_GetRuntimeClassName( void *iface, HSTRING *n ) { return WindowsCreateString( L"System.String", 12, n ); }
static HRESULT WINAPI ccw_GetTrustLevel( void *iface, INT32 *t ) { if (t) *t = 2; return S_OK; }
/* IStringable::ToString (slot 6) == IReference<String>::get_Value (slot 6) */
static HRESULT WINAPI ccw_ToString( void *iface, HSTRING *v )
{
    struct mini_ccw *impl = (struct mini_ccw *)iface;

    InterlockedIncrement( &ccw_tostring_calls );
    if (!v) return E_POINTER;
    return WindowsCreateString( impl->text, (UINT32)lstrlenW( impl->text ), v );
}
static const void *ccw_vtbl[] =
{
    ccw_QueryInterface, ccw_AddRef, ccw_Release, ccw_GetIids, ccw_GetRuntimeClassName,
    ccw_GetTrustLevel, ccw_ToString,
};

/* a pointer that is not a COM object at all: its "vtable" jumps to address 1 */
static const void *junk_vtbl[4] = { (const void *)0x1, (const void *)0x1, (const void *)0x1, (const void *)0x1 };

static void mini_ccw_init( struct mini_ccw *o, const WCHAR *text, enum ccw_flavor flavor )
{
    o->vtbl = ccw_vtbl;
    o->ref = 1;
    o->raw_slot = ccw_raw_slot;
    o->text = text;
    o->flavor = flavor;
    memset( &o->iref_iid, 0, sizeof(o->iref_iid) );
}

static BOOL hstring_eq( HSTRING a, const WCHAR *b )
{
    const WCHAR *raw = WindowsGetStringRawBuffer( a, NULL );
    return raw && !wcscmp( raw, b );
}

/* QualifierValues + MainResourceMap for the CCW drives: same object chain the
 * app walks (ResourceContext factory -> ActivateInstance -> IResourceContext ->
 * get_QualifierValues). */
static void test_ccw_string_key( void )
{
    void *factory = NULL, *ctx2 = NULL, *ctx = NULL, *mv = NULL;
    void *rmfactory = NULL, *rms = NULL, *rm2 = NULL, *map = NULL;
    void *own_ctx = NULL, *own_mv = NULL, *c2 = NULL;
    DECLSPEC_ALIGN(8) struct mini_ccw ccw_lang, ccw_absent, ccw_iref, ccw_plain, ccw_junk;
    DECLSPEC_ALIGN(8) struct mini_ccw ccw_qname, ccw_qvalue, ccw_reskey;
    HSTRING hexpect = NULL, hval = NULL, hkey = NULL, hback = NULL;
    GUID iref_iid;
    INT32 has = -1;
    HRESULT hr;

    /* the pinterface identity comes from the tree's own algorithm */
    {
        static const WCHAR *elems[] = { L"Windows.Foundation.IReference`1", L"Windows.Foundation.String" };
        ROPARAMIIDHANDLE handle = NULL;

        iref_iid = IID_IUnknown;
        hr = RoGetParameterizedTypeInstanceIID( 2, elems, NULL, &iref_iid, &handle );
        ok( SUCCEEDED( hr ), "ccw-key: RoGetParameterizedTypeInstanceIID(IReference`1<String>) hr=%08x\n", hr );
        trace( "ccw-key: IReference`1<String> pinterface = %s\n", wine_dbgstr_guid( &iref_iid ) );
    }

    /* the IStringable identity the module QIs for is the tree's own metadata
     * answer (dlls/combase/roapi.c iid_names[]) -- runtime evidence, and it
     * rules out the {f01beeae-1d32-47b8-82dd-700ea3508562} that a
     * windows-rs-shaped guess would suggest */
    {
        static const WCHAR *elem[] = { L"Windows.Foundation.IStringable" };
        ROPARAMIIDHANDLE handle = NULL;
        GUID g;

        g = IID_IUnknown;
        hr = RoGetParameterizedTypeInstanceIID( 1, elem, NULL, &g, &handle );
        ok( SUCCEEDED( hr ) && IsEqualGUID( &g, &IID_IStringable_loc ),
            "ccw-key: tree metadata IID of Windows.Foundation.IStringable is %s (expect %s) hr=%08x\n",
            wine_dbgstr_guid( &g ), wine_dbgstr_guid( &IID_IStringable_loc ), hr );
    }

    mini_ccw_init( &ccw_lang, L"Language", CCW_ISTRINGABLE );
    mini_ccw_init( &ccw_absent, L"NoSuchQualifier", CCW_ISTRINGABLE );
    mini_ccw_init( &ccw_plain, L"Language", CCW_NONE );
    mini_ccw_init( &ccw_iref, L"Language", CCW_IREFERENCE );
    ccw_iref.iref_iid = iref_iid;
    mini_ccw_init( &ccw_qname, L"TestCcwQualifier", CCW_ISTRINGABLE );
    mini_ccw_init( &ccw_qvalue, L"ccw-value", CCW_ISTRINGABLE );
    mini_ccw_init( &ccw_reskey, L"Resources\\CalculatorModeText", CCW_ISTRINGABLE );
    ccw_junk = ccw_lang;
    ccw_junk.vtbl = junk_vtbl;

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "ccw-key: RoGetActivationFactory(ResourceContext) hr=%08x\n", hr );
    if (hr != S_OK) return;
    hr = CALL( factory, 6, &ctx2 );  /* ActivateInstance */
    ok( SUCCEEDED( hr ) && ctx2, "ccw-key: ActivateInstance hr=%08x\n", hr );
    if (FAILED( hr ) || !ctx2) goto done_factory;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT( ctx2, 0 ))( ctx2, &IID_IResourceContext_loc, &ctx );
    ok( hr == S_OK && ctx, "ccw-key: QI IResourceContext hr=%08x\n", hr );
    if (FAILED( hr ) || !ctx) goto done_factory;
    hr = CALL( ctx, 6, &mv );  /* get_QualifierValues: IMapView<HSTRING,HSTRING> */
    ok( hr == S_OK && mv, "ccw-key: get_QualifierValues hr=%08x\n", hr );
    if (FAILED( hr ) || !mv) goto done_factory;

    /* baseline: a real HSTRING key resolves Language, and pins the value the
     * CCW key must resolve to (locale-independent assertion) */
    {
        HSTRING_HEADER hdr;
        WindowsCreateStringReference( L"Language", 8, &hdr, &hkey );
        hr = CALL( mv, 6, hkey, &hexpect );
        ok( SUCCEEDED( hr ) && hexpect, "ccw-key: baseline Get(Language) hr=%08x\n", hr );
    }

    /* the app's shape: IMapView::Get with a String CCW key */
    ccw_qi_stringable = ccw_tostring_calls = 0;
    hval = NULL;
    hr = CALL( mv, 6, (HSTRING)&ccw_lang, &hval );
    ok( SUCCEEDED( hr ) && hval, "ccw-key: QualifierValues Get(CCW \"Language\") hr=%08x\n", hr );
    if (SUCCEEDED( hr ) && hval)
    {
        ok( !hexpect || hstring_eq( hval, WindowsGetStringRawBuffer( hexpect, NULL ) ),
            "ccw-key: CCW Language value != baseline value\n" );
        WindowsDeleteString( hval );
    }
    ok( ccw_qi_stringable > 0, "ccw-key: CCW path did not QI IStringable (%ld)\n", ccw_qi_stringable );
    ok( ccw_tostring_calls > 0, "ccw-key: CCW path did not call ToString (%ld)\n", ccw_tostring_calls );

    /* HasKey sees the same key */
    has = -1;
    hr = CALL( mv, 8, (HSTRING)&ccw_lang, &has );  /* HasKey, slot 8 */
    ok( hr == S_OK && has, "ccw-key: QualifierValues HasKey(CCW \"Language\") hr=%08x has=%d\n", hr, has );

    /* a CCW naming an absent qualifier must not manufacture a hit; the sentinel
     * under the call shows whether the miss path writes the [out] retval at all
     * (a null HSTRING is the empty string, so it is a legal written value) */
    hval = (HSTRING)0x1;
    hr = CALL( mv, 6, (HSTRING)&ccw_absent, &hval );
    ok( hr == ELEMENT_NOT_FOUND_HR, "ccw-key: Get(CCW \"NoSuchQualifier\") hr=%08x (expect ELEMENT_NOT_FOUND)\n", hr );
    ok( hval != (HSTRING)0x1, "ccw-key: miss left the retval unwritten (sentinel %p)\n", (void *)hval );
    ok( WindowsGetStringLen( hval ) == 0, "ccw-key: miss retval length %u, want the empty string\n", WindowsGetStringLen( hval ) );
    if (hval && hval != (HSTRING)0x1) WindowsDeleteString( hval );
    hval = NULL;
    has = -1;
    hr = CALL( mv, 8, (HSTRING)&ccw_absent, &has );  /* HasKey, slot 8 */
    ok( hr == S_OK && !has, "ccw-key: HasKey(CCW \"NoSuchQualifier\") hr=%08x has=%d\n", hr, has );

    /* the IReference`1<String> pinterface surface (get_Value, slot 6) */
    if (!IsEqualGUID( &iref_iid, &IID_IUnknown ))
    {
        ccw_qi_ireference = 0;
        hval = NULL;
        hr = CALL( mv, 6, (HSTRING)&ccw_iref, &hval );
        ok( SUCCEEDED( hr ) && hval, "ccw-key: Get(IReference<String> CCW \"Language\") hr=%08x\n", hr );
        if (SUCCEEDED( hr ) && hval) WindowsDeleteString( hval );
        ok( ccw_qi_ireference > 0, "ccw-key: IReference path did not QI the pinterface (%ld)\n", ccw_qi_ireference );
    }

    /* a COM object that answers no key surface: no hit, no crash */
    hval = (HSTRING)0x1;
    hr = CALL( mv, 6, (HSTRING)&ccw_plain, &hval );
    ok( hr == ELEMENT_NOT_FOUND_HR, "ccw-key: Get(plain CCW) hr=%08x (expect ELEMENT_NOT_FOUND)\n", hr );
    ok( hval != (HSTRING)0x1, "ccw-key: plain-CCW miss left the retval unwritten (sentinel %p)\n", (void *)hval );
    ok( WindowsGetStringLen( hval ) == 0, "ccw-key: plain-CCW miss retval length %u, want the empty string\n",
        WindowsGetStringLen( hval ) );
    if (hval && hval != (HSTRING)0x1) WindowsDeleteString( hval );
    hval = NULL;
    has = -1;
    hr = CALL( mv, 8, (HSTRING)&ccw_plain, &has );  /* HasKey, slot 8 */
    ok( hr == S_OK && !has, "ccw-key: HasKey(plain CCW) hr=%08x has=%d\n", hr, has );

    /* not a COM pointer at all: the QI must be SEH-guarded, the process must
     * survive and the answer stay ELEMENT_NOT_FOUND */
    hval = (HSTRING)0x1;
    hr = CALL( mv, 6, (HSTRING)&ccw_junk, &hval );
    ok( hr == ELEMENT_NOT_FOUND_HR, "ccw-key: Get(non-Ccw pointer) hr=%08x (expect ELEMENT_NOT_FOUND)\n", hr );
    ok( hval != (HSTRING)0x1, "ccw-key: non-Ccw miss left the retval unwritten (sentinel %p)\n", (void *)hval );
    ok( WindowsGetStringLen( hval ) == 0, "ccw-key: non-Ccw miss retval length %u, want the empty string\n",
        WindowsGetStringLen( hval ) );
    if (hval && hval != (HSTRING)0x1) WindowsDeleteString( hval );
    hval = NULL;
    has = -1;
    hr = CALL( mv, 8, (HSTRING)&ccw_junk, &has );  /* HasKey, slot 8 */
    ok( hr == S_OK && !has, "ccw-key: HasKey(non-Ccw pointer) hr=%08x has=%d\n", hr, has );
    hr = CALL( mv, 7, &has );  /* Size, slot 7: the map is still intact after the AV probes */
    ok( SUCCEEDED( hr ) && has > 0, "ccw-key: QualifierValues Size after AV probes hr=%08x size=%d\n", hr, has );

    /* IResourceMap::GetValue takes the same key shape (MainResourceMap) */
    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceManager", &rmfactory );
    if (SUCCEEDED( hr ))
    {
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT( rmfactory, 0 ))( rmfactory, &IID_IResourceManagerStatics_loc, &rms );
        if (SUCCEEDED( hr ))
        {
            void *rm = NULL;
            hr = CALL( rms, 6, &rm );  /* IResourceManagerStatics::Current */
            if (SUCCEEDED( hr ) && rm)
            {
                hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT( rm, 0 ))( rm, &IID_IResourceManager_loc, &rm2 );
                if (SUCCEEDED( hr ) && rm2)
                {
                    hr = CALL( rm2, 6, &map );  /* get_MainResourceMap */
                    if (SUCCEEDED( hr ) && map)
                    {
                        /* the map answers any key (key-as-value stub tier when
                         * the pri lookup misses), so the CCW key is judged by
                         * the *resource it resolves*, not by S_OK alone */
                        WCHAR base[64], got[64];
                        void *cand = NULL, *defctx = NULL;
                        HSTRING_HEADER hdr;
                        HSTRING hres = NULL;

                        base[0] = got[0] = 0;
                        WindowsCreateStringReference( L"Resources\\CalculatorModeText", 28, &hdr, &hres );
                        hr = CALL( map, 7, hres, &cand );  /* real-HSTRING baseline */
                        if (SUCCEEDED( hr ) && cand)
                        {
                            cand_get_value( cand, base, ARRAYSIZE( base ) );
                            CALL( cand, 2, (ULONG *)&hr );
                        }
                        ok( !wcscmp( base, L"Calculator" ), "ccw-key: baseline pri value is '%s'\n", wine_dbgstr_w( base ) );

                        cand = NULL;
                        hr = CALL( map, 7, (HSTRING)&ccw_reskey, &cand );  /* IResourceMap::GetValue */
                        ok( SUCCEEDED( hr ) && cand, "ccw-key: MainResourceMap::GetValue(CCW pri key) hr=%08x\n", hr );
                        if (cand)
                        {
                            cand_get_value( cand, got, ARRAYSIZE( got ) );
                            ok( !wcscmp( got, base ), "ccw-key: GetValue(CCW) -> '%s', baseline '%s'\n",
                                wine_dbgstr_w( got ), wine_dbgstr_w( base ) );
                            CALL( cand, 2, (ULONG *)&hr );
                        }

                        cand = NULL;
                        hr = CALL( rm2, 8, &defctx );  /* get_DefaultContext */
                        if (SUCCEEDED( hr ) && defctx)
                        {
                            got[0] = 0;
                            hr = CALL( map, 8, (HSTRING)&ccw_reskey, defctx, &cand );  /* GetValueForContext */
                            ok( SUCCEEDED( hr ) && cand, "ccw-key: GetValueForContext(CCW pri key) hr=%08x\n", hr );
                            if (cand)
                            {
                                cand_get_value( cand, got, ARRAYSIZE( got ) );
                                ok( !wcscmp( got, base ), "ccw-key: GetValueForContext(CCW) -> '%s', baseline '%s'\n",
                                    wine_dbgstr_w( got ), wine_dbgstr_w( base ) );
                                CALL( cand, 2, (ULONG *)&hr );
                            }
                            CALL( defctx, 2, (ULONG *)&hr );
                        }
                        WindowsDeleteString( hres );
                    }
                    CALL( rm2, 2, (ULONG *)&hr );
                }
                CALL( rm, 2, (ULONG *)&hr );
            }
            CALL( rms, 2, (ULONG *)&hr );
        }
        CALL( rmfactory, 2, (ULONG *)&hr );
    }

    /* IResourceContext2::SetQualifierValue(value, name) with both sides CCWs:
     * the value must land in that context's QualifierValues under the CCW name */
    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    if (SUCCEEDED( hr ))
    {
        hr = CALL( factory, 6, &own_ctx );  /* own instance, not the shared current context */
        if (SUCCEEDED( hr ) && own_ctx)
        {
            hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT( own_ctx, 0 ))( own_ctx, &IID_IResourceContext2_loc, &c2 );
            ok( hr == S_OK && c2, "ccw-key: QI IResourceContext2 hr=%08x\n", hr );
            if (SUCCEEDED( hr ) && c2)
            {
                hr = CALL( c2, 6, (HSTRING)&ccw_qvalue, (HSTRING)&ccw_qname );  /* SetQualifierValue(value, name) */
                ok( hr == S_OK, "ccw-key: SetQualifierValue(CCW value, CCW name) hr=%08x\n", hr );
                CALL( c2, 2, (ULONG *)&hr );
            }
            hr = CALL( own_ctx, 6, &own_mv );  /* get_QualifierValues */
            if (SUCCEEDED( hr ) && own_mv)
            {
                HSTRING_HEADER hdr;
                WindowsCreateStringReference( L"TestCcwQualifier", 16, &hdr, &hback );
                hval = NULL;
                hr = CALL( own_mv, 6, hback, &hval );
                ok( SUCCEEDED( hr ) && hval && hstring_eq( hval, L"ccw-value" ),
                    "ccw-key: SetQualifierValue stored under the CCW name hr=%08x\n", hr );
                if (hval) WindowsDeleteString( hval );
                WindowsDeleteString( hback );
                CALL( own_mv, 2, (ULONG *)&hr );
            }
            CALL( own_ctx, 2, (ULONG *)&hr );
        }
        CALL( factory, 2, (ULONG *)&hr );
    }

    if (hkey) WindowsDeleteString( hkey );
    if (hexpect) WindowsDeleteString( hexpect );
    CALL( mv, 2, (ULONG *)&hr );
done_factory:
    if (ctx) CALL( ctx, 2, (ULONG *)&hr );
    if (ctx2) CALL( ctx2, 2, (ULONG *)&hr );
    CALL( factory, 2, (ULONG *)&hr );
}

/* The IMapView<,>/IObservableMap<,> instance IIDs are computed with the same
 * combase algorithm the module's QI uses (prototype + include at the CCW-key
 * test above); the elements are the signature-grammar names, not the
 * System.String spelling. */
static const WCHAR *elems_mapview[] = { L"Windows.Foundation.Collections.IMapView`2",
                                        L"Windows.Foundation.String", L"Windows.Foundation.String" };

/* The smallest thing the app can hand to add_MapChanged: a delegate-shaped
 * object (vtable in slot 0, IUnknown at 0..2, Invoke where the delegate's own
 * slots begin).  It answers no QI at all, which is exactly the hostile case:
 * the callee must store it, not interrogate it. */
static HRESULT WINAPI obs_delegate_QI( void *iface, const IID *iid, void **out )
{
    if (out) *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI obs_delegate_AddRef( void *iface ) { return 2; }
static ULONG WINAPI obs_delegate_Release( void *iface ) { return 1; }
static HRESULT WINAPI obs_delegate_Invoke( void *iface, void *sender, void *args ) { return S_OK; }
static void *obs_delegate_vtbl[] =
{
    obs_delegate_QI, obs_delegate_AddRef, obs_delegate_Release,
    obs_delegate_Invoke, obs_delegate_Invoke, obs_delegate_Invoke, obs_delegate_Invoke,
};
static void *obs_delegate = obs_delegate_vtbl;

/*
 * IObservableMap<HSTRING,HSTRING> on ResourceContext.QualifierValues.
 *
 * The failfast c0000602 chain that this closes: mrt100_app projects the
 * QualifierValues map as a managed IDictionary<string,string>, whose WinRT
 * adapter QIs the map object for the pinterface IID of
 * IObservableMap`2<System.String,System.String> (the projection needs the
 * MapChanged event to keep the adapter's dirty state coherent).  Answering
 * E_NOINTERFACE raises the projection's COMException, and the exception's SR
 * message resource -- the key decoded live by the module's own mv_Get,
 * L"Windows.Foundation.Collections.MapChangedEventHandler`2[System.String,System.String]"
 * (sr13: Lookup ... -> ELEMENT_NOT_FOUND x5) -- misses in the PRI, so the
 * message load FailFasts c0000602.  The map has to answer the QI and the
 * WinRT event pair.
 *
 * The IID is computed, never copied: the base interface name plus 'string'
 * arguments through RoGetParameterizedTypeInstanceIID (same machinery the
 * module's QI uses, dlls/combase/roapi.c).  The expected constant is the
 * published IObservableMap<string,string> instance IID asserted by
 * dlls/combase/tests/roapi.c:917; it is *not* the IMapView<string,string> IID
 * 1e036276-2f60-55f6-b7f3-f86079e6900b that the map already answers
 * (dlls/combase/tests/roapi.c:904).  Both are pinned here so a base-IID table
 * swap cannot pass.
 *
 * Slot order for the observable interface: this tree's widl output for the
 * [eventadd]/[eventremove] pair emits IObservableMap_impl with add_MapChanged
 * then remove_MapChanged directly after IInspectable (build tree
 * include/windows.foundation.collections.h:846-847), the same two slots
 * wine's only IObservableMap implementation uses
 * (dlls/wintypes/propertyset.c:164-177).  The delegated block is IMap, because
 * include/windows.foundation.collections.idl:229 spells IObservableMap<K,V> as
 * `requires IMap<K,V>', and its members follow in the idl:215-221 order:
 * Lookup 8, Size 9, HasKey 10, GetView 11, Insert 12, Remove 13, Clear 14,
 * then the IIterable member of IMap's own requires clause at 15.  WinRT events
 * expose no get_ accessor in the ABI, so get_MapChanged sits past that block
 * (slot 16, unreachable for a caller that follows the metadata) and is pinned
 * by test_collections_abi_slots as the module's own accessor, not a slot.
 */

/* -------------------------------------------------------------------- */
/* Collections ABI slot probes: the layout the projections bind.         */
/*                                                                       */
/* Every expectation here is the member order of this tree's metadata    */
/* source, include/windows.foundation.collections.idl, read the way widl */
/* flattens it -- the interface's own members, then the block of its     */
/* `requires' clause:                                                    */
/*                                                                       */
/*   IMapView<K,V>       idl:198-206, requires IIterable<KVP>:           */
/*     [II 5][Get 6][Size 7][HasKey 8][Split 9][First 10]                */
/*   IVectorView<T>      idl:249-256, requires IIterable<T>:             */
/*     [II 5][GetAt 6][Size 7][IndexOf 8][GetMany 9][First 10]           */
/*   IObservableMap<K,V> idl:228-234, requires IMap<K,V> (idl:212-222):  */
/*     [II 5][add 6][remove 7][Lookup 8][Size 9][HasKey 10][GetView 11]  */
/*     [Insert 12][Remove 13][Clear 14][First 15]                        */
/*                                                                       */
/* Why the position of a member is a correctness question and not a      */
/* style one: the caller has no name to look up, it indexes.  Bind Size   */
/* and HasKey the other way round and the projection writes an INT32     */
/* boolean through a UINT32* and reads a count as a pointer; answer      */
/* GetView where the projection calls Split (idl:204 spells Split for    */
/* IMapView, GetView is the IMap member at idl:218) and it receives one  */
/* self-AddRef'd pointer where it unwraps two half-maps.  Both shapes    */
/* were live in this module before this test existed.                    */
/*                                                                       */
/* The last probe pins the retval rule: ELEMENT_NOT_FOUND is a code, not */
/* an excuse to leave the [out, retval] HSTRING unwritten -- the .NET     */
/* Native / C++/WinRT adapters materialise the value before they branch  */
/* on the code, so a null retval on the miss path is a null the caller   */
/* dereferences.                                                        */
/* -------------------------------------------------------------------- */

static const WCHAR *elems_vectorview_s[] = { L"Windows.Foundation.Collections.IVectorView`1",
                                             L"Windows.Foundation.String" };
static const WCHAR *elems_obsm_abi[] = { L"Windows.Foundation.Collections.IObservableMap`2",
                                         L"Windows.Foundation.String", L"Windows.Foundation.String" };

static void test_collections_abi_slots( void )
{
    static const WCHAR name_vv_s[] = L"Windows.Foundation.Collections.IVectorView`1<System.String>";
    /* published instance IID of IVectorView<string>: the literal the Calculator
     * MRT engine QIs on the Languages object (observed in the +resources trace:
     * ov_QueryInterface languages QI {2f13c006-a03a-5f69-b090-75a43e33423e}) */
    static const GUID iid_vv_s_published = {0x2f13c006, 0xa03a, 0x5f69, {0xb0, 0x90, 0x75, 0xa4, 0x3e, 0x33, 0x42, 0x3e}};
    void *factory = NULL, *statics = NULL, *ctx = NULL, *mv = NULL, *lang = NULL, *vv = NULL;
    void *rmfactory = NULL, *rms = NULL, *rm = NULL, *rm2 = NULL, *rmaps = NULL;
    void *map = NULL, *cand = NULL, *cand2 = NULL, *quals = NULL, *q0 = NULL;
    void *om = NULL, *view = NULL, *it = NULL, *pair = NULL;
    HSTRING gbuf[4];
    HSTRING hlang = NULL, habsent = NULL, hval = NULL, hkey = NULL, hitem = NULL, hres = NULL;
    HSTRING_HEADER hdr, hdr2, hdr3;  /* one per live reference: reusing a header rewrites the referenced string */
    UINT32 size = 0xdead, osize = 0, vsize = 0, qsize = 0, idx = 0xdead, got = 0xdead, qgot = 0xdead, len = 0xdead;
    INT32 has = -1, found = -1, qfound = -1, replaced = -1;
    GUID iid_vv, iid_obsm;
    void *first = (void *)0x1, *second = (void *)0x1;
    unsigned int i;
    HRESULT hr;

    set_pri_env();
    for (i = 0; i < ARRAYSIZE( gbuf ); i++) gbuf[i] = NULL;

    /* ---------------- IMapView<HSTRING,HSTRING> (QualifierValues) ---------------- */
    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "abi: ResourceContext factory hr=%08x\n", hr );
    if (hr != S_OK) return;
    hr = QI_SLOT( factory, &IID_IResourceContextStatics2_loc, &statics );
    if (FAILED( hr )) goto done;
    hr = CALL( statics, 6, &ctx );            /* GetForCurrentView */
    if (FAILED( hr ) || !ctx) goto done;
    hr = CALL( ctx, 6, &mv );                 /* get_QualifierValues */
    ok( hr == S_OK && mv, "abi: get_QualifierValues hr=%08x\n", hr );
    if (FAILED( hr ) || !mv) goto done;

    size = 0;
    hr = CALL( mv, 7, &size );                /* Size, slot 7 */
    ok( hr == S_OK && size > 0, "abi: QualifierValues Size@7 hr=%08x size=%u\n", hr, size );
    osize = size;

    WindowsCreateStringReference( L"Language", 8, &hdr, &hlang );
    WindowsCreateStringReference( L"NoSuchQualifier", 15, &hdr2, &habsent );

    has = -1;
    hr = CALL( mv, 8, hlang, &has );          /* HasKey, slot 8 */
    ok( hr == S_OK && has, "abi: QualifierValues HasKey@8(Language) hr=%08x has=%d\n", hr, has );
    has = -1;
    hr = CALL( mv, 8, habsent, &has );
    ok( hr == S_OK && !has, "abi: QualifierValues HasKey@8(absent) hr=%08x has=%d\n", hr, has );

    first = second = (void *)0x1;
    hr = CALL( mv, 9, &first, &second );      /* Split, slot 9 */
    ok( hr == E_NOTIMPL && !first && !second,
        "abi: QualifierValues Split@9 hr=%08x first=%p second=%p (want E_NOTIMPL and both out-params written null)\n",
        hr, first, second );

    it = NULL;
    hr = CALL( mv, 10, &it );                 /* IIterable::First, slot 10 */
    ok( hr == S_OK && it, "abi: QualifierValues First@10 hr=%08x\n", hr );
    if (it) CALL( it, 2, (ULONG *)&hr );

    /* the miss path: code ELEMENT_NOT_FOUND, retval written (empty) */
    hval = (HSTRING)0x1;
    hr = CALL( mv, 6, habsent, &hval );       /* Get, slot 6 */
    ok( hr == ELEMENT_NOT_FOUND_HR, "abi: QualifierValues Get@6(absent) hr=%08x (want ELEMENT_NOT_FOUND)\n", hr );
    ok( hval != (HSTRING)0x1, "abi: ELEMENT_NOT_FOUND left the retval unwritten (sentinel %p)\n", (void *)hval );
    /* the written value is the empty string: combase/string.c:221 represents a
     * zero-length source as the null handle, which is the empty HSTRING */
    len = WindowsGetStringLen( hval );
    ok( len == 0, "abi: not-found retval length=%u, want 0 (the empty string)\n", len );
    if (hval && hval != (HSTRING)0x1) WindowsDeleteString( hval );
    hval = NULL;

    /* ---------------- IObservableMap (delegates IMap at 8..15) ---------------- */
    hr = RoGetParameterizedTypeInstanceIID( 3, elems_obsm_abi, NULL, &iid_obsm, NULL );
    ok( hr == S_OK, "abi: PIID(IObservableMap<string,string>) hr=%08x\n", hr );
    if (SUCCEEDED( hr ))
    {
        hr = QI_SLOT( mv, &iid_obsm, &om );
        ok( hr == S_OK && om, "abi: QI(IObservableMap) hr=%08x obj=%p\n", hr, om );
        if (om)
        {
            hval = NULL;
            hr = CALL( om, 8, hlang, &hval );       /* Lookup, slot 8 */
            ok( hr == S_OK && hval, "abi: observable Lookup@8(Language) hr=%08x\n", hr );
            if (hval) WindowsDeleteString( hval );
            hval = NULL;

            size = 0;
            hr = CALL( om, 9, &size );              /* Size, slot 9 */
            ok( hr == S_OK && size == osize, "abi: observable Size@9 hr=%08x size=%u map size=%u\n", hr, size, osize );

            has = -1;
            hr = CALL( om, 10, hlang, &has );       /* HasKey, slot 10 */
            ok( hr == S_OK && has, "abi: observable HasKey@10(Language) hr=%08x has=%d\n", hr, has );

            view = NULL;
            hr = CALL( om, 11, &view );             /* GetView, slot 11 (an IMap member, idl:218) */
            ok( hr == S_OK && view, "abi: observable GetView@11 hr=%08x view=%p\n", hr, view );
            if (view)
            {
                vsize = 0;
                hr = CALL( view, 7, &vsize );       /* the view is an IMapView: Size at 7 */
                ok( hr == S_OK && vsize == osize, "abi: GetView@11 view Size@7 hr=%08x size=%u want %u\n", hr, vsize, osize );
                CALL( view, 2, (ULONG *)&hr );
            }
            replaced = -1;
            hr = CALL( om, 12, hlang, hlang, &replaced );  /* Insert, slot 12 (snapshot: accepted, no-op) */
            ok( hr == S_OK, "abi: observable Insert@12 hr=%08x\n", hr );
            hr = CALL( om, 13, hlang );             /* Remove, slot 13 */
            ok( hr == S_OK, "abi: observable Remove@13 hr=%08x\n", hr );
            hr = CALL( om, 14, 0 );                 /* Clear, slot 14 */
            ok( hr == S_OK, "abi: observable Clear@14 hr=%08x\n", hr );
            it = NULL;
            hr = CALL( om, 15, &it );               /* IMap's delegated IIterable::First, slot 15 */
            ok( hr == S_OK && it, "abi: observable First@15 hr=%08x\n", hr );
            if (it) CALL( it, 2, (ULONG *)&hr );
        }
    }

    /* ---------------- IVectorView<HSTRING> (Languages) ---------------- */
    hr = CALL( ctx, 11, &lang );                    /* get_Languages: IObservableVector face */
    ok( hr == S_OK && lang, "abi: get_Languages hr=%08x\n", hr );
    if (SUCCEEDED( hr ) && lang)
    {
        size = 0;
        hr = CALL( lang, 9, &size );                /* observable vector Size, slot 9 */
        ok( hr == S_OK && size >= 1, "abi: Languages Size@9 hr=%08x size=%u\n", hr, size );
        osize = size;

        hr = RoGetParameterizedTypeInstanceIID( 2, elems_vectorview_s, NULL, &iid_vv, NULL );
        ok( hr == S_OK, "abi: PIID(IVectorView<string>) hr=%08x\n", hr );
        if (SUCCEEDED( hr ))
        {
            ok( IsEqualGUID( &iid_vv, &iid_vv_s_published ), "abi: computed IVectorView<string> PIID %s, published %s\n",
                wine_dbgstr_guid( &iid_vv ), wine_dbgstr_guid( &iid_vv_s_published ) );
            vv = NULL;
            hr = QI_SLOT( lang, &iid_vv, &vv );
            ok( hr == S_OK && vv, "abi: QI(IVectorView<string>) hr=%08x obj=%p\n", hr, vv );
            if (vv)
            {
                check_class_name( vv, name_vv_s );

                size = 0;
                hr = CALL( vv, 7, &size );          /* Size, slot 7 */
                ok( hr == S_OK && size == osize, "abi: IVectorView<string> Size@7 hr=%08x size=%u want %u\n", hr, size, osize );

                hitem = NULL;
                hr = CALL( vv, 6, 0, &hitem );      /* GetAt, slot 6 (borrowed, not released) */
                ok( hr == S_OK && hitem, "abi: IVectorView<string> GetAt@6(0) hr=%08x\n", hr );

                idx = 0xdead;
                found = -1;
                hr = CALL( vv, 8, hitem, &idx, &found );   /* IndexOf, slot 8 */
                ok( hr == S_OK && found && idx == 0, "abi: IndexOf@8(item 0) hr=%08x idx=%u found=%d\n", hr, idx, found );

                got = 0xdead;
                gbuf[0] = gbuf[1] = NULL;
                hr = CALL( vv, 9, 0, 2, gbuf, &got );      /* GetMany, slot 9 */
                ok( hr == S_OK && got == (osize > 2 ? 2 : osize) && gbuf[0] == hitem,
                    "abi: GetMany@9(0,2) hr=%08x got=%u want %u first=%p item=%p\n",
                    hr, got, (osize > 2 ? 2u : osize), gbuf[0], hitem );

                it = NULL;
                hr = CALL( vv, 10, &it );           /* IIterable::First, slot 10 */
                ok( hr == S_OK && it, "abi: IVectorView<string> First@10 hr=%08x\n", hr );
                if (it) CALL( it, 2, (ULONG *)&hr );
            }
        }
        CALL( lang, 2, (ULONG *)&hr );
        lang = NULL;
    }

    /* ---------------- IMapView<HSTRING,IResourceMap*> + IVectorView<T*> ---------------- */
    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceManager", &rmfactory );
    ok( hr == S_OK, "abi: ResourceManager factory hr=%08x\n", hr );
    if (FAILED( hr )) goto done;
    hr = QI_SLOT( rmfactory, &IID_IResourceManagerStatics_loc, &rms );
    if (FAILED( hr )) goto done;
    hr = CALL( rms, 6, &rm );                       /* get_Current */
    if (FAILED( hr ) || !rm) goto done;
    hr = QI_SLOT( rm, &IID_IResourceManager_loc, &rm2 );
    if (FAILED( hr ) || !rm2) goto done;

    hr = CALL( rm2, 7, &rmaps );                    /* get_AllResourceMaps */
    ok( hr == S_OK && rmaps, "abi: get_AllResourceMaps hr=%08x\n", hr );
    if (SUCCEEDED( hr ) && rmaps)
    {
        size = 0;
        hr = CALL( rmaps, 7, &size );               /* Size, slot 7 */
        ok( hr == S_OK && size > 0, "abi: AllResourceMaps Size@7 hr=%08x size=%u\n", hr, size );
        first = second = (void *)0x1;
        hr = CALL( rmaps, 9, &first, &second );     /* Split, slot 9 */
        ok( hr == E_NOTIMPL && !first && !second, "abi: AllResourceMaps Split@9 hr=%08x first=%p second=%p\n", hr, first, second );
        it = NULL;
        hr = CALL( rmaps, 10, &it );                /* First, slot 10 */
        ok( hr == S_OK && it, "abi: AllResourceMaps First@10 hr=%08x\n", hr );
        if (it)
        {
            has = 0;
            hr = CALL( it, 6, &has );
            ok( hr == S_OK && has, "abi: AllResourceMaps MoveNext hr=%08x has=%d\n", hr, has );
            pair = NULL;
            hr = CALL( it, 7, &pair );
            if (SUCCEEDED( hr ) && pair)
            {
                hkey = NULL;
                hr = CALL( pair, 6, &hkey );        /* IKeyValuePair::Key */
                ok( hr == S_OK && hkey, "abi: pair Key hr=%08x\n", hr );
                if (SUCCEEDED( hr ) && hkey)
                {
                    has = -1;
                    hr = CALL( rmaps, 8, hkey, &has );  /* HasKey, slot 8 */
                    ok( hr == S_OK && has, "abi: AllResourceMaps HasKey@8(the key First yielded) hr=%08x has=%d\n", hr, has );
                    WindowsDeleteString( hkey );
                    hkey = NULL;
                }
                CALL( pair, 2, (ULONG *)&hr );
            }
            CALL( it, 2, (ULONG *)&hr );
            it = NULL;
        }
        CALL( rmaps, 2, (ULONG *)&hr );
        rmaps = NULL;
    }

    hr = CALL( rm2, 6, &map );                      /* get_MainResourceMap */
    if (SUCCEEDED( hr ) && map)
    {
        WindowsCreateStringReference( L"Calculator", 10, &hdr3, &hres );
        cand = NULL;
        hr = CALL( map, 7, hres, &cand );           /* IResourceMap::GetValue */
        ok( SUCCEEDED( hr ) && cand, "abi: GetValue(Calculator) hr=%08x\n", hr );
        if (SUCCEEDED( hr ) && cand)
        {
            hr = QI_SLOT( cand, &IID_IResourceCandidate_loc, &cand2 );
            if (SUCCEEDED( hr ))
            {
                hr = CALL( cand2, 6, &quals );      /* get_Qualifiers: IVectorView<ResourceQualifier> */
                ok( hr == S_OK && quals, "abi: get_Qualifiers hr=%08x\n", hr );
                if (SUCCEEDED( hr ) && quals)
                {
                    qsize = 0;
                    hr = CALL( quals, 7, &qsize );  /* Size, slot 7 */
                    ok( hr == S_OK && qsize > 0, "abi: Qualifiers Size@7 hr=%08x size=%u\n", hr, qsize );

                    q0 = NULL;
                    hr = CALL( quals, 6, 0, &q0 );  /* GetAt, slot 6 (borrowed) */
                    ok( hr == S_OK && q0, "abi: Qualifiers GetAt@6(0) hr=%08x\n", hr );

                    idx = 0xdead;
                    qfound = -1;
                    hr = CALL( quals, 8, q0, &idx, &qfound );   /* IndexOf, slot 8 */
                    ok( hr == S_OK && qfound && idx == 0, "abi: Qualifiers IndexOf@8(element 0) hr=%08x idx=%u found=%d\n",
                        hr, idx, qfound );

                    idx = 0xdead;
                    qfound = -1;
                    hr = CALL( quals, 8, &obs_delegate, &idx, &qfound );  /* not an element: no hit, no fault */
                    ok( hr == S_OK && !qfound, "abi: Qualifiers IndexOf@8(foreign object) hr=%08x found=%d\n", hr, qfound );

                    qgot = 0xdead;
                    gbuf[0] = gbuf[1] = NULL;
                    hr = CALL( quals, 9, 0, 2, gbuf, &qgot );  /* GetMany, slot 9 */
                    ok( hr == S_OK && qgot == (qsize > 2 ? 2 : qsize) && gbuf[0] == q0,
                        "abi: Qualifiers GetMany@9(0,2) hr=%08x got=%u want %u first=%p elem=%p\n",
                        hr, qgot, (qsize > 2 ? 2u : qsize), gbuf[0], q0 );

                    it = NULL;
                    hr = CALL( quals, 10, &it );    /* IIterable::First, slot 10 */
                    ok( hr == S_OK && it, "abi: Qualifiers First@10 hr=%08x\n", hr );
                    if (it) CALL( it, 2, (ULONG *)&hr );
                    CALL( quals, 2, (ULONG *)&hr );
                    quals = NULL;
                }
                CALL( cand2, 2, (ULONG *)&hr );
            }
            CALL( cand, 2, (ULONG *)&hr );
        }
        CALL( map, 2, (ULONG *)&hr );
        map = NULL;
    }

done:
    if (hval && hval != (HSTRING)0x1) WindowsDeleteString( hval );
    if (om) CALL( om, 2, (ULONG *)&hr );
    if (mv) CALL( mv, 2, (ULONG *)&hr );
    if (vv) CALL( vv, 2, (ULONG *)&hr );
    if (lang) CALL( lang, 2, (ULONG *)&hr );
    if (quals) CALL( quals, 2, (ULONG *)&hr );
    if (map) CALL( map, 2, (ULONG *)&hr );
    if (cand2) CALL( cand2, 2, (ULONG *)&hr );
    if (cand) CALL( cand, 2, (ULONG *)&hr );
    if (rmaps) CALL( rmaps, 2, (ULONG *)&hr );
    if (rm2) CALL( rm2, 2, (ULONG *)&hr );
    if (rm) CALL( rm, 2, (ULONG *)&hr );
    if (rms) CALL( rms, 2, (ULONG *)&hr );
    if (rmfactory) CALL( rmfactory, 2, (ULONG *)&hr );
    if (ctx) CALL( ctx, 2, (ULONG *)&hr );
    if (statics) CALL( statics, 2, (ULONG *)&hr );
    if (factory) CALL( factory, 2, (ULONG *)&hr );
}

static void test_observable_map( void )
{
    static const WCHAR *elems_obsm[] = { L"Windows.Foundation.Collections.IObservableMap`2",
                                         L"Windows.Foundation.String", L"Windows.Foundation.String" };
    static const WCHAR *elems_vec[] = { L"Windows.Foundation.Collections.IObservableVector`1",
                                        L"Windows.Foundation.String" };
    static const WCHAR name_obsm_ss[] = L"Windows.Foundation.Collections.IObservableMap`2<System.String,System.String>";
    static const GUID iid_obsm_expected = {0xac7f26f2, 0xfeb7, 0x5b2a, {0x8a, 0xc4, 0x34, 0x5b, 0xc6, 0x2c, 0xae, 0xde}};
    void *factory = NULL, *statics = NULL, *ctx = NULL, *mv = NULL, *om = NULL, *om2 = NULL, *handler = NULL;
    void *om_again = NULL;
    HSTRING hkey = NULL, hval = NULL;
    HSTRING_HEADER hdr;
    INT64 token = 0;
    GUID iid_obsm, iid_vec;
    HRESULT hr;

    hr = RoGetParameterizedTypeInstanceIID( 3, elems_obsm, NULL, &iid_obsm, NULL );
    ok( hr == S_OK, "obsmap: RoGetParameterizedTypeInstanceIID(IObservableMap`2<string,string>) hr=%08x\n", hr );
    if (FAILED( hr )) return;
    ok( IsEqualGUID( &iid_obsm, &iid_obsm_expected ),
        "obsmap: computed PIID %s, want published %s\n", wine_dbgstr_guid( &iid_obsm ), wine_dbgstr_guid( &iid_obsm_expected ) );

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "obsmap: factory hr=%08x\n", hr );
    if (hr != S_OK) return;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceContextStatics2_loc, &statics);
    if (FAILED( hr )) goto done_factory;
    hr = CALL( statics, 6, &ctx );  /* IResourceContextStatics2::GetForCurrentView */
    if (FAILED( hr ) || !ctx) goto done_factory;
    hr = CALL( ctx, 6, &mv );  /* IResourceContext::get_QualifierValues */
    ok( hr == S_OK && mv, "obsmap: get_QualifierValues hr=%08x\n", hr );
    if (FAILED( hr ) || !mv) goto done_factory;

    /* 1. the QI the app drives */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(mv, 0))(mv, &iid_obsm, &om);
    ok( hr == S_OK && om, "obsmap: QI(IObservableMap<string,string>) hr=%08x obj=%p\n", hr, om );

    /* 2. identity: the same object answers IMapView and IInspectable; the
     *    IMapView members of the observable vtable delegate (Lookup at the
     *    delegated-IMap slot returns the real qualifier value). */
    if (om)
    {
        void *inspectable = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(om, 0))(om, &IID_IInspectable_loc, &inspectable);
        ok( hr == S_OK && inspectable == mv, "obsmap: QI(IInspectable) from observable hr=%08x obj=%p mv=%p\n",
            hr, inspectable, mv );
        if (inspectable) CALL( inspectable, 2, (ULONG *)&hr );

        WindowsCreateStringReference( L"Language", 8, &hdr, &hkey );
        hval = NULL;
        hr = CALL( om, 8, hkey, &hval );  /* IMap::Lookup through the observable vtable */
        ok( SUCCEEDED( hr ) && hval, "obsmap: observable Lookup(Language) hr=%08x value=%p\n", hr, hval );
        if (hval) WindowsDeleteString( hval );
        WindowsDeleteString( hkey );
        hkey = NULL;

        /* 3. the WinRT event pair, with a minimal delegate IUnknown: the
         *    handler has to be accepted opaquely (this fixture's delegate
         *    answers no QueryInterface at all, so a callee that interrogates
         *    it fails the app's own registration). */
        hr = CALL( om, 6, &obs_delegate, &token );  /* add_MapChanged */
        ok( hr == S_OK, "obsmap: add_MapChanged hr=%08x\n", hr );
        ok( token != 0, "obsmap: add_MapChanged token=%I64d (must be nonzero)\n", token );

        hr = CALL( om, 16, &handler );  /* get_MapChanged */
        ok( hr == S_OK && handler, "obsmap: get_MapChanged hr=%08x handler=%p\n", hr, handler );

        hr = CALL( om, 7, token );  /* remove_MapChanged */
        ok( hr == S_OK, "obsmap: remove_MapChanged hr=%08x\n", hr );

        check_class_name( om, name_obsm_ss );
    }

    /* 4. COM identity across the two vtables of the one object: a second QI
     *    for the observable PIID hands back the same pointer, and the map
     *    object reached from the observable pointer is the map the context
     *    handed out (QualifierValues is a view, not a per-call object). */
    if (om)
    {
        om_again = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(om, 0))(om, &iid_obsm, &om_again);
        ok( hr == S_OK && om_again == om, "obsmap: QI identity hr=%08x obj=%p om=%p\n", hr, om_again, om );
        if (om_again) CALL( om_again, 2, (ULONG *)&hr );

        om2 = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(om, 0))(om, &IID_IResourceContext_loc, &om2);
        ok( hr == E_NOINTERFACE && !om2, "obsmap: QI(IResourceContext) from map hr=%08x obj=%p (expect E_NOINTERFACE)\n",
            hr, om2 );
    }

    /* 5. existing QI answers are unchanged: an unimplemented pinterface stays
     *    E_NOINTERFACE, the IMapView PIID still resolves. */
    hr = RoGetParameterizedTypeInstanceIID( 2, elems_vec, NULL, &iid_vec, NULL );
    if (SUCCEEDED( hr ))
    {
        om2 = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(mv, 0))(mv, &iid_vec, &om2);
        ok( hr == E_NOINTERFACE && !om2, "obsmap: QI(IObservableVector<string>) hr=%08x obj=%p (expect E_NOINTERFACE)\n",
            hr, om2 );
    }
    hr = RoGetParameterizedTypeInstanceIID( 3, elems_mapview, NULL, &iid_vec, NULL );
    if (SUCCEEDED( hr ))
    {
        om2 = NULL;
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(mv, 0))(mv, &iid_vec, &om2);
        ok( hr == S_OK && om2, "obsmap: QI(IMapView<string,string>) still hr=%08x obj=%p\n", hr, om2 );
        if (om2) CALL( om2, 2, (ULONG *)&hr );
    }

done_factory:
    if (handler) CALL( handler, 2, (ULONG *)&hr );
    if (om) CALL( om, 2, (ULONG *)&hr );
    if (hkey) WindowsDeleteString( hkey );
    if (mv) CALL( mv, 2, (ULONG *)&hr );
    if (ctx) CALL( ctx, 2, (ULONG *)&hr );
    if (statics) CALL( statics, 2, (ULONG *)&hr );
    CALL( factory, 2, (ULONG *)&hr );
}

/* -------------------------------------------------------------------- */
/* MapChangedEventHandler`2<System.String,System.String> event          */
/* registration on the QualifierValues map (sr15 gate evidence).        */
/*                                                                      */
/* (a) The literal the sr15 QI census saw on the map,                   */
/*     {94ea2b94-e9cc-49e0-c0ff-ee64ca8f5b90}, is IAgileObject, NOT a   */
/*     delegate PIID: include/objidlbase.idl:91 uuid(94ea2b94-...) and  */
/*     dlls/combase/roapi.c iid_names[] name it                          */
/*     "Windows.Foundation.IAgileObject", and it cannot be the output    */
/*     of RoGetParameterizedTypeInstanceIID: that call stamps Data3 with */
/*     version 5 and Data4[0] with the RFC-4122 variant                 */
/*     (dlls/combase/roapi.c:1036-1037), while the literal carries Data3 */
/*     0x49e0 and Data4[0] 0xc0.  The map answers it S_OK through       */
/*     mv_QueryInterface's IAgileObject branch.                          */
/*                                                                      */
/* (b) The delegate's own PIID is computed, never copied:               */
/*     RoGetParameterizedTypeInstanceIID over                            */
/*     "pinterface({179517f3-94ee-41f8-bddc-768a895544f3};string;string)" */
/*     with the base uuid from dlls/combase/roapi.c pinterface_names[]   */
/*     "Windows.Foundation.Collections.MapChangedEventHandler`2",        */
/*     agreeing with include/windows.foundation.collections.idl:179.     */
/*     The computation is anchored on the two published instances the    */
/*     same call reproduces -- IMapView<string,string>                   */
/*     1e036276-2f60-55f6-b7f3-f86079e6900b and IObservableMap<...>      */
/*     ac7f26f2-feb7-5b2a-8ac4-345bc62caede (dlls/combase/tests/roapi.c: */
/*     904, :917).  No published instance IID of the delegate exists in  */
/*     this tree, so the delegate value is pinned only as a regression   */
/*     guard on the anchored derivation: no expectation here is          */
/*     confirmed by the call that computes it.                           */
/*                                                                      */
/* main.c wiring under test: struct map_view (vt_mv/vt_om) answers the  */
/* two map PIIDs from mv_QueryInterface (map_view_vtbl,                  */
/* map_view_observable_vtbl); the delegate face is per-registration      */
/* (struct map_event_reg, map_event_handler_vtbl), reached through the   */
/* observable face's event pair (add@6/remove@7, handler out of the      */
/* trailing get_MapChanged@16), answering the delegate PIID,             */
/* IAgileObject and IInspectable as itself, GetIids S_OK/empty           */
/* (simple_get_iids), the delegate class name, and Invoke S_OK.          */
/* -------------------------------------------------------------------- */

static LONG ev_addrefs;

struct ev_delegate
{
    const void *vtbl;
    LONG ref;
};
static HRESULT WINAPI ev_delegate_QI( void *iface, const IID *iid, void **out )
{
    if (out) *out = NULL;
    return E_NOINTERFACE;  /* the projection's delegate answers no QI on this path */
}
static ULONG WINAPI ev_delegate_AddRef( void *iface )
{
    InterlockedIncrement( &ev_addrefs );
    return InterlockedIncrement( &((struct ev_delegate *)iface)->ref );
}
static ULONG WINAPI ev_delegate_Release( void *iface )
{
    return InterlockedDecrement( &((struct ev_delegate *)iface)->ref );
}
static HRESULT WINAPI ev_delegate_Invoke( void *iface, void *sender, void *args ) { return S_OK; }
static const void *ev_delegate_vtbl[] =
{
    ev_delegate_QI, ev_delegate_AddRef, ev_delegate_Release,
    ev_delegate_Invoke, ev_delegate_Invoke, ev_delegate_Invoke, ev_delegate_Invoke,
};
static struct ev_delegate ev_delegate = { ev_delegate_vtbl, 1 };

static void test_delegate_event_registration( void )
{
    static const WCHAR *elems_mce[] = { L"Windows.Foundation.Collections.MapChangedEventHandler`2",
                                        L"Windows.Foundation.String", L"Windows.Foundation.String" };
    static const WCHAR *elems_obsm_ev[] = { L"Windows.Foundation.Collections.IObservableMap`2",
                                            L"Windows.Foundation.String", L"Windows.Foundation.String" };
    static const WCHAR name_mce_ss[] = L"Windows.Foundation.Collections.MapChangedEventHandler`2<System.String,System.String>";
    static const WCHAR suffix_mce_ss[] = L"MapChangedEventHandler`2<System.String,System.String>";
    /* the observed sr15 QI census literal, verbatim */
    static const GUID observed_qi = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
    /* published instances the derivation must reproduce, verbatim from
     * dlls/combase/tests/roapi.c:904 and :917: these two anchor the computation */
    static const GUID iid_mapview_ss   = {0x1e036276, 0x2f60, 0x55f6, {0xb7, 0xf3, 0xf8, 0x60, 0x79, 0xe6, 0x90, 0x0b}};
    static const GUID iid_obsmap_ss    = {0xac7f26f2, 0xfeb7, 0x5b2a, {0x8a, 0xc4, 0x34, 0x5b, 0xc6, 0x2c, 0xae, 0xde}};
    /* the value the anchored derivation yields for the delegate, pinned as a
     * regression guard only: no published instance IID of the delegate exists
     * in this tree, so this literal is never its own expectation */
    static const GUID iid_mce_expected = {0xe2663f37, 0x2e1b, 0x500c, {0xad, 0x68, 0xc3, 0xed, 0x7a, 0x8f, 0x74, 0xc8}};
    void *factory = NULL, *statics = NULL, *ctx = NULL, *mv = NULL, *om = NULL, *hnd = NULL, *o = NULL;
    INT64 token = 0, token2 = 0;
    GUID iid_mce, derived;
    ULONG count = 0xdead;
    IID *iids = (void *)0x1;
    HSTRING name = NULL;
    const WCHAR *raw;
    const WCHAR *tail;
    HRESULT hr;

    /* 1. anchor the derivation on the two published instances (the same call the
     *    module's QI uses), then compute the delegate PIID with it */
    hr = RoGetParameterizedTypeInstanceIID( 3, elems_mapview, NULL, &derived, NULL );
    ok( hr == S_OK && IsEqualGUID( &derived, &iid_mapview_ss ),
        "evreg: anchor IMapView<string,string> computed %s, published %s hr=%08x\n",
        wine_dbgstr_guid( &derived ), wine_dbgstr_guid( &iid_mapview_ss ), hr );
    hr = RoGetParameterizedTypeInstanceIID( 3, elems_obsm_ev, NULL, &derived, NULL );
    ok( hr == S_OK && IsEqualGUID( &derived, &iid_obsmap_ss ),
        "evreg: anchor IObservableMap<string,string> computed %s, published %s hr=%08x\n",
        wine_dbgstr_guid( &derived ), wine_dbgstr_guid( &iid_obsmap_ss ), hr );
    if (FAILED( hr )) return;

    hr = RoGetParameterizedTypeInstanceIID( 3, elems_mce, NULL, &iid_mce, NULL );
    ok( hr == S_OK, "evreg: RoGetParameterizedTypeInstanceIID(MapChangedEventHandler`2<string,string>) hr=%08x\n", hr );
    if (FAILED( hr )) return;
    ok( IsEqualGUID( &iid_mce, &iid_mce_expected ),
        "evreg: derived delegate PIID %s, want %s\n", wine_dbgstr_guid( &iid_mce ), wine_dbgstr_guid( &iid_mce_expected ) );
    trace( "evreg: MapChangedEventHandler`2<string,string> PIID = %s\n", wine_dbgstr_guid( &iid_mce ) );

    /* 2. the observed literal pinned as what it is: IAgileObject, and provably
     *    not a derived pinterface IID (version/variant stamps, roapi.c:1036-1037) */
    ok( IsEqualGUID( &observed_qi, &IID_IAgileObject_loc ),
        "evreg: observed QI %s is IAgileObject %s (objidlbase.idl:91)\n",
        wine_dbgstr_guid( &observed_qi ), wine_dbgstr_guid( &IID_IAgileObject_loc ) );
    ok( (observed_qi.Data3 & 0xf000) != 0x5000, "evreg: observed Data3 %04x carries no UUIDv5 version stamp\n", observed_qi.Data3 );
    ok( observed_qi.Data4[0] > 0xbf, "evreg: observed Data4[0] %02x is outside the RFC-4122 variant stamp range\n",
        observed_qi.Data4[0] );
    ok( !IsEqualGUID( &iid_mce, &observed_qi ), "evreg: derived delegate PIID %s != observed literal %s\n",
        wine_dbgstr_guid( &iid_mce ), wine_dbgstr_guid( &observed_qi ) );

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "evreg: factory hr=%08x\n", hr );
    if (hr != S_OK) return;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(factory, 0))(factory, &IID_IResourceContextStatics2_loc, &statics);
    if (FAILED( hr )) goto done_factory;
    hr = CALL( statics, 6, &ctx );  /* IResourceContextStatics2::GetForCurrentView */
    if (FAILED( hr ) || !ctx) goto done_factory;
    hr = CALL( ctx, 6, &mv );  /* IResourceContext::get_QualifierValues */
    ok( hr == S_OK && mv, "evreg: get_QualifierValues hr=%08x\n", hr );
    if (FAILED( hr ) || !mv) goto done_factory;

    /* 3. the two map faces: the observable PIID answers (vt_om) and is the same
     *    COM object as the map; the delegate PIID is answered per-registration,
     *    so the map itself keeps E_NOINTERFACE for it (mv_QueryInterface) */
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(mv, 0))(mv, &iid_obsmap_ss, &om);
    ok( hr == S_OK && om, "evreg: QI(IObservableMap<string,string>) hr=%08x obj=%p\n", hr, om );
    if (!om) goto done_factory;
    o = NULL;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(om, 0))(om, &IID_IInspectable_loc, &o);
    ok( hr == S_OK && o == mv, "evreg: QI(IInspectable) from the observable face hr=%08x obj=%p mv=%p\n", hr, o, mv );
    if (o) CALL( o, 2, (ULONG *)&hr );
    o = NULL;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(mv, 0))(mv, &iid_mce, &o);
    ok( hr == E_NOINTERFACE && !o, "evreg: QI(delegate PIID) on the map itself hr=%08x obj=%p (the face is per-registration)\n",
        hr, o );

    /* 4. event add (observable slot 6): handler stored opaque (this fixture's
     *    delegate answers no QI at all), token is the registration */
    ev_addrefs = 0;
    token = 0;
    hr = CALL( om, 6, &ev_delegate, &token );  /* add_MapChanged */
    ok( hr == S_OK, "evreg: add_MapChanged hr=%08x\n", hr );
    ok( token != 0, "evreg: add_MapChanged token=%I64d (must be nonzero)\n", token );
    ok( ev_addrefs == 1, "evreg: add_MapChanged AddRef count %ld, want 1\n", (long)ev_addrefs );

    token2 = 0;
    hr = CALL( om, 6, &ev_delegate, &token2 );  /* a second registration */
    ok( hr == S_OK && token2 != 0 && token2 != token, "evreg: second add hr=%08x token=%I64d first=%I64d\n",
        hr, token2, token );

    /* 5. the delegate face: get_MapChanged (observable trailing slot 16) hands
     *    back a registration's handler face; the delegate PIID, IAgileObject and
     *    IInspectable all answer it as itself (a per-registration object, not the map) */
    hnd = NULL;
    hr = CALL( om, 16, &hnd );  /* get_MapChanged */
    ok( hr == S_OK && hnd, "evreg: get_MapChanged hr=%08x handler=%p\n", hr, hnd );
    if (!hnd) goto done_factory;
    o = NULL;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(hnd, 0))(hnd, &iid_mce, &o);
    ok( hr == S_OK && o == hnd, "evreg: QI(delegate PIID) on the handler face hr=%08x obj=%p hnd=%p\n", hr, o, hnd );
    if (o) CALL( o, 2, (ULONG *)&hr );
    o = NULL;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(hnd, 0))(hnd, &IID_IInspectable_loc, &o);
    ok( hr == S_OK && o == hnd, "evreg: QI(IInspectable) on the handler face hr=%08x obj=%p\n", hr, o );
    if (o) CALL( o, 2, (ULONG *)&hr );
    o = NULL;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(hnd, 0))(hnd, &IID_IAgileObject_loc, &o);
    ok( hr == S_OK && o == hnd, "evreg: QI(IAgileObject) on the handler face hr=%08x obj=%p\n", hr, o );
    if (o) CALL( o, 2, (ULONG *)&hr );

    /* 6. the handler face's IInspectable members: GetIids answers the empty set
     *    (main.c simple_get_iids), GetRuntimeClassName names the delegate type
     *    (the RCW parses it), and Invoke is the quiet S_OK of a snapshot view */
    count = 0xdead;
    iids = (void *)0x1;
    hr = CALL( hnd, 3, &count, &iids );  /* IInspectable::GetIids */
    ok( hr == S_OK && count == 0 && !iids, "evreg: handler GetIids hr=%08x count %u iids %p\n", hr, count, iids );
    name = NULL;
    hr = CALL( hnd, 4, &name );  /* IInspectable::GetRuntimeClassName */
    ok( hr == S_OK && name, "evreg: GetRuntimeClassName hr=%08x name=%p\n", hr, name );
    if (SUCCEEDED( hr ) && name)
    {
        raw = WindowsGetStringRawBuffer( name, NULL );
        ok( raw && !lstrcmpW( raw, name_mce_ss ), "evreg: class name %s, want %s\n",
            wine_dbgstr_w( raw ), wine_dbgstr_w( name_mce_ss ) );
        tail = raw + (lstrlenW( raw ) - lstrlenW( suffix_mce_ss ));
        ok( raw && tail >= raw && !lstrcmpW( tail, suffix_mce_ss ),
            "evreg: class name %s ends with %s\n", wine_dbgstr_w( raw ), wine_dbgstr_w( suffix_mce_ss ) );
        WindowsDeleteString( name );
    }
    hr = CALL( hnd, 6, NULL, NULL );  /* the delegate's own Invoke: never raised on a snapshot view */
    ok( hr == S_OK, "evreg: handler face Invoke hr=%08x\n", hr );
    CALL( hnd, 2, (ULONG *)&hr );  /* the registration keeps its own list reference */
    hnd = NULL;

    /* 7. event remove (observable slot 7): the token is found and dropped;
     *    removing twice, an unknown token and 0 stay S_OK (Dispose path) */
    hr = CALL( om, 7, token );
    ok( hr == S_OK, "evreg: remove_MapChanged(%I64d) hr=%08x\n", token, hr );
    hr = CALL( om, 7, token );
    ok( hr == S_OK, "evreg: remove_MapChanged(%I64d) twice hr=%08x\n", token, hr );
    hr = CALL( om, 7, token2 );
    ok( hr == S_OK, "evreg: remove_MapChanged(%I64d) hr=%08x\n", token2, hr );
    hr = CALL( om, 7, 0 );
    ok( hr == S_OK, "evreg: remove_MapChanged(0) hr=%08x\n", hr );

    /* 8. existing answers of mv_QueryInterface are unchanged: the observed
     *    IAgileObject probe, the IMapView PIID the app drives, and an
     *    unimplemented pinterface all keep their behaviour. */
    o = NULL;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(mv, 0))(mv, &observed_qi, &o);
    ok( hr == S_OK && o == mv, "evreg: QI(observed IAgileObject) hr=%08x obj=%p mv=%p\n", hr, o, mv );
    if (o) CALL( o, 2, (ULONG *)&hr );
    o = NULL;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(mv, 0))(mv, &iid_mapview_ss, &o);
    ok( hr == S_OK && o, "evreg: QI(IMapView<string,string>) hr=%08x obj=%p\n", hr, o );
    if (o) CALL( o, 2, (ULONG *)&hr );
    o = NULL;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, const IID *, void **))VT(mv, 0))(mv, &IID_IResourceContext_loc, &o);
    ok( hr == E_NOINTERFACE && !o, "evreg: QI(IResourceContext) from the map hr=%08x obj=%p (expect E_NOINTERFACE)\n",
        hr, o );

done_factory:
    if (hnd) CALL( hnd, 2, (ULONG *)&hr );
    if (om) CALL( om, 2, (ULONG *)&hr );
    if (mv) CALL( mv, 2, (ULONG *)&hr );
    if (ctx) CALL( ctx, 2, (ULONG *)&hr );
    if (statics) CALL( statics, 2, (ULONG *)&hr );
    CALL( factory, 2, (ULONG *)&hr );
}

/*
 * Languages shape (vtslot wave): the mrt100_app CoreRT __HCODEC adapter QIs
 * the ResourceContext::Languages pointer for IObservableVector<string>
 * ({11d3b13a-...}) and IVectorView<string> ({2f13c006-...}) back to back and
 * then drives the read slots; a failfast c0000602 follows the {2f13c006} QI
 * on the deployed build.  This test pins the shape it consumes: exactly one
 * element L"en-US", IDL slot order on both faces
 * (view: [GetAt 6][Size 7][IndexOf 8][GetMany 9][First 10], idl:252-255;
 *  observable: [add 6][remove 7][GetAt 8][Size 9][GetView 10]..., idl:265-276,
 *  286-288), E_BOUNDS past the end, and GetMany filling exactly one.
 */
static void test_languages_shape(void)
{
    static const WCHAR *elems_observablev_s[] = { L"Windows.Foundation.Collections.IObservableVector`1",
                                                  L"Windows.Foundation.String" };
    static const WCHAR lang_enus[] = L"en-US";
    void *factory = NULL, *statics = NULL, *ctx = NULL, *langs = NULL, *vv = NULL, *view = NULL;
    HSTRING hitem = NULL, hprobe = NULL, hbuf[2];
    GUID iid_ov = {0}, iid_vv = {0};
    UINT32 size = 0, got = 0;
    INT32 found = -1;
    HRESULT hr;

    hr = get_factory( L"Windows.ApplicationModel.Resources.Core.ResourceContext", &factory );
    ok( hr == S_OK, "shape: ResourceContext factory hr=%08x\n", hr );
    if (FAILED( hr )) return;
    hr = QI_SLOT( factory, &IID_IResourceContextStatics2_loc, &statics );
    if (SUCCEEDED( hr )) hr = CALL( statics, 6, &ctx );        /* GetForCurrentView */
    else hr = CALL( factory, 6, &ctx );                        /* ActivateInstance */
    ok( SUCCEEDED( hr ) && ctx, "shape: context hr=%08x ctx=%p\n", hr, ctx );
    if (FAILED( hr ) || !ctx) goto done;

    hr = CALL( ctx, 11, &langs );                              /* IResourceContext::get_Languages */
    ok( hr == S_OK && langs, "shape: get_Languages@11 hr=%08x\n", hr );
    if (FAILED( hr ) || !langs) goto done;

    /* ---- IObservableVector<string> face ({11d3b13a}) ---- */
    hr = RoGetParameterizedTypeInstanceIID( 2, elems_observablev_s, NULL, &iid_ov, NULL );
    ok( hr == S_OK, "shape: PIID(IObservableVector<string>) hr=%08x\n", hr );
    if (SUCCEEDED( hr ))
    {
        void *ov = NULL;
        hr = QI_SLOT( langs, &iid_ov, &ov );
        ok( hr == S_OK && ov, "shape: QI(IObservableVector<string> %s) hr=%08x obj=%p\n",
            wine_dbgstr_guid( &iid_ov ), hr, ov );
        if (SUCCEEDED( hr ) && ov)
        {
            size = 0;
            hr = CALL( ov, 9, &size );                         /* Size, slot 9 */
            ok( hr == S_OK && size == 1, "shape: observable Size@9 hr=%08x size=%u want 1\n", hr, size );

            hitem = NULL;
            hr = CALL( ov, 8, 0, &hitem );                     /* GetAt, slot 8 */
            ok( hr == S_OK && hitem, "shape: observable GetAt@8(0) hr=%08x\n", hr );
            if (SUCCEEDED( hr ) && hitem)
                ok( !lstrcmpW( WindowsGetStringRawBuffer( hitem, NULL ), lang_enus ),
                    "shape: observable GetAt@8(0) is %s want en-US\n",
                    wine_dbgstr_w( WindowsGetStringRawBuffer( hitem, NULL ) ) );

            hbuf[0] = hbuf[1] = NULL; got = 0xdead;
            hr = CALL( ov, 18, 0, 1, hbuf, &got );             /* GetMany, slot 18 */
            ok( hr == S_OK && got == 1 && hbuf[0], "shape: observable GetMany@18(0,1) hr=%08x got=%u\n", hr, got );

            view = NULL;
            hr = CALL( ov, 10, &view );                        /* GetView, slot 10 */
            ok( hr == S_OK && view, "shape: observable GetView@10 hr=%08x\n", hr );
            if (SUCCEEDED( hr ) && view)
            {
                size = 0;
                hr = CALL( view, 7, &size );                   /* view Size, slot 7 */
                ok( hr == S_OK && size == 1, "shape: GetView face Size@7 hr=%08x size=%u want 1\n", hr, size );
                CALL( view, 2, (ULONG *)&hr );
                view = NULL;
            }
            CALL( ov, 2, (ULONG *)&hr );
        }
    }

    /* ---- IVectorView<string> face ({2f13c006}) ---- */
    hr = RoGetParameterizedTypeInstanceIID( 2, elems_vectorview_s, NULL, &iid_vv, NULL );
    ok( hr == S_OK, "shape: PIID(IVectorView<string>) hr=%08x\n", hr );
    if (FAILED( hr )) goto done;
    hr = QI_SLOT( langs, &iid_vv, &vv );
    ok( hr == S_OK && vv, "shape: QI(IVectorView<string> %s) hr=%08x obj=%p\n",
        wine_dbgstr_guid( &iid_vv ), hr, vv );
    if (FAILED( hr ) || !vv) goto done;

    size = 0;
    hr = CALL( vv, 7, &size );                                 /* Size, slot 7 */
    ok( hr == S_OK && size == 1, "shape: view Size@7 hr=%08x size=%u want 1\n", hr, size );

    hitem = NULL;
    hr = CALL( vv, 6, 0, &hitem );                             /* GetAt, slot 6 */
    ok( hr == S_OK && hitem, "shape: view GetAt@6(0) hr=%08x\n", hr );
    if (SUCCEEDED( hr ) && hitem)
        ok( !lstrcmpW( WindowsGetStringRawBuffer( hitem, NULL ), lang_enus ),
            "shape: view GetAt@6(0) is %s want en-US\n",
            wine_dbgstr_w( WindowsGetStringRawBuffer( hitem, NULL ) ) );

    hprobe = NULL;
    if (SUCCEEDED( WindowsCreateString( L"en-US", 5, &hprobe ) ))
    {
        UINT32 idx = 0xdead;
        found = -1;
        hr = CALL( vv, 8, hprobe, &idx, &found );              /* IndexOf, slot 8 */
        ok( hr == S_OK && found && idx == 0, "shape: IndexOf@8(en-US) hr=%08x idx=%u found=%d\n", hr, idx, found );
    }
    WindowsDeleteString( hprobe );
    hprobe = NULL;
    if (SUCCEEDED( WindowsCreateString( L"fr-FR", 5, &hprobe ) ))
    {
        UINT32 idx = 0xdead;
        found = -1;
        hr = CALL( vv, 8, hprobe, &idx, &found );              /* IndexOf, slot 8 */
        ok( hr == S_OK && !found, "shape: IndexOf@8(fr-FR) hr=%08x found=%d want miss\n", hr, found );
    }
    WindowsDeleteString( hprobe );

    hr = CALL( vv, 6, 1, &hprobe );                            /* GetAt past the end */
    ok( hr == E_BOUNDS, "shape: view GetAt@6(1) hr=%08x want E_BOUNDS\n", hr );

    hbuf[0] = hbuf[1] = NULL; got = 0xdead;
    hr = CALL( vv, 9, 0, 1, hbuf, &got );                      /* GetMany, slot 9 */
    ok( hr == S_OK && got == 1 && hbuf[0], "shape: view GetMany@9(0,1) hr=%08x got=%u want 1\n", hr, got );

    hbuf[0] = hbuf[1] = NULL; got = 0xdead;
    hr = CALL( vv, 9, 0, 2, hbuf, &got );                      /* GetMany oversized buffer */
    ok( hr == S_OK && got == 1, "shape: view GetMany@9(0,2) hr=%08x got=%u want 1\n", hr, got );

done:
    if (vv) CALL( vv, 2, (ULONG *)&hr );
    if (langs) CALL( langs, 2, (ULONG *)&hr );
    if (ctx) CALL( ctx, 2, (ULONG *)&hr );
    if (statics) CALL( statics, 2, (ULONG *)&hr );
    if (factory) CALL( factory, 2, (ULONG *)&hr );
}

START_TEST(core)
{
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize hr=%08x\n", hr);

    test_resourcecontext_activation();
    set_pri_env();
    test_resourcemanager_lookup();
    test_real_pri_lookup();
    test_virtualized_hstring();
    test_ccw_string_key();
    test_iagile();
    test_getiids_nullargs();
    test_qualifier_set();
    test_current_context_reentrancy();
    test_reference_tracker_identity();
    test_parameterized_class_names();
    test_memorymanager_activation();
    test_observable_map();
    test_delegate_event_registration();
    test_collections_abi_slots();
    test_languages_shape();

    RoUninitialize();
}
