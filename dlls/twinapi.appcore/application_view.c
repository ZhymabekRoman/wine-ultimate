/* WinRT Windows.UI.ViewManagement.ApplicationView implementation
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

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

static const GUID IID_IApplicationView_alt =
    {0xd222d3c7, 0x71a8, 0x495d, {0x95, 0x46, 0x04, 0x33, 0x0f, 0xb3, 0x3d, 0x76}};
static const GUID IID_IApplicationView3_alt =
    {0x907f6222, 0xdf3d, 0x4794, {0x98, 0x76, 0x1f, 0x42, 0xe5, 0x87, 0xb2, 0xe1}};
static const GUID IID_IApplicationViewTitleBar_alt =
    {0x00924509, 0x5480, 0x4cf8, {0xac, 0x71, 0x71, 0x22, 0xdd, 0x51, 0x8c, 0x77}};

struct application_view_title_bar
{
    IApplicationViewTitleBar IApplicationViewTitleBar_iface;
    LONG ref;

    __FIReference_1_Color *foreground_color;
    __FIReference_1_Color *background_color;
    __FIReference_1_Color *button_foreground_color;
    __FIReference_1_Color *button_background_color;
    __FIReference_1_Color *button_hover_foreground_color;
    __FIReference_1_Color *button_hover_background_color;
    __FIReference_1_Color *button_pressed_foreground_color;
    __FIReference_1_Color *button_pressed_background_color;
    __FIReference_1_Color *inactive_foreground_color;
    __FIReference_1_Color *inactive_background_color;
    __FIReference_1_Color *button_inactive_foreground_color;
    __FIReference_1_Color *button_inactive_background_color;
};

static inline struct application_view_title_bar *impl_from_IApplicationViewTitleBar(IApplicationViewTitleBar *iface)
{
    return CONTAINING_RECORD(iface, struct application_view_title_bar, IApplicationViewTitleBar_iface);
}

static HRESULT WINAPI title_bar_QueryInterface(IApplicationViewTitleBar *iface, REFIID iid, void **out)
{
    struct application_view_title_bar *impl = impl_from_IApplicationViewTitleBar(iface);

    TRACE("iface %p, iid %s, out %p\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IApplicationViewTitleBar) ||
        IsEqualGUID(iid, &IID_IApplicationViewTitleBar_alt))
    {
        IApplicationViewTitleBar_AddRef(&impl->IApplicationViewTitleBar_iface);
        *out = &impl->IApplicationViewTitleBar_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI title_bar_AddRef(IApplicationViewTitleBar *iface)
{
    struct application_view_title_bar *impl = impl_from_IApplicationViewTitleBar(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI title_bar_Release(IApplicationViewTitleBar *iface)
{
    struct application_view_title_bar *impl = impl_from_IApplicationViewTitleBar(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    if (!ref)
    {
        if (impl->foreground_color) __FIReference_1_Color_Release(impl->foreground_color);
        if (impl->background_color) __FIReference_1_Color_Release(impl->background_color);
        if (impl->button_foreground_color) __FIReference_1_Color_Release(impl->button_foreground_color);
        if (impl->button_background_color) __FIReference_1_Color_Release(impl->button_background_color);
        if (impl->button_hover_foreground_color) __FIReference_1_Color_Release(impl->button_hover_foreground_color);
        if (impl->button_hover_background_color) __FIReference_1_Color_Release(impl->button_hover_background_color);
        if (impl->button_pressed_foreground_color) __FIReference_1_Color_Release(impl->button_pressed_foreground_color);
        if (impl->button_pressed_background_color) __FIReference_1_Color_Release(impl->button_pressed_background_color);
        if (impl->inactive_foreground_color) __FIReference_1_Color_Release(impl->inactive_foreground_color);
        if (impl->inactive_background_color) __FIReference_1_Color_Release(impl->inactive_background_color);
        if (impl->button_inactive_foreground_color) __FIReference_1_Color_Release(impl->button_inactive_foreground_color);
        if (impl->button_inactive_background_color) __FIReference_1_Color_Release(impl->button_inactive_background_color);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI title_bar_GetIids(IApplicationViewTitleBar *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT WINAPI title_bar_GetRuntimeClassName(IApplicationViewTitleBar *iface, HSTRING *class_name)
{
    return WindowsCreateString(RuntimeClass_Windows_UI_ViewManagement_ApplicationViewTitleBar,
                               wcslen(RuntimeClass_Windows_UI_ViewManagement_ApplicationViewTitleBar),
                               class_name);
}

static HRESULT WINAPI title_bar_GetTrustLevel(IApplicationViewTitleBar *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p\n", iface, trust_level);
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

#define DEFINE_TITLE_BAR_COLOR_PROP(prop, field) \
static HRESULT WINAPI title_bar_put_##prop(IApplicationViewTitleBar *iface, __FIReference_1_Color *value) \
{ \
    struct application_view_title_bar *impl = impl_from_IApplicationViewTitleBar(iface); \
    TRACE("iface %p, value %p\n", iface, value); \
    if (value) __FIReference_1_Color_AddRef(value); \
    if (impl->field) __FIReference_1_Color_Release(impl->field); \
    impl->field = value; \
    return S_OK; \
} \
static HRESULT WINAPI title_bar_get_##prop(IApplicationViewTitleBar *iface, __FIReference_1_Color **value) \
{ \
    struct application_view_title_bar *impl = impl_from_IApplicationViewTitleBar(iface); \
    TRACE("iface %p, value %p\n", iface, value); \
    if (!value) return E_POINTER; \
    if ((*value = impl->field)) \
        __FIReference_1_Color_AddRef(*value); \
    return S_OK; \
}

DEFINE_TITLE_BAR_COLOR_PROP(ForegroundColor, foreground_color)
DEFINE_TITLE_BAR_COLOR_PROP(BackgroundColor, background_color)
DEFINE_TITLE_BAR_COLOR_PROP(ButtonForegroundColor, button_foreground_color)
DEFINE_TITLE_BAR_COLOR_PROP(ButtonBackgroundColor, button_background_color)
DEFINE_TITLE_BAR_COLOR_PROP(ButtonHoverForegroundColor, button_hover_foreground_color)
DEFINE_TITLE_BAR_COLOR_PROP(ButtonHoverBackgroundColor, button_hover_background_color)
DEFINE_TITLE_BAR_COLOR_PROP(ButtonPressedForegroundColor, button_pressed_foreground_color)
DEFINE_TITLE_BAR_COLOR_PROP(ButtonPressedBackgroundColor, button_pressed_background_color)
DEFINE_TITLE_BAR_COLOR_PROP(InactiveForegroundColor, inactive_foreground_color)
DEFINE_TITLE_BAR_COLOR_PROP(InactiveBackgroundColor, inactive_background_color)
DEFINE_TITLE_BAR_COLOR_PROP(ButtonInactiveForegroundColor, button_inactive_foreground_color)
DEFINE_TITLE_BAR_COLOR_PROP(ButtonInactiveBackgroundColor, button_inactive_background_color)

static const struct IApplicationViewTitleBarVtbl title_bar_vtbl =
{
    title_bar_QueryInterface,
    title_bar_AddRef,
    title_bar_Release,
    /* IInspectable methods */
    title_bar_GetIids,
    title_bar_GetRuntimeClassName,
    title_bar_GetTrustLevel,
    /* IApplicationViewTitleBar methods */
    title_bar_put_ForegroundColor,
    title_bar_get_ForegroundColor,
    title_bar_put_BackgroundColor,
    title_bar_get_BackgroundColor,
    title_bar_put_ButtonForegroundColor,
    title_bar_get_ButtonForegroundColor,
    title_bar_put_ButtonBackgroundColor,
    title_bar_get_ButtonBackgroundColor,
    title_bar_put_ButtonHoverForegroundColor,
    title_bar_get_ButtonHoverForegroundColor,
    title_bar_put_ButtonHoverBackgroundColor,
    title_bar_get_ButtonHoverBackgroundColor,
    title_bar_put_ButtonPressedForegroundColor,
    title_bar_get_ButtonPressedForegroundColor,
    title_bar_put_ButtonPressedBackgroundColor,
    title_bar_get_ButtonPressedBackgroundColor,
    title_bar_put_InactiveForegroundColor,
    title_bar_get_InactiveForegroundColor,
    title_bar_put_InactiveBackgroundColor,
    title_bar_get_InactiveBackgroundColor,
    title_bar_put_ButtonInactiveForegroundColor,
    title_bar_get_ButtonInactiveForegroundColor,
    title_bar_put_ButtonInactiveBackgroundColor,
    title_bar_get_ButtonInactiveBackgroundColor,
};

