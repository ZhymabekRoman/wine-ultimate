/*
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

#include <stdarg.h>
#define COBJMACROS
#include "initguid.h"
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "imm.h"
#include "winstring.h"
#include "roapi.h"
#include "weakreference.h"
#include "wine/test.h"

#define WIDL_using_Windows_UI_ViewManagement_Core
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_UI_Core
#define WIDL_using_Windows_UI_Text_Core
#define WIDL_using_Windows_Globalization
#include "windows.globalization.h"
#include "windows.ui.viewmanagement.core.h"
#include "windows.ui.text.core.h"


#define check_interface(obj, iid, supported) _check_interface(__LINE__, obj, iid, supported)
static void _check_interface(unsigned int line, void *obj, const IID *iid, BOOL supported)
{
    IUnknown *iface = obj, *unknown;
    HRESULT hr;

    hr = IUnknown_QueryInterface(iface, iid, (void **)&unknown);
    ok_(__FILE__, line)(hr == S_OK || (!supported && hr == E_NOINTERFACE), "Got unexpected hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(unknown);
}

static void test_CoreInputViewStatics(void)
{
    IActivationFactory *factory;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString(RuntimeClass_Windows_UI_ViewManagement_Core_CoreInputView,
                             wcslen(RuntimeClass_Windows_UI_ViewManagement_Core_CoreInputView), &str);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    WindowsDeleteString(str);
    ok(hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "got hr %#lx.\n", hr);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("%s runtimeclass not registered, skipping tests.\n",
                 wine_dbgstr_w(RuntimeClass_Windows_UI_ViewManagement_Core_CoreInputView));
        return;
    }

    check_interface(factory, &IID_IUnknown, TRUE);
    check_interface(factory, &IID_IInspectable, TRUE);
    check_interface(factory, &IID_IActivationFactory, TRUE);
    check_interface(factory, &IID_ICoreInputViewStatics, TRUE);
    /* WinAppSDK C++/WinRT resolves an agile reference on every activation factory before using it. */
    check_interface(factory, &IID_IAgileObject, TRUE);

    ref = IActivationFactory_Release(factory);
    ok(ref == 1, "Got unexpected refcount %ld.\n", ref);
}

static void test_CoreInputView(void)
{
    ICoreInputViewStatics *core_input_view_statics;
    IVectorView_CoreInputViewOcclusion *occlusions;
    ICoreInputView *core_input_view;
    IActivationFactory *factory;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString(RuntimeClass_Windows_UI_ViewManagement_Core_CoreInputView,
                             wcslen(RuntimeClass_Windows_UI_ViewManagement_Core_CoreInputView), &str);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    WindowsDeleteString(str);
    ok(hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "got hr %#lx.\n", hr);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("%s runtimeclass not registered, skipping tests.\n",
                 wine_dbgstr_w(RuntimeClass_Windows_UI_ViewManagement_Core_CoreInputView));
        return;
    }

    hr = IActivationFactory_QueryInterface(factory, &IID_ICoreInputViewStatics,
                                           (void **)&core_input_view_statics);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    hr = ICoreInputViewStatics_GetForCurrentView(core_input_view_statics, &core_input_view);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    check_interface(core_input_view, &IID_IUnknown, TRUE);
    check_interface(core_input_view, &IID_IInspectable, TRUE);
    check_interface(core_input_view, &IID_IAgileObject, TRUE);
    check_interface(core_input_view, &IID_IWeakReferenceSource, TRUE);
    check_interface(core_input_view, &IID_ICoreInputView, TRUE);
    check_interface(core_input_view, &IID_ICoreInputView2, TRUE);
    check_interface(core_input_view, &IID_ICoreInputView3, TRUE);
    check_interface(core_input_view, &IID_ICoreInputView4, TRUE);

    hr = ICoreInputView_GetCoreInputViewOcclusions(core_input_view, &occlusions);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    IVectorView_CoreInputViewOcclusion_Release(occlusions);

    ICoreInputView_Release(core_input_view);

    ref = ICoreInputViewStatics_Release(core_input_view_statics);
    ok(ref == 2, "Got unexpected refcount %ld.\n", ref);
    ref = IActivationFactory_Release(factory);
    ok(ref == 1, "Got unexpected refcount %ld.\n", ref);
}

