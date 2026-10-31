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
    test_memorymanager_activation();

    RoUninitialize();
}