static HRESULT application_view_title_bar_create(struct application_view_title_bar **out)
{
    struct application_view_title_bar *impl;

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IApplicationViewTitleBar_iface.lpVtbl = &title_bar_vtbl;
    impl->ref = 1;
    *out = impl;
    return S_OK;
}

struct application_view
{
    IApplicationView IApplicationView_iface;
    IApplicationView2 IApplicationView2_iface;
    IApplicationView3 IApplicationView3_iface;
    LONG ref;

    INT32 id;
    HSTRING title;
    boolean is_fullscreen;
    boolean suppress_system_overlays;
    ApplicationViewBoundsMode desired_bounds_mode;
    FullScreenSystemOverlayMode full_screen_system_overlay_mode;
    struct application_view_title_bar *title_bar;
};

static const struct IApplicationViewVtbl application_view_vtbl;
static const struct IApplicationView2Vtbl application_view2_vtbl;
static const struct IApplicationView3Vtbl application_view3_vtbl;

static inline struct application_view *impl_from_IApplicationView(IApplicationView *iface)
{
    return CONTAINING_RECORD(iface, struct application_view, IApplicationView_iface);
}

DEFINE_IINSPECTABLE(application_view2, IApplicationView2, struct application_view, IApplicationView_iface)
DEFINE_IINSPECTABLE(application_view3, IApplicationView3, struct application_view, IApplicationView_iface)