static void test_CoreTextServicesManager(void)
{
    ICoreTextServicesManager *core_text_manager;
    ICoreTextServicesManagerStatics *core_text_manager_stat;
    IActivationFactory *factory;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString(RuntimeClass_Windows_UI_Text_Core_CoreTextServicesManager,
                             wcslen(RuntimeClass_Windows_UI_Text_Core_CoreTextServicesManager), &str);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    WindowsDeleteString(str);
    ok(hr == S_OK || broken(hr == REGDB_E_CLASSNOTREG), "got hr %#lx.\n", hr);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("%s runtimeclass not registered, skipping tests.\n",
                 wine_dbgstr_w(RuntimeClass_Windows_UI_Text_Core_CoreTextServicesManager));
        return;
    }

    hr = IActivationFactory_QueryInterface(factory, &IID_ICoreTextServicesManagerStatics,
                                           (void **)&core_text_manager_stat);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    hr = ICoreTextServicesManagerStatics_GetForCurrentView(core_text_manager_stat, &core_text_manager);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    check_interface(core_text_manager, &IID_IUnknown, TRUE);
    check_interface(core_text_manager, &IID_IInspectable, TRUE);
    check_interface(core_text_manager, &IID_IAgileObject, TRUE);
    check_interface(core_text_manager, &IID_ICoreTextServicesManagerStatics, FALSE);
    check_interface(core_text_manager, &IID_ICoreTextServicesManager, TRUE);

    ICoreTextServicesManager_Release(core_text_manager);
    ref = ICoreTextServicesManagerStatics_Release(core_text_manager_stat);
    ok(ref == 2, "Got unexpected refcount %ld.\n", ref);
    ref = IActivationFactory_Release(factory);
    ok(ref == 1, "Got unexpected refcount %ld.\n", ref);
}

static ICoreTextEditContext *create_edit_context(void)
{
    ICoreTextServicesManager *manager;
    ICoreTextServicesManagerStatics *statics;
    IActivationFactory *factory;
    ICoreTextEditContext *context;
    HSTRING str = NULL;
    HRESULT hr;

    hr = WindowsCreateString(RuntimeClass_Windows_UI_Text_Core_CoreTextServicesManager,
                             wcslen(RuntimeClass_Windows_UI_Text_Core_CoreTextServicesManager), &str);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    WindowsDeleteString(str);
    if (hr == REGDB_E_CLASSNOTREG) return NULL;
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    hr = IActivationFactory_QueryInterface(factory, &IID_ICoreTextServicesManagerStatics, (void **)&statics);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = ICoreTextServicesManagerStatics_GetForCurrentView(statics, &manager);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = ICoreTextServicesManager_CreateEditContext(manager, &context);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    ICoreTextServicesManager_Release(manager);
    ICoreTextServicesManagerStatics_Release(statics);
    IActivationFactory_Release(factory);
    return context;
}

/* A single recording handler shared across all the CoreTextEditContext events. */
static int event_order[16];
static int event_count;