static __thread struct application_view *thread_application_view;

static HRESULT WINAPI application_view_QueryInterface(IApplicationView *iface, REFIID iid, void **out)
{
    struct application_view *impl = impl_from_IApplicationView(iface);

    TRACE("iface %p, iid %s, out %p\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_IApplicationView) ||
        IsEqualGUID(iid, &IID_IApplicationView_alt))
    {
        IApplicationView_AddRef(&impl->IApplicationView_iface);
        *out = &impl->IApplicationView_iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_IApplicationView2))
    {
        IApplicationView2_AddRef(&impl->IApplicationView2_iface);
        *out = &impl->IApplicationView2_iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_IApplicationView3) ||
             IsEqualGUID(iid, &IID_IApplicationView3_alt))
    {
        IApplicationView3_AddRef(&impl->IApplicationView3_iface);
        *out = &impl->IApplicationView3_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI application_view_AddRef(IApplicationView *iface)
{
    struct application_view *impl = impl_from_IApplicationView(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI application_view_Release(IApplicationView *iface)
{
    struct application_view *impl = impl_from_IApplicationView(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    if (!ref)
    {
        if (thread_application_view == impl)
            thread_application_view = NULL;
        if (impl->title_bar)
            IApplicationViewTitleBar_Release(&impl->title_bar->IApplicationViewTitleBar_iface);
        WindowsDeleteString(impl->title);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI application_view_GetIids(IApplicationView *iface, ULONG *iid_count, IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT WINAPI application_view_GetRuntimeClassName(IApplicationView *iface, HSTRING *class_name)
{
    return WindowsCreateString(RuntimeClass_Windows_UI_ViewManagement_ApplicationView,
                               wcslen(RuntimeClass_Windows_UI_ViewManagement_ApplicationView),
                               class_name);
}

static HRESULT WINAPI application_view_GetTrustLevel(IApplicationView *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p\n", iface, trust_level);
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI application_view_get_Orientation(IApplicationView *iface, ApplicationViewOrientation *value)
{
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = ApplicationViewOrientation_Landscape;
    return S_OK;
}

static HRESULT WINAPI application_view_get_AdjacentToLeftDisplayEdge(IApplicationView *iface, boolean *value)
{
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI application_view_get_AdjacentToRightDisplayEdge(IApplicationView *iface, boolean *value)
{
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI application_view_get_IsFullScreen(IApplicationView *iface, boolean *value)
{
    struct application_view *impl = impl_from_IApplicationView(iface);
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = impl->is_fullscreen;
    return S_OK;
}

static HRESULT WINAPI application_view_get_IsOnLockScreen(IApplicationView *iface, boolean *value)
{
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI application_view_get_IsScreenCaptureEnabled(IApplicationView *iface, boolean *value)
{
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI application_view_put_IsScreenCaptureEnabled(IApplicationView *iface, boolean value)
{
    TRACE("iface %p, value %d\n", iface, value);
    return S_OK;
}

static HRESULT WINAPI application_view_put_Title(IApplicationView *iface, HSTRING value)
{
    struct application_view *impl = impl_from_IApplicationView(iface);
    TRACE("iface %p, value %p\n", iface, value);
    WindowsDeleteString(impl->title);
    return WindowsDuplicateString(value, &impl->title);
}

static HRESULT WINAPI application_view_get_Title(IApplicationView *iface, HSTRING *value)
{
    struct application_view *impl = impl_from_IApplicationView(iface);
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    if (!impl->title) return WindowsCreateString(NULL, 0, value);
    return WindowsDuplicateString(impl->title, value);
}

static HRESULT WINAPI application_view_get_Id(IApplicationView *iface, INT32 *value)
{
    struct application_view *impl = impl_from_IApplicationView(iface);
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = impl->id;
    return S_OK;
}

static HRESULT WINAPI application_view_add_Consolidated(IApplicationView *iface,
    __FITypedEventHandler_2_Windows__CUI__CViewManagement__CApplicationView_Windows__CUI__CViewManagement__CApplicationViewConsolidatedEventArgs *handler,
    EventRegistrationToken *token)
{
    TRACE("iface %p, handler %p, token %p\n", iface, handler, token);
    if (!token) return E_POINTER;
    token->value = 1;
    return S_OK;
}

static HRESULT WINAPI application_view_remove_Consolidated(IApplicationView *iface, EventRegistrationToken token)
{
    TRACE("iface %p, token %#I64x\n", iface, token.value);
    return S_OK;
}

static const struct IApplicationViewVtbl application_view_vtbl =
{
    application_view_QueryInterface,
    application_view_AddRef,
    application_view_Release,
    /* IInspectable methods */
    application_view_GetIids,
    application_view_GetRuntimeClassName,
    application_view_GetTrustLevel,
    /* IApplicationView methods */
    application_view_get_Orientation,
    application_view_get_AdjacentToLeftDisplayEdge,
    application_view_get_AdjacentToRightDisplayEdge,
    application_view_get_IsFullScreen,
    application_view_get_IsOnLockScreen,
    application_view_get_IsScreenCaptureEnabled,
    application_view_put_IsScreenCaptureEnabled,
    application_view_put_Title,
    application_view_get_Title,
    application_view_get_Id,
    application_view_add_Consolidated,
    application_view_remove_Consolidated,
};

static HRESULT WINAPI application_view2_get_SuppressSystemOverlays(IApplicationView2 *iface, boolean *value)
{
    struct application_view *impl = impl_from_IApplicationView2(iface);
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = impl->suppress_system_overlays;
    return S_OK;
}

static HRESULT WINAPI application_view2_put_SuppressSystemOverlays(IApplicationView2 *iface, boolean value)
{
    struct application_view *impl = impl_from_IApplicationView2(iface);
    TRACE("iface %p, value %d\n", iface, value);
    impl->suppress_system_overlays = value;
    return S_OK;
}

static HRESULT WINAPI application_view2_get_VisibleBounds(IApplicationView2 *iface, Rect *value)
{
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    value->X = 0;
    value->Y = 0;
    value->Width = 1024;
    value->Height = 768;
    return S_OK;
}

static HRESULT WINAPI application_view2_add_VisibleBoundsChanged(IApplicationView2 *iface,
    __FITypedEventHandler_2_Windows__CUI__CViewManagement__CApplicationView_IInspectable *handler,
    EventRegistrationToken *token)
{
    TRACE("iface %p, handler %p, token %p\n", iface, handler, token);
    if (!token) return E_POINTER;
    token->value = 1;
    return S_OK;
}

static HRESULT WINAPI application_view2_remove_VisibleBoundsChanged(IApplicationView2 *iface, EventRegistrationToken token)
{
    TRACE("iface %p, token %#I64x\n", iface, token.value);
    return S_OK;
}

static HRESULT WINAPI application_view2_SetDesiredBoundsMode(IApplicationView2 *iface, ApplicationViewBoundsMode mode, boolean *success)
{
    struct application_view *impl = impl_from_IApplicationView2(iface);
    TRACE("iface %p, mode %d, success %p\n", iface, mode, success);
    impl->desired_bounds_mode = mode;
    if (success) *success = TRUE;
    return S_OK;
}

static HRESULT WINAPI application_view2_get_DesiredBoundsMode(IApplicationView2 *iface, ApplicationViewBoundsMode *value)
{
    struct application_view *impl = impl_from_IApplicationView2(iface);
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = impl->desired_bounds_mode;
    return S_OK;
}

static const struct IApplicationView2Vtbl application_view2_vtbl =
{
    application_view2_QueryInterface,
    application_view2_AddRef,
    application_view2_Release,
    /* IInspectable methods */
    application_view2_GetIids,
    application_view2_GetRuntimeClassName,
    application_view2_GetTrustLevel,
    /* IApplicationView2 methods */
    application_view2_get_SuppressSystemOverlays,
    application_view2_put_SuppressSystemOverlays,
    application_view2_get_VisibleBounds,
    application_view2_add_VisibleBoundsChanged,
    application_view2_remove_VisibleBoundsChanged,
    application_view2_SetDesiredBoundsMode,
    application_view2_get_DesiredBoundsMode,
};

static HRESULT WINAPI application_view3_get_TitleBar(IApplicationView3 *iface, IApplicationViewTitleBar **value)
{
    struct application_view *impl = impl_from_IApplicationView3(iface);
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    if (!impl->title_bar)
    {
        HRESULT hr = application_view_title_bar_create(&impl->title_bar);
        if (FAILED(hr)) return hr;
    }
    *value = &impl->title_bar->IApplicationViewTitleBar_iface;
    IApplicationViewTitleBar_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI application_view3_get_FullScreenSystemOverlayMode(IApplicationView3 *iface, FullScreenSystemOverlayMode *value)
{
    struct application_view *impl = impl_from_IApplicationView3(iface);
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = impl->full_screen_system_overlay_mode;
    return S_OK;
}

static HRESULT WINAPI application_view3_put_FullScreenSystemOverlayMode(IApplicationView3 *iface, FullScreenSystemOverlayMode value)
{
    struct application_view *impl = impl_from_IApplicationView3(iface);
    TRACE("iface %p, value %d\n", iface, value);
    impl->full_screen_system_overlay_mode = value;
    return S_OK;
}

static HRESULT WINAPI application_view3_get_IsFullScreenMode(IApplicationView3 *iface, boolean *value)
{
    struct application_view *impl = impl_from_IApplicationView3(iface);
    TRACE("iface %p, value %p\n", iface, value);
    if (!value) return E_POINTER;
    *value = impl->is_fullscreen;
    return S_OK;
}

static HRESULT WINAPI application_view3_TryEnterFullScreenMode(IApplicationView3 *iface, boolean *success)
{
    struct application_view *impl = impl_from_IApplicationView3(iface);
    TRACE("iface %p, success %p\n", iface, success);
    impl->is_fullscreen = TRUE;
    if (success) *success = TRUE;
    return S_OK;
}

static HRESULT WINAPI application_view3_ExitFullScreenMode(IApplicationView3 *iface)
{
    struct application_view *impl = impl_from_IApplicationView3(iface);
    TRACE("iface %p\n", iface);
    impl->is_fullscreen = FALSE;
    return S_OK;
}

static HRESULT WINAPI application_view3_ShowStandardSystemOverlays(IApplicationView3 *iface)
{
    TRACE("iface %p\n", iface);
    return S_OK;
}

static HRESULT WINAPI application_view3_TryResizeView(IApplicationView3 *iface, Size value, boolean *success)
{
    TRACE("iface %p, width %f, height %f, success %p\n", iface, value.Width, value.Height, success);
    if (success) *success = TRUE;
    return S_OK;
}

static HRESULT WINAPI application_view3_SetPreferredMinSize(IApplicationView3 *iface, Size size)
{
    TRACE("iface %p, width %f, height %f\n", iface, size.Width, size.Height);
    return S_OK;
}

static const struct IApplicationView3Vtbl application_view3_vtbl =
{
    application_view3_QueryInterface,
    application_view3_AddRef,
    application_view3_Release,
    /* IInspectable methods */
    application_view3_GetIids,
    application_view3_GetRuntimeClassName,
    application_view3_GetTrustLevel,
    /* IApplicationView3 methods */
    application_view3_get_TitleBar,
    application_view3_get_FullScreenSystemOverlayMode,
    application_view3_put_FullScreenSystemOverlayMode,
    application_view3_get_IsFullScreenMode,
    application_view3_TryEnterFullScreenMode,
    application_view3_ExitFullScreenMode,
    application_view3_ShowStandardSystemOverlays,
    application_view3_TryResizeView,
    application_view3_SetPreferredMinSize,
};

static HRESULT application_view_create(struct application_view **out)
{
    struct application_view *impl;

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IApplicationView_iface.lpVtbl = &application_view_vtbl;
    impl->IApplicationView2_iface.lpVtbl = &application_view2_vtbl;
    impl->IApplicationView3_iface.lpVtbl = &application_view3_vtbl;
    impl->ref = 1;
    impl->id = 1;
    *out = impl;
    return S_OK;
}

struct factory
{
    IActivationFactory IActivationFactory_iface;
    IApplicationViewStatics IApplicationViewStatics_iface;
    IApplicationViewStatics2 IApplicationViewStatics2_iface;
    LONG ref;
};

static inline struct factory *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct factory, IActivationFactory_iface);
}

static HRESULT WINAPI activation_factory_QueryInterface(IActivationFactory *iface, REFIID iid,
                                                        void **out)
{
    struct factory *impl = impl_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown)
        || IsEqualGUID(iid, &IID_IInspectable)
        || IsEqualGUID(iid, &IID_IAgileObject)
        || IsEqualGUID(iid, &IID_IActivationFactory))
    {
        IActivationFactory_AddRef(&impl->IActivationFactory_iface);
        *out = &impl->IActivationFactory_iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_IApplicationViewStatics))
    {
        IApplicationViewStatics_AddRef(&impl->IApplicationViewStatics_iface);
        *out = &impl->IApplicationViewStatics_iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_IApplicationViewStatics2))
    {
        IApplicationViewStatics2_AddRef(&impl->IApplicationViewStatics2_iface);
        *out = &impl->IApplicationViewStatics2_iface;
        return S_OK;
    }

    FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI activation_factory_AddRef(IActivationFactory *iface)
{
    struct factory *impl = impl_from_IActivationFactory(iface);
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static ULONG WINAPI activation_factory_Release(IActivationFactory *iface)
{
    struct factory *impl = impl_from_IActivationFactory(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    TRACE("iface %p, ref %lu.\n", iface, ref);
    return ref;
}

static HRESULT WINAPI activation_factory_GetIids(IActivationFactory *iface, ULONG *iid_count,
                                                 IID **iids)
{
    FIXME("iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids);
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_GetRuntimeClassName(IActivationFactory *iface,
                                                             HSTRING *class_name)
{
    FIXME("iface %p, class_name %p stub!\n", iface, class_name);
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_GetTrustLevel(IActivationFactory *iface,
                                                       TrustLevel *trust_level)
{
    FIXME("iface %p, trust_level %p stub!\n", iface, trust_level);
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_ActivateInstance(IActivationFactory *iface,
                                                          IInspectable **instance)
{
    FIXME("iface %p, instance %p stub!\n", iface, instance);
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl activation_factory_vtbl =
{
    activation_factory_QueryInterface,
    activation_factory_AddRef,
    activation_factory_Release,
    /* IInspectable methods */
    activation_factory_GetIids,
    activation_factory_GetRuntimeClassName,
    activation_factory_GetTrustLevel,
    /* IActivationFactory methods */
    activation_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE(statics, IApplicationViewStatics, struct factory, IActivationFactory_iface)

static HRESULT WINAPI statics_Value(IApplicationViewStatics *iface, ApplicationViewState *value)
{
    FIXME("iface %p, value %p stub!\n", iface, value);
    return E_NOTIMPL;
}

static HRESULT WINAPI statics_TryUnsnap(IApplicationViewStatics *iface, boolean *success)
{
    FIXME("iface %p, success %p stub!\n", iface, success);
    return E_NOTIMPL;
}

static const struct IApplicationViewStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    /* IInspectable methods */
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    /* IApplicationViewStatics methods */
    statics_Value,
    statics_TryUnsnap,
};

DEFINE_IINSPECTABLE(statics2, IApplicationViewStatics2, struct factory, IActivationFactory_iface)

static HRESULT WINAPI statics2_GetForCurrentView(IApplicationViewStatics2 *iface, IApplicationView **current)
{
    TRACE("iface %p, current %p\n", iface, current);

    if (!current) return E_POINTER;

    if (!thread_application_view)
    {
        HRESULT hr = application_view_create(&thread_application_view);
        if (FAILED(hr)) return hr;
    }

    *current = &thread_application_view->IApplicationView_iface;
    IApplicationView_AddRef(*current);
    return S_OK;
}

static HRESULT WINAPI statics2_get_TerminateAppOnFinalViewClose(IApplicationViewStatics2 *iface, boolean *value)
{
    FIXME("iface %p, value %p stub!\n", iface, value);
    return E_NOTIMPL;
}

static HRESULT WINAPI statics2_put_TerminateAppOnFinalViewClose(IApplicationViewStatics2 *iface, boolean value)
{
    FIXME("iface %p, value %d stub!\n", iface, value);
    return E_NOTIMPL;
}

static const struct IApplicationViewStatics2Vtbl statics2_vtbl =
{
    statics2_QueryInterface,
    statics2_AddRef,
    statics2_Release,
    /* IInspectable methods */
    statics2_GetIids,
    statics2_GetRuntimeClassName,
    statics2_GetTrustLevel,
    /* IApplicationViewStatics2 methods */
    statics2_GetForCurrentView,
    statics2_get_TerminateAppOnFinalViewClose,
    statics2_put_TerminateAppOnFinalViewClose
};

static struct factory factory =
{
    {&activation_factory_vtbl},
    {&statics_vtbl},
    {&statics2_vtbl},
    1,
};

IActivationFactory *application_view_factory = &factory.IActivationFactory_iface;