static HRESULT WINAPI handler_QueryInterface(ITypedEventHandler_CoreTextEditContext_IInspectable *iface,
                                             REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable)
        || IsEqualGUID(iid, &IID_IAgileObject)
        || IsEqualGUID(iid, &IID_ITypedEventHandler_CoreTextEditContext_IInspectable))
    {
        *out = iface;
        ITypedEventHandler_CoreTextEditContext_IInspectable_AddRef((ITypedEventHandler_CoreTextEditContext_IInspectable *)*out);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI handler_AddRef(ITypedEventHandler_CoreTextEditContext_IInspectable *iface) { return 2; }
static ULONG WINAPI handler_Release(ITypedEventHandler_CoreTextEditContext_IInspectable *iface) { return 1; }

struct recording_handler
{
    ITypedEventHandler_CoreTextEditContext_IInspectable iface;
    int *counter;
};

static HRESULT WINAPI handler_Invoke(ITypedEventHandler_CoreTextEditContext_IInspectable *iface,
                                     ICoreTextEditContext *sender, IInspectable *args)
{
    struct recording_handler *impl = CONTAINING_RECORD(iface, struct recording_handler, iface);
    (*impl->counter)++;
    if (event_count < (int)ARRAYSIZE(event_order)) event_order[event_count++] = *impl->counter;
    return S_OK;
}

static const struct ITypedEventHandler_CoreTextEditContext_IInspectableVtbl handler_vtbl =
{
    handler_QueryInterface,
    handler_AddRef,
    handler_Release,
    handler_Invoke,
};

static void test_CoreTextEditContext(void)
{
    ICoreTextEditContext *context = create_edit_context();
    CoreTextInputScope scope;
    CoreTextInputPaneDisplayPolicy policy;
    HSTRING name = NULL, get_name = NULL;
    boolean readonly;
    HRESULT hr;
    LONG ref;

    if (!context)
    {
        win_skip("CoreTextEditContext not creatable, skipping tests.\n");
        return;
    }

    check_interface(context, &IID_IUnknown, TRUE);
    check_interface(context, &IID_IInspectable, TRUE);
    check_interface(context, &IID_IAgileObject, TRUE);
    check_interface(context, &IID_ICoreTextEditContext, TRUE);
    check_interface(context, &IID_ICoreInputView, FALSE);

    /* Name round-trip. */
    hr = WindowsCreateString(L"myedit", 6, &name);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = ICoreTextEditContext_put_Name(context, name);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = ICoreTextEditContext_get_Name(context, &get_name);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(!lstrcmpW(WindowsGetStringRawBuffer(get_name, NULL), L"myedit"), "Got unexpected name.\n");
    WindowsDeleteString(name);
    WindowsDeleteString(get_name);

    /* InputScope round-trip. */
    hr = ICoreTextEditContext_put_InputScope(context, CoreTextInputScope_Search);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = ICoreTextEditContext_get_InputScope(context, &scope);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(scope == CoreTextInputScope_Search, "Got unexpected scope %d.\n", scope);

    /* IsReadOnly round-trip. */
    hr = ICoreTextEditContext_put_IsReadOnly(context, TRUE);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    readonly = FALSE;
    hr = ICoreTextEditContext_get_IsReadOnly(context, &readonly);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(readonly, "Expected read-only.\n");

    /* InputPaneDisplayPolicy round-trip. */
    hr = ICoreTextEditContext_put_InputPaneDisplayPolicy(context, CoreTextInputPaneDisplayPolicy_Manual);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = ICoreTextEditContext_get_InputPaneDisplayPolicy(context, &policy);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    ok(policy == CoreTextInputPaneDisplayPolicy_Manual, "Got unexpected policy %d.\n", policy);

    /* Event registration/unregistration round-trips. */
    {
        struct recording_handler handler_obj = {{&handler_vtbl}, NULL};
        EventRegistrationToken token;
        void *handler = &handler_obj.iface;

        hr = ICoreTextEditContext_add_CompositionStarted(context, handler, &token);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
        hr = ICoreTextEditContext_remove_CompositionStarted(context, token);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

        hr = ICoreTextEditContext_add_TextUpdating(context, handler, &token);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
        hr = ICoreTextEditContext_remove_TextUpdating(context, token);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

        hr = ICoreTextEditContext_add_LayoutRequested(context, handler, &token);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
        hr = ICoreTextEditContext_remove_LayoutRequested(context, token);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

        hr = ICoreTextEditContext_add_CompositionCompleted(context, handler, &token);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
        hr = ICoreTextEditContext_remove_CompositionCompleted(context, token);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

        hr = ICoreTextEditContext_add_FocusRemoved(context, handler, &token);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
        hr = ICoreTextEditContext_remove_FocusRemoved(context, token);
        ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    }

    hr = ICoreTextEditContext_NotifyFocusEnter(context);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);
    hr = ICoreTextEditContext_NotifyFocusLeave(context);
    ok(hr == S_OK, "Got unexpected hr %#lx.\n", hr);

    ref = ICoreTextEditContext_Release(context);
    ok(ref == 0, "Got unexpected refcount %ld.\n", ref);
}

static void test_CoreTextEditContext_IME(void)
{
    static const WCHAR class_name[] = {'w','i','n','e','t','e','x','t','i','n','p','u','t',0};
    struct recording_handler started = {{&handler_vtbl}, NULL};
    struct recording_handler updating = {{&handler_vtbl}, NULL};
    struct recording_handler completed = {{&handler_vtbl}, NULL};
    WNDCLASSEXW wc = {0};
    ICoreTextEditContext *context;
    EventRegistrationToken t1, t2, t3;
    HWND hwnd;
    int started_n = 0, updating_n = 0, completed_n = 0;

    context = create_edit_context();
    if (!context)
    {
        win_skip("CoreTextEditContext not creatable, skipping IME tests.\n");
        return;
    }

    started.counter = &started_n;
    updating.counter = &updating_n;
    completed.counter = &completed_n;
    event_count = 0;

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = class_name;
    ok(RegisterClassExW(&wc), "Failed to register class.\n");
    hwnd = CreateWindowExW(0, class_name, L"", WS_OVERLAPPEDWINDOW, 0, 0, 200, 200, NULL, NULL, wc.hInstance, NULL);
    ok(hwnd != NULL, "Failed to create window.\n");
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(hwnd);

    ok(ICoreTextEditContext_add_CompositionStarted(context, (void *)&started.iface, &t1) == S_OK, "add failed.\n");
    ok(ICoreTextEditContext_add_TextUpdating(context, (void *)&updating.iface, &t2) == S_OK, "add failed.\n");
    ok(ICoreTextEditContext_add_CompositionCompleted(context, (void *)&completed.iface, &t3) == S_OK, "add failed.\n");

    /* Focus enter attaches the window to IMM32. */
    ok(ICoreTextEditContext_NotifyFocusEnter(context) == S_OK, "NotifyFocusEnter failed.\n");

    /* Simulate an IME composition cycle. */
    SendMessageW(hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
    ok(started_n == 1, "Expected CompositionStarted, got %d.\n", started_n);

    SendMessageW(hwnd, WM_IME_COMPOSITION, 0, GCS_COMPSTR);
    ok(updating_n == 1, "Expected TextUpdating, got %d.\n", updating_n);

    SendMessageW(hwnd, WM_IME_ENDCOMPOSITION, 0, 0);
    ok(completed_n == 1, "Expected CompositionCompleted, got %d.\n", completed_n);

    /* Focus leave detaches cleanly. */
    ok(ICoreTextEditContext_NotifyFocusLeave(context) == S_OK, "NotifyFocusLeave failed.\n");

    ok(ICoreTextEditContext_remove_CompositionStarted(context, t1) == S_OK, "remove failed.\n");
    ok(ICoreTextEditContext_remove_TextUpdating(context, t2) == S_OK, "remove failed.\n");
    ok(ICoreTextEditContext_remove_CompositionCompleted(context, t3) == S_OK, "remove failed.\n");

    ICoreTextEditContext_Release(context);
    DestroyWindow(hwnd);
    UnregisterClassW(class_name, wc.hInstance);
}

START_TEST(textinput)
{
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK, "RoInitialize failed, hr %#lx\n", hr);

    test_CoreInputViewStatics();
    test_CoreInputView();
    test_CoreTextServicesManager();
    test_CoreTextEditContext();
    test_CoreTextEditContext_IME();

    RoUninitialize();
}
