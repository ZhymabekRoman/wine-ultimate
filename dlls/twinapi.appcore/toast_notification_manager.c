/* WinRT Windows.UI.Notifications.ToastNotificationManager implementation
 *
 * The tray/toast path of packaged apps activates
 * Windows.UI.Notifications.ToastNotificationManager (on Windows the class is
 * served by twinapi.appcore.dll).  Wine answers it with a functional no-op
 * toast surface: the statics hand out ToastNotifier instances whose
 * Show/Hide/schedule entry points succeed; template content is not produced
 * (E_NOTIMPL), the honest stub marker for callers that need the XML.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"

#include <shellapi.h>

#include "wine/debug.h"
#include "wine/list.h"

#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);
WINE_DECLARE_DEBUG_CHANNEL(notify);
#define NOTIFY_TRACE(...) WINE_TRACE_(notify)(__VA_ARGS__)

/* IInspectable forwarder for the secondary interfaces of an object: identity,
 * refcount and the IInspectable entry points live on the object's canonical
 * interface, as the WinRT projection requires. */
#define FORWARD_IINSPECTABLE( pfx, iface_type, impl_type, impl_from, base_expr )                     \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )          \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_QueryInterface( (IInspectable *)(base_expr), iid, out );                 \
    }                                                                                                \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                            \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_AddRef( (IInspectable *)(base_expr) );                                   \
    }                                                                                                \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                           \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_Release( (IInspectable *)(base_expr) );                                  \
    }                                                                                                \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )           \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_GetIids( (IInspectable *)(base_expr), iid_count, iids );                  \
    }                                                                                                \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )        \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(base_expr), class_name );           \
    }                                                                                                \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )          \
    {                                                                                                \
        impl_type *impl = impl_from( iface );                                                        \
        return IInspectable_GetTrustLevel( (IInspectable *)(base_expr), trust_level );                \
    }

/* IInspectable::GetIids over a fixed list of the interfaces the object
 * answers for in QueryInterface. The caller frees the array with CoTaskMemFree. */
static HRESULT notifications_get_iids( const IID *const *iids, ULONG count, ULONG *iid_count, IID **out )
{
    IID *arr;
    ULONG i;

    if (!count)
    {
        *iid_count = 0;
        *out = NULL;
        return S_OK;
    }
    if (!(arr = CoTaskMemAlloc( count * sizeof(*arr) ))) return E_OUTOFMEMORY;
    for (i = 0; i < count; ++i) arr[i] = *iids[i];
    *iid_count = count;
    *out = arr;
    return S_OK;
}

/*
 * Template documents served by ToastNotificationManager::GetTemplateContent.
 * The layout matches the Windows templates (toast/visual/binding, numbered
 * text elements, image for the ToastImageAndText* family) so an app that
 * fills the returned document through IXmlCharacterData put_Data round-trips
 * unchanged; toast_notifier_Show() reads the same element layout back out to
 * build the FreeDesktop Notifications summary/body/icon fields.
 */
/* Index order follows the tree's ToastTemplateType enum:
 * ToastImageAndText01..04 (0..3), ToastText01..04 (4..7). */
static const WCHAR *const toast_template_xml[] =
{
    L"<toast><visual><binding template=\"ToastImageAndText01\"><image id=\"1\" src=\"\" alt=\"\"/>"
        L"<text id=\"2\">Body text</text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastImageAndText02\"><image id=\"1\" src=\"\" alt=\"\"/>"
        L"<text id=\"2\">Title</text><text id=\"3\">Body text</text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastImageAndText03\"><image id=\"1\" src=\"\" alt=\"\"/>"
        L"<text id=\"2\">Title</text><text id=\"3\">Body text</text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastImageAndText04\"><image id=\"1\" src=\"\" alt=\"\"/>"
        L"<text id=\"2\">Title</text><text id=\"3\">Body text</text><text id=\"4\">Body text</text>"
        L"</binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastText01\"><text id=\"1\">Body text</text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastText02\"><text id=\"1\">Title</text>"
        L"<text id=\"2\">Body text</text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastText03\"><text id=\"1\">Title</text>"
        L"<text id=\"2\">Body text</text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastText04\"><text id=\"1\">Title</text>"
        L"<text id=\"2\">Body text</text><text id=\"3\">Body text</text></binding></visual></toast>",
};

/*
 * Event sinks: the Activated/Dismissed/Failed handlers an app registers on a
 * ToastNotification are kept (token-keyed, AddRef'd) and replayed when the
 * notification daemon reports the matching ActionInvoked/NotificationClosed
 * signal for the id the toast was shown under.
 */
struct toast_event_sink
{
    struct list entry;
    EventRegistrationToken token;
    IUnknown *handler;
};

static CRITICAL_SECTION toast_state_cs;
static CRITICAL_SECTION_DEBUG toast_state_cs_debug =
{
    0, 0, &toast_state_cs,
    { &toast_state_cs_debug.ProcessLocksList, &toast_state_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": toast_state_cs") }
};
static CRITICAL_SECTION toast_state_cs = { &toast_state_cs_debug, -1, 0, 0, 0, 0 };

static LONG toast_event_token;

static HRESULT toast_event_add( struct list *sinks, IUnknown *handler, EventRegistrationToken *token )
{
    struct toast_event_sink *sink;

    if (!handler || !token) return E_POINTER;
    if (!(sink = malloc( sizeof(*sink) ))) return E_OUTOFMEMORY;

    sink->token.value = (ULONGLONG)InterlockedIncrement( &toast_event_token );
    sink->handler = handler;
    IUnknown_AddRef( handler );
    EnterCriticalSection( &toast_state_cs );
    list_add_tail( sinks, &sink->entry );
    LeaveCriticalSection( &toast_state_cs );
    *token = sink->token;
    return S_OK;
}

static HRESULT toast_event_remove( struct list *sinks, EventRegistrationToken token )
{
    struct toast_event_sink *sink, *next;
    IUnknown *to_release = NULL;

    EnterCriticalSection( &toast_state_cs );
    LIST_FOR_EACH_ENTRY_SAFE( sink, next, sinks, struct toast_event_sink, entry )
    {
        if (sink->token.value != token.value) continue;
        list_remove( &sink->entry );
        to_release = sink->handler;
        free( sink );
        break;
    }
    LeaveCriticalSection( &toast_state_cs );
    if (to_release) IUnknown_Release( to_release );
    return S_OK;
}

static void toast_event_clear( struct list *sinks )
{
    struct list detached = LIST_INIT( detached );
    struct toast_event_sink *sink, *next;

    EnterCriticalSection( &toast_state_cs );
    LIST_FOR_EACH_ENTRY_SAFE( sink, next, sinks, struct toast_event_sink, entry )
    {
        list_remove( &sink->entry );
        list_add_tail( &detached, &sink->entry );
    }
    LeaveCriticalSection( &toast_state_cs );

    LIST_FOR_EACH_ENTRY_SAFE( sink, next, &detached, struct toast_event_sink, entry )
    {
        list_remove( &sink->entry );
        IUnknown_Release( sink->handler );
        free( sink );
    }
}

/*
 * Windows.UI.Notifications.ToastActivatedEventArgs: handed to the Activated
 * handlers with get_Arguments() answering the toast document's launch
 * attribute, as the Windows delivery path reports it.
 */
struct toast_activated_args
{
    __x_ABI_CWindows_CUI_CNotifications_CIToastActivatedEventArgs IToastActivatedEventArgs_iface;
    HSTRING args;
    LONG ref;
};

static inline struct toast_activated_args *impl_from_IToastActivatedEventArgs(
        __x_ABI_CWindows_CUI_CNotifications_CIToastActivatedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct toast_activated_args, IToastActivatedEventArgs_iface );
}

static HRESULT WINAPI toast_activated_args_QueryInterface(
        __x_ABI_CWindows_CUI_CNotifications_CIToastActivatedEventArgs *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IToastActivatedEventArgs ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_activated_args_AddRef(
        __x_ABI_CWindows_CUI_CNotifications_CIToastActivatedEventArgs *iface )
{
    struct toast_activated_args *impl = impl_from_IToastActivatedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_activated_args_Release(
        __x_ABI_CWindows_CUI_CNotifications_CIToastActivatedEventArgs *iface )
{
    struct toast_activated_args *impl = impl_from_IToastActivatedEventArgs( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->args );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI toast_activated_args_GetIids(
        __x_ABI_CWindows_CUI_CNotifications_CIToastActivatedEventArgs *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI toast_activated_args_GetRuntimeClassName(
        __x_ABI_CWindows_CUI_CNotifications_CIToastActivatedEventArgs *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastActivatedEventArgs,
            ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastActivatedEventArgs ) - 1, class_name );
}

static HRESULT WINAPI toast_activated_args_GetTrustLevel(
        __x_ABI_CWindows_CUI_CNotifications_CIToastActivatedEventArgs *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI toast_activated_args_get_Arguments(
        __x_ABI_CWindows_CUI_CNotifications_CIToastActivatedEventArgs *iface, HSTRING *value )
{
    struct toast_activated_args *impl = impl_from_IToastActivatedEventArgs( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->args, value );
}

static const struct IToastActivatedEventArgsVtbl toast_activated_args_vtbl =
{
    toast_activated_args_QueryInterface,
    toast_activated_args_AddRef,
    toast_activated_args_Release,
    /* IInspectable methods */
    toast_activated_args_GetIids,
    toast_activated_args_GetRuntimeClassName,
    toast_activated_args_GetTrustLevel,
    /* IToastActivatedEventArgs methods */
    toast_activated_args_get_Arguments,
};

static HRESULT create_toast_activated_args( HSTRING args, struct toast_activated_args **out )
{
    struct toast_activated_args *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IToastActivatedEventArgs_iface.lpVtbl = &toast_activated_args_vtbl;
    impl->ref = 1;
    if (args && FAILED( hr = WindowsDuplicateString( args, &impl->args ) ))
    {
        free( impl );
        return hr;
    }
    *out = impl;
    return S_OK;
}

/*
 * Windows.UI.Notifications.ToastDismissedEventArgs: carries the dismissal
 * reason for the Dismissed handlers.
 */
struct toast_dismissed_args
{
    __x_ABI_CWindows_CUI_CNotifications_CIToastDismissedEventArgs IToastDismissedEventArgs_iface;
    __x_ABI_CWindows_CUI_CNotifications_CToastDismissalReason reason;
    LONG ref;
};

static inline struct toast_dismissed_args *impl_from_IToastDismissedEventArgs(
        __x_ABI_CWindows_CUI_CNotifications_CIToastDismissedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct toast_dismissed_args, IToastDismissedEventArgs_iface );
}

static HRESULT WINAPI toast_dismissed_args_QueryInterface(
        __x_ABI_CWindows_CUI_CNotifications_CIToastDismissedEventArgs *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IToastDismissedEventArgs ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_dismissed_args_AddRef(
        __x_ABI_CWindows_CUI_CNotifications_CIToastDismissedEventArgs *iface )
{
    struct toast_dismissed_args *impl = impl_from_IToastDismissedEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_dismissed_args_Release(
        __x_ABI_CWindows_CUI_CNotifications_CIToastDismissedEventArgs *iface )
{
    struct toast_dismissed_args *impl = impl_from_IToastDismissedEventArgs( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI toast_dismissed_args_GetIids(
        __x_ABI_CWindows_CUI_CNotifications_CIToastDismissedEventArgs *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI toast_dismissed_args_GetRuntimeClassName(
        __x_ABI_CWindows_CUI_CNotifications_CIToastDismissedEventArgs *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastDismissedEventArgs,
            ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastDismissedEventArgs ) - 1, class_name );
}

static HRESULT WINAPI toast_dismissed_args_GetTrustLevel(
        __x_ABI_CWindows_CUI_CNotifications_CIToastDismissedEventArgs *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI toast_dismissed_args_get_Reason(
        __x_ABI_CWindows_CUI_CNotifications_CIToastDismissedEventArgs *iface,
        __x_ABI_CWindows_CUI_CNotifications_CToastDismissalReason *value )
{
    struct toast_dismissed_args *impl = impl_from_IToastDismissedEventArgs( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->reason;
    return S_OK;
}

static const struct IToastDismissedEventArgsVtbl toast_dismissed_args_vtbl =
{
    toast_dismissed_args_QueryInterface,
    toast_dismissed_args_AddRef,
    toast_dismissed_args_Release,
    /* IInspectable methods */
    toast_dismissed_args_GetIids,
    toast_dismissed_args_GetRuntimeClassName,
    toast_dismissed_args_GetTrustLevel,
    /* IToastDismissedEventArgs methods */
    toast_dismissed_args_get_Reason,
};

static HRESULT create_toast_dismissed_args( __x_ABI_CWindows_CUI_CNotifications_CToastDismissalReason reason,
        struct toast_dismissed_args **out )
{
    struct toast_dismissed_args *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IToastDismissedEventArgs_iface.lpVtbl = &toast_dismissed_args_vtbl;
    impl->ref = 1;
    impl->reason = reason;
    *out = impl;
    return S_OK;
}

/*
 * XML payload helpers shared with badge_update_manager.c: read the toast (or
 * badge) document the app built and pull out the fields the FreeDesktop
 * Notify() call is assembled from.
 */
HRESULT toast_create_xml_document( const WCHAR *xml, __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument **out )
{
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *doc;
    HSTRING class_name = NULL, hstr = NULL;
    IInspectable *obj;
    HRESULT hr;

    if (!out) return E_POINTER;
    if (FAILED( hr = WindowsCreateString( RuntimeClass_Windows_Data_Xml_Dom_XmlDocument,
            ARRAY_SIZE( RuntimeClass_Windows_Data_Xml_Dom_XmlDocument ) - 1, &class_name ) ))
        return hr;
    if (FAILED( hr = RoActivateInstance( class_name, &obj ) ))
    {
        WindowsDeleteString( class_name );
        return hr;
    }
    WindowsDeleteString( class_name );
    hr = IInspectable_QueryInterface( obj, &IID_IXmlDocument, (void **)&doc );
    IInspectable_Release( obj );
    if (FAILED( hr )) return hr;

    if (SUCCEEDED( hr = WindowsCreateString( xml, lstrlenW( xml ), &hstr ) ))
    {
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocumentIO *io;

        hr = IInspectable_QueryInterface( (IInspectable *)doc, &IID_IXmlDocumentIO, (void **)&io );
        if (SUCCEEDED( hr ))
        {
            hr = IXmlDocumentIO_LoadXml( io, hstr );
            IInspectable_Release( (IInspectable *)io );
        }
        WindowsDeleteString( hstr );
    }
    if (FAILED( hr )) IInspectable_Release( (IInspectable *)doc );
    else *out = doc;
    return hr;
}

HRESULT toast_xml_first_attribute( __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *doc,
        const WCHAR *tag, const WCHAR *name, HSTRING *value )
{
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlElement *element = NULL;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlNodeList *list = NULL;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlNode *node = NULL;
    HSTRING tag_name = NULL, attr_name = NULL;
    UINT32 length = 0;
    HRESULT hr;

    *value = NULL;
    if (FAILED( hr = WindowsCreateString( tag, lstrlenW( tag ), &tag_name ) )) return hr;
    if (SUCCEEDED( hr = WindowsCreateString( name, lstrlenW( name ), &attr_name ) ))
        hr = IXmlDocument_GetElementsByTagName( doc, tag_name, &list );
    if (SUCCEEDED( hr )) hr = IXmlNodeList_get_Length( list, &length );
    if (SUCCEEDED( hr ) && length) hr = IXmlNodeList_Item( list, 0, &node );
    if (SUCCEEDED( hr ) && node)
        hr = IInspectable_QueryInterface( (IInspectable *)node, &IID_IXmlElement, (void **)&element );
    if (SUCCEEDED( hr ) && element) hr = IXmlElement_GetAttribute( element, attr_name, value );

    if (element) IInspectable_Release( (IInspectable *)element );
    if (node) IInspectable_Release( (IInspectable *)node );
    if (list) IInspectable_Release( (IInspectable *)list );
    WindowsDeleteString( attr_name );
    WindowsDeleteString( tag_name );
    return hr;
}

void toast_payload_free( struct toast_payload *payload )
{
    WindowsDeleteString( payload->summary );
    WindowsDeleteString( payload->body );
    WindowsDeleteString( payload->launch );
    WindowsDeleteString( payload->image );
    memset( payload, 0, sizeof(*payload) );
}

static HRESULT toast_collect_texts( __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *doc,
        HSTRING **out, UINT32 *count )
{
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlNodeList *list = NULL;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlCharacterData *text = NULL;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlNode *node = NULL;
    HSTRING tag_name = NULL;
    HSTRING *texts = NULL;
    UINT32 i, length = 0;
    HRESULT hr;

    *out = NULL;
    *count = 0;
    if (FAILED( hr = WindowsCreateString( L"text", 4, &tag_name ) )) return hr;
    if (SUCCEEDED( hr = IXmlDocument_GetElementsByTagName( doc, tag_name, &list ) ))
        hr = IXmlNodeList_get_Length( list, &length );
    if (SUCCEEDED( hr ) && length && !(texts = malloc( length * sizeof(*texts) )))
        hr = E_OUTOFMEMORY;

    for (i = 0; SUCCEEDED( hr ) && i < length; ++i)
    {
        texts[i] = NULL;
        if (SUCCEEDED( hr = IXmlNodeList_Item( list, i, &node ) ))
            hr = IInspectable_QueryInterface( (IInspectable *)node, &IID_IXmlCharacterData, (void **)&text );
        if (SUCCEEDED( hr )) hr = IXmlCharacterData_get_Data( text, &texts[i] );
        if (text)
        {
            IInspectable_Release( (IInspectable *)text );
            text = NULL;
        }
        /* msxml6's DOM nodes answer IXmlCharacterData only for text nodes; an
         * element's content is read through the serializer's InnerText. */
        if (hr == E_NOINTERFACE)
        {
            __x_ABI_CWindows_CData_CXml_CDom_CIXmlNodeSerializer *serializer = NULL;
            hr = IInspectable_QueryInterface( (IInspectable *)node, &IID_IXmlNodeSerializer, (void **)&serializer );
            if (SUCCEEDED( hr )) hr = IXmlNodeSerializer_get_InnerText( serializer, &texts[i] );
            if (serializer) IInspectable_Release( (IInspectable *)serializer );
        }
        if (node)
        {
            IInspectable_Release( (IInspectable *)node );
            node = NULL;
        }
    }
    if (SUCCEEDED( hr ))
    {
        *out = texts;
        *count = length;
    }
    else
    {
        for (i = 0; texts && i < length; ++i) WindowsDeleteString( texts[i] );
        free( texts );
    }
    IInspectable_Release( (IInspectable *)list );
    WindowsDeleteString( tag_name );
    return hr;
}

static HRESULT toast_join_texts( const HSTRING *texts, UINT32 first, UINT32 count, HSTRING *out )
{
    WCHAR *buffer, *p;
    UINT32 i, len = 0;
    const WCHAR *raw;
    HRESULT hr;

    *out = NULL;
    for (i = first; i < count; ++i) len += WindowsGetStringLen( texts[i] );
    len += count - first - 1; /* spaces between lines */
    if (!(buffer = malloc( (len + 1) * sizeof(*buffer) ))) return E_OUTOFMEMORY;

    p = buffer;
    for (i = first; i < count; ++i)
    {
        UINT32 size;
        if (i > first) *p++ = ' ';
        raw = WindowsGetStringRawBuffer( texts[i], &size );
        memcpy( p, raw, size * sizeof(*p) );
        p += size;
    }
    *p = 0;
    hr = WindowsCreateString( buffer, len, out );
    free( buffer );
    return hr;
}

/*
 * Tray (Shell_NotifyIcon) delivery reads the notification back as a flat
 * title + body pair: the first non-empty <text> element is the balloon title,
 * the remaining ones join into the body. A document with a single <text> has
 * no title to show, so the generic "Notification" app label stands in.
 */
static void toast_copy_text( WCHAR *dst, size_t dst_len, const WCHAR *src, size_t len )
{
    if (len > dst_len - 1) len = dst_len - 1;
    memcpy( dst, src, len * sizeof(*dst) );
    dst[len] = 0;
}

/*
 * Last-resort <text> reader for documents whose DOM answers neither
 * IXmlCharacterData nor the element-list accessors: serialize the document
 * through IXmlNodeSerializer::GetXml and scan the <text>…</text> spans out of
 * the markup.
 */
static HRESULT toast_collect_texts_markup( __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *doc,
        HSTRING **out, UINT32 *count )
{
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlNodeSerializer *serializer = NULL;
    HSTRING xml = NULL, *texts = NULL;
    UINT32 size = 0, n = 0;
    const WCHAR *p;
    HRESULT hr;

    *out = NULL;
    *count = 0;
    hr = IInspectable_QueryInterface( (IInspectable *)doc, &IID_IXmlNodeSerializer, (void **)&serializer );
    if (FAILED( hr )) return hr;
    hr = IXmlNodeSerializer_GetXml( serializer, &xml );
    IInspectable_Release( (IInspectable *)serializer );
    if (FAILED( hr )) return hr;

    hr = S_OK;
    for (p = WindowsGetStringRawBuffer( xml, NULL ); p && (p = wcsstr( p, L"<text" )); )
    {
        const WCHAR *gt, *close;

        if (!(gt = wcschr( p, L'>' ))) break;
        p = gt + 1;
        if (gt[-1] == L'/') close = p;                            /* <text/>: empty node */
        else if (!(close = wcsstr( p, L"</text" ))) break;

        if (n == size)
        {
            HSTRING *grown = realloc( texts, (size + 8) * sizeof(*grown) );
            if (!grown) { hr = E_OUTOFMEMORY; break; }
            texts = grown;
            size += 8;
        }
        texts[n] = NULL;
        if (FAILED( hr = WindowsCreateString( p, close - p, &texts[n] ) )) break;
        ++n;
        p = close;
    }
    WindowsDeleteString( xml );

    if (SUCCEEDED( hr ))
    {
        *out = texts;
        *count = n;
    }
    else
    {
        UINT32 i;
        for (i = 0; texts && i < n; ++i) WindowsDeleteString( texts[i] );
        free( texts );
    }
    return hr;
}

static HRESULT toast_extract_text( IToastNotification *notification,
        WCHAR *title, size_t title_len, WCHAR *body, size_t body_len )
{
    static const WCHAR notificationW[] = L"Notification";
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *doc = NULL;
    HSTRING *texts = NULL;
    UINT32 i, count = 0, first, len;
    const WCHAR *raw;
    WCHAR *p;
    HRESULT hr;

    title[0] = body[0] = 0;

    if (FAILED( hr = IToastNotification_get_Content( notification, &doc ) ) || !doc)
        return S_OK;   /* a notification with no content shows an empty balloon */

    hr = toast_collect_texts( doc, &texts, &count );
    if (FAILED( hr ) || !count)
    {
        if (texts)
        {
            for (i = 0; i < count; ++i) WindowsDeleteString( texts[i] );
            free( texts );
            texts = NULL;
        }
        hr = toast_collect_texts_markup( doc, &texts, &count );
    }
    IInspectable_Release( (IInspectable *)doc );
    if (FAILED( hr )) return hr;

    for (i = 0; i < count; ++i)
    {
        if (texts[i] && WindowsGetStringLen( texts[i] )) break;
    }
    if (i == count) goto done;   /* only empty <text> elements */
    first = i;

    /* a lone <text> is the body; the title falls back to the app label */
    for (i = first + 1; i < count; ++i)
    {
        if (texts[i] && WindowsGetStringLen( texts[i] )) break;
    }
    if (i == count)
    {
        toast_copy_text( title, title_len, notificationW, ARRAY_SIZE( notificationW ) - 1 );
        raw = WindowsGetStringRawBuffer( texts[first], &len );
        toast_copy_text( body, body_len, raw, len );
        goto done;
    }

    raw = WindowsGetStringRawBuffer( texts[first], &len );
    toast_copy_text( title, title_len, raw, len );

    p = body;
    for (i = first + 1; i < count; ++i)
    {
        if (!texts[i] || !(len = WindowsGetStringLen( texts[i] ))) continue;
        if (p > body && p < body + body_len - 1) *p++ = '\n';
        if ((size_t)(p - body) + len > body_len - 1) len = body_len - 1 - (p - body);
        memcpy( p, WindowsGetStringRawBuffer( texts[i], NULL ), len * sizeof(*p) );
        p += len;
    }
    *p = 0;

done:
    for (i = 0; i < count; ++i) WindowsDeleteString( texts[i] );
    free( texts );
    return S_OK;
}

HRESULT toast_extract_payload( __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *doc,
        struct toast_payload *payload )
{
    HSTRING *texts = NULL;
    UINT32 i, count = 0;
    HRESULT hr;

    memset( payload, 0, sizeof(*payload) );

    if (FAILED( hr = toast_xml_first_attribute( doc, L"toast", L"launch", &payload->launch ) ))
        return hr;
    if (FAILED( hr = toast_collect_texts( doc, &texts, &count ) )) return hr;

    if (count == 1) payload->body = texts[0];
    else if (count >= 2)
    {
        payload->summary = texts[0];
        hr = toast_join_texts( texts, 1, count, &payload->body );
    }
    for (i = 0; SUCCEEDED( hr ) && i < count; ++i)
    {
        if (texts[i] == payload->summary || texts[i] == payload->body) continue;
        WindowsDeleteString( texts[i] );
    }
    free( texts );

    if (FAILED( hr )) return hr;
    return toast_xml_first_attribute( doc, L"image", L"src", &payload->image );
}

/*
 * Wine extension interfaces on the ToastNotification face.
 *
 * windows.ui.notifications.idl freezes the IToastNotification/IToastNotification2
 * vtables at their 1081/1105-contract shapes, and the ToastGeneric template
 * (binding/@template="ToastGeneric", the payload ToastNotificationManager.
 * GetTemplateContent is documented to hand out on 1703+ builds) has no
 * ToastTemplateType member before ToastImageAndText01..ToastText04.  The
 * NotificationSuppressed event and the ToastContent/ToastTemplate readback of
 * the parsed payload are therefore answered on module-private interfaces,
 * QI-able on the same object with shared identity, keyed by fixed GUIDs.
 */
#define ToastTemplate_ToastImageAndText01 0
#define ToastTemplate_ToastImageAndText02 1
#define ToastTemplate_ToastImageAndText03 2
#define ToastTemplate_ToastImageAndText04 3
#define ToastTemplate_ToastText01         4
#define ToastTemplate_ToastText02         5
#define ToastTemplate_ToastText03         6
#define ToastTemplate_ToastText04         7
#define ToastTemplate_ToastGeneric        8

interface IToastNotificationSuppressed;
typedef interface IToastNotificationSuppressed IToastNotificationSuppressed;

typedef struct IToastNotificationSuppressedVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IToastNotificationSuppressed *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IToastNotificationSuppressed *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IToastNotificationSuppressed *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IToastNotificationSuppressed *iface, ULONG *iid_count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IToastNotificationSuppressed *iface, HSTRING *class_name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IToastNotificationSuppressed *iface, TrustLevel *trust_level );
    HRESULT (STDMETHODCALLTYPE *add_NotificationSuppressed)( IToastNotificationSuppressed *iface,
            __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler,
            EventRegistrationToken *token );
    HRESULT (STDMETHODCALLTYPE *remove_NotificationSuppressed)( IToastNotificationSuppressed *iface,
            EventRegistrationToken token );
} IToastNotificationSuppressedVtbl;

interface IToastNotificationSuppressed
{
    const struct IToastNotificationSuppressedVtbl *lpVtbl;
};

#define IToastNotificationSuppressed_add_NotificationSuppressed(p,h,t) \
        (p)->lpVtbl->add_NotificationSuppressed(p,h,t)
#define IToastNotificationSuppressed_remove_NotificationSuppressed(p,t) \
        (p)->lpVtbl->remove_NotificationSuppressed(p,t)

interface IToastNotificationTemplate;
typedef interface IToastNotificationTemplate IToastNotificationTemplate;

typedef struct IToastNotificationTemplateVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IToastNotificationTemplate *iface, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( IToastNotificationTemplate *iface );
    ULONG (STDMETHODCALLTYPE *Release)( IToastNotificationTemplate *iface );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IToastNotificationTemplate *iface, ULONG *iid_count, IID **iids );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IToastNotificationTemplate *iface, HSTRING *class_name );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IToastNotificationTemplate *iface, TrustLevel *trust_level );
    HRESULT (STDMETHODCALLTYPE *get_ToastTemplate)( IToastNotificationTemplate *iface, INT32 *value );
    HRESULT (STDMETHODCALLTYPE *get_ToastContent)( IToastNotificationTemplate *iface,
            __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument **value );
} IToastNotificationTemplateVtbl;

interface IToastNotificationTemplate
{
    const struct IToastNotificationTemplateVtbl *lpVtbl;
};

#define IToastNotificationTemplate_get_ToastTemplate(p,v) (p)->lpVtbl->get_ToastTemplate(p,v)
#define IToastNotificationTemplate_get_ToastContent(p,v) (p)->lpVtbl->get_ToastContent(p,v)

struct toast_notification
{
    IToastNotification IToastNotification_iface;
    IToastNotification2 IToastNotification2_iface;
    IToastNotificationSuppressed IToastNotificationSuppressed_iface;
    IToastNotificationTemplate IToastNotificationTemplate_iface;
    __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *content;
    __FIReference_1_DateTime *expiration_time;
    HSTRING tag, group;
    boolean suppress_popup;
    LONG ref;
    struct list activated_sinks, dismissed_sinks, failed_sinks, suppressed_sinks;
    struct list shown_entry;   /* entry on shown_toasts while visible */
    UINT32 fd_id;              /* notification daemon id of the visible toast */
    HSTRING launch_args;       /* toast/@launch of the content, replayed on activation */
};

static inline struct toast_notification *impl_from_IToastNotification( IToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification, IToastNotification_iface );
}

static void toast_notifier_drain_events( void );
static void toast_history_add( IToastNotification *toast );
static void toast_history_remove_toast( IToastNotification *toast );
static void toast_notification_fire_suppressed( struct toast_notification *impl );

/* ToastNotifications currently visible in the notification daemon, keyed by
 * the daemon-assigned id carried in ActionInvoked/NotificationClosed. */
static struct list shown_toasts = LIST_INIT( shown_toasts );

/*
 * The app view's toast queue: the last 16 notifications handed to
 * IToastNotifier::Show, oldest first, newest at the tail.  The queue is
 * process-wide (the prefix has one app view, so every ToastNotifier reads and
 * appends to the same queue, the way GetForCurrentView's single live notifier
 * owns it on Windows); showing the same toast twice moves it to the newest
 * slot, and showing a 17th drops the oldest.  ToastNotifier answers
 * IVectorView<ToastNotification> (the .NET projection of IObservableVector)
 * with a snapshot of it: that QI is the get_Notifications entry point.
 */
#define TOAST_SHOWN_QUEUE_SIZE 16

struct toast_shown_entry
{
    struct list entry;
    IToastNotification *toast;
};

static struct list shown_queue = LIST_INIT( shown_queue );
static UINT32 shown_queue_size;

static void toast_shown_queue_add( IToastNotification *toast )
{
    struct toast_shown_entry *entry;
    IToastNotification *dropped = NULL;

    EnterCriticalSection( &toast_state_cs );
    LIST_FOR_EACH_ENTRY( entry, &shown_queue, struct toast_shown_entry, entry )
    {
        if (entry->toast != toast) continue;
        list_remove( &entry->entry );
        list_add_tail( &shown_queue, &entry->entry );
        LeaveCriticalSection( &toast_state_cs );
        return;
    }
    if (!(entry = malloc( sizeof(*entry) )))
    {
        LeaveCriticalSection( &toast_state_cs );
        return;
    }
    entry->toast = toast;
    IToastNotification_AddRef( toast );
    list_add_tail( &shown_queue, &entry->entry );
    ++shown_queue_size;

    if (shown_queue_size > TOAST_SHOWN_QUEUE_SIZE)
    {
        entry = LIST_ENTRY( list_head( &shown_queue ), struct toast_shown_entry, entry );
        list_remove( &entry->entry );
        dropped = entry->toast;
        free( entry );
        --shown_queue_size;
    }
    LeaveCriticalSection( &toast_state_cs );
    if (dropped) IToastNotification_Release( dropped );
}

static HRESULT toast_shown_queue_snapshot( __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification **out );

/* get_Notifications: the shown-toast queue as IObservableVector, projected
 * here as the closed IVectorView<ToastNotification> pinterface. */
static HRESULT toast_notifier_get_Notifications( IToastNotifier *iface,
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification **value )
{
    if (!value) return E_POINTER;
    return toast_shown_queue_snapshot( value );
}

struct toast_notifier
{
    IToastNotifier IToastNotifier_iface;
    LONG ref;
};

static inline struct toast_notifier *impl_from_IToastNotifier( IToastNotifier *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notifier, IToastNotifier_iface );
}

static HRESULT WINAPI toast_notifier_QueryInterface( IToastNotifier *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IToastNotifier ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    /* get_Notifications: QI hands out a snapshot of the shown-toast queue. */
    if (IsEqualGUID( iid, &IID___FIVectorView_1_Windows__CUI__CNotifications__CToastNotification ))
        return toast_notifier_get_Notifications( iface,
                (__FIVectorView_1_Windows__CUI__CNotifications__CToastNotification **)out );

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_notifier_AddRef( IToastNotifier *iface )
{
    struct toast_notifier *impl = impl_from_IToastNotifier( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_notifier_Release( IToastNotifier *iface )
{
    struct toast_notifier *impl = impl_from_IToastNotifier( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI toast_notifier_GetIids( IToastNotifier *iface, ULONG *iid_count, IID **iids )
{
    static const IID *const iids_arr[] =
    {
        &IID_IToastNotifier,
    };

    if (!iid_count) return E_POINTER;
    return notifications_get_iids( iids_arr, ARRAY_SIZE( iids_arr ), iid_count, iids );
}

static HRESULT WINAPI toast_notifier_GetRuntimeClassName( IToastNotifier *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotifier,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotifier ) - 1, class_name );
}

static HRESULT WINAPI toast_notifier_GetTrustLevel( IToastNotifier *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

/*
 * Win32 tray delivery. Shell_NotifyIconW is resolved from shell32.dll at call
 * time (the module links shell32, the runtime lookup keeps the import set as
 * it is), on a process-wide message-only window owning one icon id. Setting
 * WINE_DISABLE_TOAST_POPUP keeps toasts on the FreeDesktop path only.
 */
#define TOAST_TRAY_ID 0x544F4153   /* 'TOAS' */

typedef BOOL (WINAPI *pShell_NotifyIconW)( DWORD message, NOTIFYICONDATAW *data );

static pShell_NotifyIconW toast_shell_notify_icon( void )
{
    static pShell_NotifyIconW fn;
    pShell_NotifyIconW ret;

    EnterCriticalSection( &toast_state_cs );
    if (!fn)
    {
        HMODULE module;
        if (!(module = GetModuleHandleW( L"shell32.dll" ))) module = LoadLibraryW( L"shell32.dll" );
        if (module) fn = (pShell_NotifyIconW)GetProcAddress( module, "Shell_NotifyIconW" );
    }
    ret = fn;
    LeaveCriticalSection( &toast_state_cs );
    return ret;
}

static HWND toast_tray_window( void )
{
    static const WCHAR class_name[] = L"Wine Toast Tray";
    static HWND hwnd;
    HWND ret;

    EnterCriticalSection( &toast_state_cs );
    if (!hwnd)
    {
        WNDCLASSEXW desc = {0};

        desc.cbSize = sizeof(desc);
        desc.lpfnWndProc = DefWindowProcW;
        desc.hInstance = GetModuleHandleW( NULL );
        desc.lpszClassName = class_name;
        RegisterClassExW( &desc );
        hwnd = CreateWindowExW( 0, class_name, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, desc.hInstance, NULL );
    }
    ret = hwnd;
    LeaveCriticalSection( &toast_state_cs );
    return ret;
}

static void toast_dispatch_notify_icon( const WCHAR *title, const WCHAR *body )
{
    static BOOL added;
    pShell_NotifyIconW notify_icon;
    NOTIFYICONDATAW nid = {0};
    HWND hwnd;

    if (GetEnvironmentVariableW( L"WINE_DISABLE_TOAST_POPUP", NULL, 0 )) return;
    if (!(notify_icon = toast_shell_notify_icon())) return;
    if (!(hwnd = toast_tray_window())) return;

    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TOAST_TRAY_ID;
    nid.uFlags = NIF_INFO | NIF_TIP;
    nid.dwInfoFlags = NIIF_INFO;
    lstrcpynW( nid.szInfo, body, ARRAY_SIZE( nid.szInfo ) );
    lstrcpynW( nid.szInfoTitle, title, ARRAY_SIZE( nid.szInfoTitle ) );
    lstrcpynW( nid.szTip, title, ARRAY_SIZE( nid.szTip ) );

    EnterCriticalSection( &toast_state_cs );
    if (notify_icon( added ? NIM_MODIFY : NIM_ADD, &nid ) && !added) added = TRUE;
    else if (added) notify_icon( NIM_ADD, &nid );   /* the icon was removed in between */
    LeaveCriticalSection( &toast_state_cs );
}

static void toast_dismiss_notify_icon( void )
{
    static const WCHAR emptyW[] = L"";
    pShell_NotifyIconW notify_icon;
    NOTIFYICONDATAW nid = {0};
    HWND hwnd;

    if (GetEnvironmentVariableW( L"WINE_DISABLE_TOAST_POPUP", NULL, 0 )) return;
    if (!(notify_icon = toast_shell_notify_icon())) return;
    if (!(hwnd = toast_tray_window())) return;

    /* An empty szInfo on NIM_MODIFY retires the visible balloon. */
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TOAST_TRAY_ID;
    nid.uFlags = NIF_INFO;
    lstrcpynW( nid.szInfo, emptyW, ARRAY_SIZE( nid.szInfo ) );
    notify_icon( NIM_MODIFY, &nid );
}

static HRESULT WINAPI toast_notifier_Show( IToastNotifier *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *notification )
{
    struct toast_payload payload = {0};
    struct toast_notification *impl;
    struct fd_notify_params params;
    WCHAR title[64], body[256];
    HRESULT hr;

    TRACE( "iface %p, notification %p.\n", iface, notification );

    toast_notifier_drain_events();
    if (!notification) return E_POINTER;
    impl = impl_from_IToastNotification( notification );
    if (!impl->content) return S_OK;

    hr = toast_extract_payload( impl->content, &payload );
    if (FAILED( hr ))
    {
        toast_payload_free( &payload );
        return S_OK; /* malformed document: the toast is dropped, as on Windows */
    }
    WindowsDeleteString( impl->launch_args );
    impl->launch_args = payload.launch;
    payload.launch = NULL;

    /* Showing a toast puts it in the app's notification history, whether or not
     * a notification daemon answered to display it. */
    toast_history_add( notification );
    toast_shown_queue_add( notification );
    if (impl->suppress_popup) toast_notification_fire_suppressed( impl );

    params.app_name = L"Wine Application";
    params.icon = payload.image ? WindowsGetStringRawBuffer( payload.image, NULL ) : NULL;
    params.summary = payload.summary ? WindowsGetStringRawBuffer( payload.summary, NULL ) : NULL;
    params.body = payload.body ? WindowsGetStringRawBuffer( payload.body, NULL ) : NULL;
    params.badge_count = -1;
    params.replaces_id = impl->fd_id;

    /* The parsed <text> content of the payload, for gate traces. */
    NOTIFY_TRACE( "toast shown: summary %s, body %s%s\n", debugstr_w( params.summary ),
                  debugstr_w( params.body ), impl->suppress_popup ? " (popup suppressed)" : "" );

    /* Win32 balloon on the tray, fed by the same <text> elements. */
    toast_extract_text( notification, title, ARRAY_SIZE( title ), body, ARRAY_SIZE( body ) );
    TRACE( "toast Show title=%s body=%s\n", debugstr_w( title ), debugstr_w( body ) );
    toast_dispatch_notify_icon( title, body );

    if (TWINAPI_CALL( fd_notify, &params ) == STATUS_SUCCESS && params.id)
    {
        EnterCriticalSection( &toast_state_cs );
        impl->fd_id = params.id;
        if (list_empty( &impl->shown_entry ))
        {
            IUnknown_AddRef( (IUnknown *)notification );
            list_add_tail( &shown_toasts, &impl->shown_entry );
        }
        LeaveCriticalSection( &toast_state_cs );
        TRACE( "delivered toast as notification id %u\n", params.id );
    }
    toast_payload_free( &payload );
    return S_OK;
}

static HRESULT WINAPI toast_notifier_Hide( IToastNotifier *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *notification )
{
    struct toast_notification *impl;
    struct fd_close_params close_params;

    TRACE( "iface %p, notification %p.\n", iface, notification );

    toast_notifier_drain_events();
    if (!notification) return E_POINTER;
    impl = impl_from_IToastNotification( notification );
    EnterCriticalSection( &toast_state_cs );
    close_params.id = impl->fd_id;
    impl->fd_id = 0;
    if (!list_empty( &impl->shown_entry ))
    {
        list_remove( &impl->shown_entry );
        list_init( &impl->shown_entry );
        LeaveCriticalSection( &toast_state_cs );
        IToastNotification_Release( notification );
    }
    else
    {
        LeaveCriticalSection( &toast_state_cs );
    }
    if (close_params.id) TWINAPI_CALL( fd_close, &close_params );
    toast_dismiss_notify_icon();
    toast_history_remove_toast( notification );
    return S_OK;
}

static HRESULT WINAPI toast_notifier_get_Setting( IToastNotifier *iface,
        __x_ABI_CWindows_CUI_CNotifications_CNotificationSetting *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NotificationSetting_Enabled;
    return S_OK;
}

static HRESULT WINAPI toast_notifier_AddToSchedule( IToastNotifier *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification *scheduled_toast )
{
    FIXME( "iface %p, scheduled_toast %p stub!\n", iface, scheduled_toast );
    return S_OK;
}

static HRESULT WINAPI toast_notifier_RemoveFromSchedule( IToastNotifier *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification *scheduled_toast )
{
    FIXME( "iface %p, scheduled_toast %p stub!\n", iface, scheduled_toast );
    return S_OK;
}

/*
 * The app's tray path enumerates scheduled notifications (CancelScheduled-
 * Notifications) before touching the tray; the C# projection throws on
 * E_NOTIMPL and the unhandled exception fail-fasts the process, so the empty
 * scheduled set is answered as a real (empty) IVectorView.
 *
 * The C# projection of IReadOnlyList<T> enumerates the same object through
 * IEnumerable<T>: IReadOnlyListImpl`1.Make_IEnumerableObjRef() QIs the vector
 * view for the closed pinterface instantiation
 * IIterable<ScheduledToastNotification> ({7a7b2a51-c182-5846-a861-4f9c036f24ad},
 * the parameterized instance of pinterface({4947e93c-...};...)), and an
 * E_NOINTERFACE there becomes an InvalidCastException in the caller.  The
 * IIterable is answered on the same object (shared identity) and its First()
 * hands out a real (exhausted) IIterator.
 */
struct scheduled_toast_vector
{
    __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
    __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
    LONG ref;
};

static inline struct scheduled_toast_vector *impl_from_FIVectorView_ScheduledToastNotification(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct scheduled_toast_vector,
            __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface );
}

static HRESULT WINAPI sched_vector_QueryInterface(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification ))
    {
        struct scheduled_toast_vector *impl = impl_from_FIVectorView_ScheduledToastNotification( iface );

        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->__FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sched_vector_AddRef(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_vector *impl = impl_from_FIVectorView_ScheduledToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sched_vector_Release(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_vector *impl = impl_from_FIVectorView_ScheduledToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sched_vector_GetIids(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count) return E_POINTER;
    *iid_count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI sched_vector_GetRuntimeClassName(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IVectorView`1<Windows.UI.Notifications.ScheduledToastNotification>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI sched_vector_GetTrustLevel(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sched_vector_GetAt(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, UINT32 index,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification **value )
{
    return E_BOUNDS;
}

static HRESULT WINAPI sched_vector_get_Size(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT WINAPI sched_vector_IndexOf(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification *element, UINT32 *index, BOOLEAN *value )
{
    if (!index || !value) return E_POINTER;
    *index = 0;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI sched_vector_GetMany(
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, UINT32 start_index,
        UINT32 items_size, __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification **items, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = 0;
    return S_OK;
}

static const struct __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotificationVtbl sched_vector_vtbl =
{
    sched_vector_QueryInterface,
    sched_vector_AddRef,
    sched_vector_Release,
    /* IInspectable methods */
    sched_vector_GetIids,
    sched_vector_GetRuntimeClassName,
    sched_vector_GetTrustLevel,
    /* IVectorView<ScheduledToastNotification> methods */
    sched_vector_GetAt,
    sched_vector_get_Size,
    sched_vector_IndexOf,
    sched_vector_GetMany,
};

/*
 * IIterator<ScheduledToastNotification> over the (empty) scheduled set.  The
 * set never has contents, so the iterator is created already exhausted:
 * HasCurrent is FALSE, MoveNext is FALSE, GetMany returns zero items and
 * get_Current is E_BOUNDS, exactly what an empty enumeration reports.
 */
struct scheduled_toast_iterator
{
    __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
    LONG ref;
};

static inline struct scheduled_toast_iterator *impl_from_FIIterator_ScheduledToastNotification(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct scheduled_toast_iterator,
            __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface );
}

static HRESULT WINAPI sched_iterator_QueryInterface(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sched_iterator_AddRef(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_iterator *impl = impl_from_FIIterator_ScheduledToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sched_iterator_Release(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_iterator *impl = impl_from_FIIterator_ScheduledToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sched_iterator_GetIids(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI sched_iterator_GetRuntimeClassName(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterator`1<Windows.UI.Notifications.ScheduledToastNotification>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI sched_iterator_GetTrustLevel(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sched_iterator_get_Current(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return E_BOUNDS;
}

static HRESULT WINAPI sched_iterator_get_HasCurrent(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, BOOLEAN *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI sched_iterator_MoveNext(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, BOOLEAN *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI sched_iterator_GetMany(
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, UINT32 items_size,
        __x_ABI_CWindows_CUI_CNotifications_CIScheduledToastNotification **items, UINT32 *value )
{
    if (!value) return E_POINTER;
    if (items_size && !items) return E_POINTER;
    *value = 0;
    return S_OK;
}

static const struct __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotificationVtbl sched_iterator_vtbl =
{
    sched_iterator_QueryInterface,
    sched_iterator_AddRef,
    sched_iterator_Release,
    /* IInspectable methods */
    sched_iterator_GetIids,
    sched_iterator_GetRuntimeClassName,
    sched_iterator_GetTrustLevel,
    /* IIterator<ScheduledToastNotification> methods */
    sched_iterator_get_Current,
    sched_iterator_get_HasCurrent,
    sched_iterator_MoveNext,
    sched_iterator_GetMany,
};

static inline struct scheduled_toast_vector *impl_from_FIIterable_ScheduledToastNotification(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct scheduled_toast_vector,
            __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface );
}

static HRESULT WINAPI sched_iterable_QueryInterface(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, REFIID iid, void **out )
{
    struct scheduled_toast_vector *impl = impl_from_FIIterable_ScheduledToastNotification( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    /* IUnknown identity is the vector view iface, the object's canonical one. */
    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->__FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI sched_iterable_AddRef(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_vector *impl = impl_from_FIIterable_ScheduledToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI sched_iterable_Release(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface )
{
    struct scheduled_toast_vector *impl = impl_from_FIIterable_ScheduledToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI sched_iterable_GetIids(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI sched_iterable_GetRuntimeClassName(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterable`1<Windows.UI.Notifications.ScheduledToastNotification>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI sched_iterable_GetTrustLevel(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI sched_iterable_First(
        __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification *iface,
        __FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification **result )
{
    struct scheduled_toast_iterator *impl;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->__FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface.lpVtbl = &sched_iterator_vtbl;
    impl->ref = 1;
    *result = &impl->__FIIterator_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
    return S_OK;
}

static const struct __FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotificationVtbl sched_iterable_vtbl =
{
    sched_iterable_QueryInterface,
    sched_iterable_AddRef,
    sched_iterable_Release,
    /* IInspectable methods */
    sched_iterable_GetIids,
    sched_iterable_GetRuntimeClassName,
    sched_iterable_GetTrustLevel,
    /* IIterable<ScheduledToastNotification> methods */
    sched_iterable_First,
};

static HRESULT toast_notifier_GetScheduled( IToastNotifier *iface,
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification **result )
{
    struct scheduled_toast_vector *impl;

    if (!result) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->__FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface.lpVtbl = &sched_vector_vtbl;
    impl->__FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface.lpVtbl = &sched_iterable_vtbl;
    impl->ref = 1;
    *result = &impl->__FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification_iface;
    return S_OK;
}

static const struct IToastNotifierVtbl toast_notifier_vtbl =
{
    toast_notifier_QueryInterface,
    toast_notifier_AddRef,
    toast_notifier_Release,
    /* IInspectable methods */
    toast_notifier_GetIids,
    toast_notifier_GetRuntimeClassName,
    toast_notifier_GetTrustLevel,
    /* IToastNotifier methods */
    toast_notifier_Show,
    toast_notifier_Hide,
    toast_notifier_get_Setting,
    toast_notifier_AddToSchedule,
    toast_notifier_RemoveFromSchedule,
    toast_notifier_GetScheduled,
};

static HRESULT create_toast_notifier( __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier **out )
{
    struct toast_notifier *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IToastNotifier_iface.lpVtbl = &toast_notifier_vtbl;
    impl->ref = 1;
    *out = (__x_ABI_CWindows_CUI_CNotifications_CIToastNotifier *)&impl->IToastNotifier_iface;
    return S_OK;
}

/*
 * Windows.UI.Notifications.ToastNotificationHistory
 * (IToastNotificationHistory 5caddc63-01d3-4c97-986f-0533483fee14,
 *  IToastNotificationHistory2 3bc3d253-2f31-4092-9129-8ad5abf067da):
 * the store of toasts this app has shown. The FreeDesktop bridge keeps no
 * history of its own, so the manager owns the list: IToastNotifier::Show
 * records every toast it is handed, replacing the entry carrying the same
 * tag/group the way Windows does, and the history accessors read from and
 * prune that list. A NULL tag or group in a Remove() call is the wildcard,
 * matching every toast on the other dimension, as on Windows.
 *
 * GetHistory() snapshots the list into an IVectorView<ToastNotification>,
 * answered with the closed IIterable<ToastNotification> pinterface on the same
 * object because the C# projection enumerates IReadOnlyList<T> through
 * IEnumerable<T>; see the scheduled set above for the same requirement.
 */
struct toast_history_entry
{
    struct list entry;
    IToastNotification *toast;
    HSTRING tag, group;
};

static struct list toast_history = LIST_INIT( toast_history );

static BOOLEAN toast_history_matches( HSTRING a, HSTRING b )
{
    const WCHAR *raw_a, *raw_b;
    UINT32 len_a = 0, len_b = 0;

    if (a == b) return TRUE;
    if (!a || !b) return FALSE;
    raw_a = WindowsGetStringRawBuffer( a, &len_a );
    raw_b = WindowsGetStringRawBuffer( b, &len_b );
    return len_a == len_b && !memcmp( raw_a, raw_b, len_a * sizeof(*raw_a) );
}

static void toast_history_add( IToastNotification *toast )
{
    __x_ABI_CWindows_CUI_CNotifications_CIToastNotification2 *toast2;
    struct toast_history_entry *entry, *next;
    struct list removed = LIST_INIT( removed );
    HSTRING tag = NULL, group = NULL;

    if (SUCCEEDED( IToastNotification_QueryInterface( toast, &IID_IToastNotification2, (void **)&toast2 ) ))
    {
        IToastNotification2_get_Tag( toast2, &tag );
        IToastNotification2_get_Group( toast2, &group );
        IToastNotification2_Release( toast2 );
    }

    if (!(entry = malloc( sizeof(*entry) )))
    {
        WindowsDeleteString( tag );
        WindowsDeleteString( group );
        return;
    }
    list_init( &entry->entry );
    entry->toast = toast;
    entry->tag = tag;      /* the dups handed out by get_Tag/get_Group, owned here */
    entry->group = group;
    IToastNotification_AddRef( toast );

    EnterCriticalSection( &toast_state_cs );
    {
        struct toast_history_entry *cur, *nxt;
        LIST_FOR_EACH_ENTRY_SAFE( cur, nxt, &toast_history, struct toast_history_entry, entry )
        {
            if (!toast_history_matches( cur->tag, tag ) || !toast_history_matches( cur->group, group ))
                continue;
            list_remove( &cur->entry );
            list_add_tail( &removed, &cur->entry );
        }
    }
    list_add_tail( &toast_history, &entry->entry );
    LeaveCriticalSection( &toast_state_cs );

    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &removed, struct toast_history_entry, entry )
    {
        list_remove( &entry->entry );
        IToastNotification_Release( entry->toast );
        WindowsDeleteString( entry->tag );
        WindowsDeleteString( entry->group );
        free( entry );
    }
}

/* Drop the history entries for this exact notification (ToastNotifier::Hide). */
static void toast_history_remove_toast( IToastNotification *toast )
{
    struct list removed = LIST_INIT( removed );
    struct toast_history_entry *entry, *next;

    EnterCriticalSection( &toast_state_cs );
    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &toast_history, struct toast_history_entry, entry )
    {
        if (entry->toast != toast) continue;
        list_remove( &entry->entry );
        list_add_tail( &removed, &entry->entry );
    }
    LeaveCriticalSection( &toast_state_cs );

    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &removed, struct toast_history_entry, entry )
    {
        list_remove( &entry->entry );
        IToastNotification_Release( entry->toast );
        WindowsDeleteString( entry->tag );
        WindowsDeleteString( entry->group );
        free( entry );
    }
}

/* Drop the history entries matching tag and group; NULL matches anything. */
static void toast_history_prune( HSTRING tag, HSTRING group )
{
    struct list removed = LIST_INIT( removed );
    struct toast_history_entry *entry, *next;

    EnterCriticalSection( &toast_state_cs );
    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &toast_history, struct toast_history_entry, entry )
    {
        if (tag && !toast_history_matches( entry->tag, tag )) continue;
        if (group && !toast_history_matches( entry->group, group )) continue;
        list_remove( &entry->entry );
        list_add_tail( &removed, &entry->entry );
    }
    LeaveCriticalSection( &toast_state_cs );

    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &removed, struct toast_history_entry, entry )
    {
        list_remove( &entry->entry );
        IToastNotification_Release( entry->toast );
        WindowsDeleteString( entry->tag );
        WindowsDeleteString( entry->group );
        free( entry );
    }
}

/*
 * IVectorView<ToastNotification>/IIterable<ToastNotification> snapshot of the
 * history taken at GetHistory() time; it owns a reference on each toast.
 */
struct toast_snapshot_vector
{
    __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_iface;
    __FIIterable_1_Windows__CUI__CNotifications__CToastNotification
        __FIIterable_1_Windows__CUI__CNotifications__CToastNotification_iface;
    IToastNotification **items;
    UINT32 size;
    LONG ref;
};

static inline struct toast_snapshot_vector *impl_from_FIVectorView_ToastNotification(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct toast_snapshot_vector,
            __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_iface );
}

static inline struct toast_snapshot_vector *impl_from_FIIterable_ToastNotification(
        __FIIterable_1_Windows__CUI__CNotifications__CToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct toast_snapshot_vector,
            __FIIterable_1_Windows__CUI__CNotifications__CToastNotification_iface );
}

static const struct __FIIterator_1_Windows__CUI__CNotifications__CToastNotificationVtbl
        history_iterator_vtbl;

struct toast_snapshot_iterator
{
    __FIIterator_1_Windows__CUI__CNotifications__CToastNotification
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification_iface;
    struct toast_snapshot_vector *vector;
    INT32 index;   /* position of Current, -1 before the first MoveNext */
    LONG ref;
};

static inline struct toast_snapshot_iterator *impl_from_FIIterator_ToastNotification(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface )
{
    return CONTAINING_RECORD( iface, struct toast_snapshot_iterator,
            __FIIterator_1_Windows__CUI__CNotifications__CToastNotification_iface );
}

static HRESULT WINAPI history_vector_QueryInterface(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIVectorView_1_Windows__CUI__CNotifications__CToastNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIIterable_1_Windows__CUI__CNotifications__CToastNotification ))
    {
        struct toast_snapshot_vector *impl = impl_from_FIVectorView_ToastNotification( iface );

        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->__FIIterable_1_Windows__CUI__CNotifications__CToastNotification_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI history_vector_AddRef(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface )
{
    struct toast_snapshot_vector *impl = impl_from_FIVectorView_ToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI history_vector_Release(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface )
{
    struct toast_snapshot_vector *impl = impl_from_FIVectorView_ToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        UINT32 i;

        TRACE( "destroying %p\n", iface );
        for (i = 0; i < impl->size; ++i) IToastNotification_Release( impl->items[i] );
        free( impl->items );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI history_vector_GetIids(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface, ULONG *iid_count, IID **iids )
{
    static const IID *const iids_arr[] =
    {
        &IID___FIVectorView_1_Windows__CUI__CNotifications__CToastNotification,
        &IID___FIIterable_1_Windows__CUI__CNotifications__CToastNotification,
    };

    if (!iid_count) return E_POINTER;
    return notifications_get_iids( iids_arr, ARRAY_SIZE( iids_arr ), iid_count, iids );
}

static HRESULT WINAPI history_vector_GetRuntimeClassName(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IVectorView`1<Windows.UI.Notifications.ToastNotification>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI history_vector_GetTrustLevel(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI history_vector_GetAt(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface, UINT32 index,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification **value )
{
    struct toast_snapshot_vector *impl = impl_from_FIVectorView_ToastNotification( iface );

    if (!value) return E_POINTER;
    if (index >= impl->size) return E_BOUNDS;
    *value = impl->items[index];
    IToastNotification_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI history_vector_get_Size(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface, UINT32 *value )
{
    struct toast_snapshot_vector *impl = impl_from_FIVectorView_ToastNotification( iface );

    if (!value) return E_POINTER;
    *value = impl->size;
    return S_OK;
}

static HRESULT WINAPI history_vector_IndexOf(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification *element, UINT32 *index, BOOLEAN *value )
{
    struct toast_snapshot_vector *impl = impl_from_FIVectorView_ToastNotification( iface );
    UINT32 i;

    if (!index || !value) return E_POINTER;
    for (i = 0; i < impl->size; ++i)
    {
        if (impl->items[i] != (IToastNotification *)element) continue;
        *index = i;
        *value = TRUE;
        return S_OK;
    }
    *index = 0;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI history_vector_GetMany(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification *iface, UINT32 start_index,
        UINT32 items_size, __x_ABI_CWindows_CUI_CNotifications_CIToastNotification **items, UINT32 *value )
{
    struct toast_snapshot_vector *impl = impl_from_FIVectorView_ToastNotification( iface );
    UINT32 i = 0;

    if (!value) return E_POINTER;
    if (items && start_index < impl->size)
    {
        for (; i < items_size && start_index + i < impl->size; ++i)
        {
            items[i] = impl->items[start_index + i];
            IToastNotification_AddRef( items[i] );
        }
    }
    *value = i;
    return S_OK;
}

static const struct __FIVectorView_1_Windows__CUI__CNotifications__CToastNotificationVtbl history_vector_vtbl =
{
    history_vector_QueryInterface,
    history_vector_AddRef,
    history_vector_Release,
    /* IInspectable methods */
    history_vector_GetIids,
    history_vector_GetRuntimeClassName,
    history_vector_GetTrustLevel,
    /* IVectorView<ToastNotification> methods */
    history_vector_GetAt,
    history_vector_get_Size,
    history_vector_IndexOf,
    history_vector_GetMany,
};

static HRESULT WINAPI history_iterable_QueryInterface(
        __FIIterable_1_Windows__CUI__CNotifications__CToastNotification *iface, REFIID iid, void **out )
{
    struct toast_snapshot_vector *impl = impl_from_FIIterable_ToastNotification( iface );

    return IUnknown_QueryInterface( (IUnknown *)&impl->
            __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_iface, iid, out );
}

static ULONG WINAPI history_iterable_AddRef(
        __FIIterable_1_Windows__CUI__CNotifications__CToastNotification *iface )
{
    struct toast_snapshot_vector *impl = impl_from_FIIterable_ToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI history_iterable_Release(
        __FIIterable_1_Windows__CUI__CNotifications__CToastNotification *iface )
{
    struct toast_snapshot_vector *impl = impl_from_FIIterable_ToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        UINT32 i;

        TRACE( "destroying %p\n", iface );
        for (i = 0; i < impl->size; ++i) IToastNotification_Release( impl->items[i] );
        free( impl->items );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI history_iterable_GetIids(
        __FIIterable_1_Windows__CUI__CNotifications__CToastNotification *iface, ULONG *iid_count, IID **iids )
{
    static const IID *const iids_arr[] =
    {
        &IID___FIVectorView_1_Windows__CUI__CNotifications__CToastNotification,
        &IID___FIIterable_1_Windows__CUI__CNotifications__CToastNotification,
    };

    if (!iid_count) return E_POINTER;
    return notifications_get_iids( iids_arr, ARRAY_SIZE( iids_arr ), iid_count, iids );
}

static HRESULT WINAPI history_iterable_GetRuntimeClassName(
        __FIIterable_1_Windows__CUI__CNotifications__CToastNotification *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterable`1<Windows.UI.Notifications.ToastNotification>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI history_iterable_GetTrustLevel(
        __FIIterable_1_Windows__CUI__CNotifications__CToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI history_iterable_First(
        __FIIterable_1_Windows__CUI__CNotifications__CToastNotification *iface,
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification **result )
{
    struct toast_snapshot_vector *vector = impl_from_FIIterable_ToastNotification( iface );
    struct toast_snapshot_iterator *impl;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->__FIIterator_1_Windows__CUI__CNotifications__CToastNotification_iface.lpVtbl = &history_iterator_vtbl;
    impl->vector = vector;
    InterlockedIncrement( &vector->ref );
    impl->index = -1;
    impl->ref = 1;
    *result = &impl->__FIIterator_1_Windows__CUI__CNotifications__CToastNotification_iface;
    return S_OK;
}

static const struct __FIIterable_1_Windows__CUI__CNotifications__CToastNotificationVtbl history_iterable_vtbl =
{
    history_iterable_QueryInterface,
    history_iterable_AddRef,
    history_iterable_Release,
    /* IInspectable methods */
    history_iterable_GetIids,
    history_iterable_GetRuntimeClassName,
    history_iterable_GetTrustLevel,
    /* IIterable<ToastNotification> methods */
    history_iterable_First,
};

static HRESULT WINAPI history_iterator_QueryInterface(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIIterator_1_Windows__CUI__CNotifications__CToastNotification ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI history_iterator_AddRef(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface )
{
    struct toast_snapshot_iterator *impl = impl_from_FIIterator_ToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI history_iterator_Release(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface )
{
    struct toast_snapshot_iterator *impl = impl_from_FIIterator_ToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        InterlockedDecrement( &impl->vector->ref );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI history_iterator_GetIids(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI history_iterator_GetRuntimeClassName(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterator`1<Windows.UI.Notifications.ToastNotification>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI history_iterator_GetTrustLevel(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI history_iterator_get_Current(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification **value )
{
    struct toast_snapshot_iterator *impl = impl_from_FIIterator_ToastNotification( iface );

    if (!value) return E_POINTER;
    if (impl->index < 0 || (UINT32)impl->index >= impl->vector->size) return E_BOUNDS;
    *value = impl->vector->items[impl->index];
    IToastNotification_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI history_iterator_get_HasCurrent(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface, BOOLEAN *value )
{
    struct toast_snapshot_iterator *impl = impl_from_FIIterator_ToastNotification( iface );

    if (!value) return E_POINTER;
    *value = (impl->index >= 0 && (UINT32)impl->index < impl->vector->size);
    return S_OK;
}

static HRESULT WINAPI history_iterator_MoveNext(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface, BOOLEAN *value )
{
    struct toast_snapshot_iterator *impl = impl_from_FIIterator_ToastNotification( iface );

    if (!value) return E_POINTER;
    ++impl->index;
    *value = (impl->index < (INT32)impl->vector->size);
    return S_OK;
}

static HRESULT WINAPI history_iterator_GetMany(
        __FIIterator_1_Windows__CUI__CNotifications__CToastNotification *iface, UINT32 size,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotification **items, UINT32 *value )
{
    struct toast_snapshot_iterator *impl = impl_from_FIIterator_ToastNotification( iface );
    struct toast_snapshot_vector *vector = impl->vector;
    UINT32 i = 0;

    if (!value) return E_POINTER;
    if (items && impl->index >= 0)
    {
        for (; i < size && (UINT32)impl->index < vector->size; ++i)
        {
            items[i] = vector->items[impl->index];
            IToastNotification_AddRef( items[i] );
            ++impl->index;
        }
        if (i) --impl->index;   /* leave Current on the last item handed out */
    }
    *value = i;
    return S_OK;
}

static const struct __FIIterator_1_Windows__CUI__CNotifications__CToastNotificationVtbl history_iterator_vtbl =
{
    history_iterator_QueryInterface,
    history_iterator_AddRef,
    history_iterator_Release,
    /* IInspectable methods */
    history_iterator_GetIids,
    history_iterator_GetRuntimeClassName,
    history_iterator_GetTrustLevel,
    /* IIterator<ToastNotification> methods */
    history_iterator_get_Current,
    history_iterator_get_HasCurrent,
    history_iterator_MoveNext,
    history_iterator_GetMany,
};

/* Snapshot of the current history, as IVectorView<ToastNotification>. */
static HRESULT toast_history_snapshot(
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification **out )
{
    struct toast_snapshot_vector *impl;
    struct toast_history_entry *entry;
    UINT32 i = 0, size = 0;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->__FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_iface.lpVtbl = &history_vector_vtbl;
    impl->__FIIterable_1_Windows__CUI__CNotifications__CToastNotification_iface.lpVtbl = &history_iterable_vtbl;
    impl->ref = 1;

    EnterCriticalSection( &toast_state_cs );
    LIST_FOR_EACH_ENTRY( entry, &toast_history, struct toast_history_entry, entry ) ++size;
    impl->size = size;
    if (size)
    {
        if (!(impl->items = malloc( size * sizeof(*impl->items) )))
        {
            LeaveCriticalSection( &toast_state_cs );
            free( impl );
            return E_OUTOFMEMORY;
        }
        LIST_FOR_EACH_ENTRY( entry, &toast_history, struct toast_history_entry, entry )
        {
            impl->items[i++] = entry->toast;
            IToastNotification_AddRef( entry->toast );
        }
    }
    LeaveCriticalSection( &toast_state_cs );

    *out = &impl->__FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_iface;
    return S_OK;
}

/* Snapshot of the shown-toast queue (oldest shown first), same vector object
 * the history snapshot answers: IVectorView + closed IIterable pinterface. */
static HRESULT toast_shown_queue_snapshot( __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification **out )
{
    struct toast_snapshot_vector *impl;
    struct toast_shown_entry *entry;
    UINT32 i = 0;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->__FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_iface.lpVtbl = &history_vector_vtbl;
    impl->__FIIterable_1_Windows__CUI__CNotifications__CToastNotification_iface.lpVtbl = &history_iterable_vtbl;
    impl->ref = 1;

    EnterCriticalSection( &toast_state_cs );
    impl->size = shown_queue_size;
    if (impl->size)
    {
        if (!(impl->items = malloc( impl->size * sizeof(*impl->items) )))
        {
            LeaveCriticalSection( &toast_state_cs );
            free( impl );
            return E_OUTOFMEMORY;
        }
        LIST_FOR_EACH_ENTRY( entry, &shown_queue, struct toast_shown_entry, entry )
        {
            impl->items[i++] = entry->toast;
            IToastNotification_AddRef( entry->toast );
        }
    }
    LeaveCriticalSection( &toast_state_cs );

    *out = &impl->__FIVectorView_1_Windows__CUI__CNotifications__CToastNotification_iface;
    return S_OK;
}

/*
 * The history object itself: Windows hands every caller of ToastNotificationManager.History
 * (or ToastNotificationManagerForUser.History) the app's one history store, so this is a
 * process-wide singleton, kept alive like the other static objects of the file.
 */
struct toast_notification_history
{
    IToastNotificationHistory IToastNotificationHistory_iface;
    IToastNotificationHistory2 IToastNotificationHistory2_iface;
    LONG ref;
};

static inline struct toast_notification_history *impl_from_IToastNotificationHistory(
        IToastNotificationHistory *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_history, IToastNotificationHistory_iface );
}

static inline struct toast_notification_history *impl_from_IToastNotificationHistory2(
        IToastNotificationHistory2 *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_history, IToastNotificationHistory2_iface );
}

static HRESULT WINAPI toast_notification_history_QueryInterface( IToastNotificationHistory *iface,
        REFIID iid, void **out )
{
    struct toast_notification_history *impl = impl_from_IToastNotificationHistory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IToastNotificationHistory ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationHistory2 ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IToastNotificationHistory2_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_notification_history_AddRef( IToastNotificationHistory *iface )
{
    struct toast_notification_history *impl = impl_from_IToastNotificationHistory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_notification_history_Release( IToastNotificationHistory *iface )
{
    struct toast_notification_history *impl = impl_from_IToastNotificationHistory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI toast_notification_history_GetIids( IToastNotificationHistory *iface,
        ULONG *iid_count, IID **iids )
{
    static const IID *const iids_arr[] =
    {
        &IID_IToastNotificationHistory,
        &IID_IToastNotificationHistory2,
    };

    if (!iid_count) return E_POINTER;
    return notifications_get_iids( iids_arr, ARRAY_SIZE( iids_arr ), iid_count, iids );
}

static HRESULT WINAPI toast_notification_history_GetRuntimeClassName( IToastNotificationHistory *iface,
        HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotificationHistory,
            ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotificationHistory ) - 1, class_name );
}

static HRESULT WINAPI toast_notification_history_GetTrustLevel( IToastNotificationHistory *iface,
        TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI toast_notification_history_RemoveGroup( IToastNotificationHistory *iface, HSTRING group )
{
    TRACE( "iface %p, group %s.\n", iface, debugstr_hstring( group ) );
    toast_history_prune( NULL, group );
    return S_OK;
}

static HRESULT WINAPI toast_notification_history_RemoveGroupWithId( IToastNotificationHistory *iface,
        HSTRING group, HSTRING application_id )
{
    TRACE( "iface %p, group %s, application_id %s.\n", iface, debugstr_hstring( group ),
            debugstr_hstring( application_id ) );
    toast_history_prune( NULL, group );
    return S_OK;
}

static HRESULT WINAPI toast_notification_history_RemoveGroupedTagWithId( IToastNotificationHistory *iface,
        HSTRING tag, HSTRING group, HSTRING application_id )
{
    TRACE( "iface %p, tag %s, group %s, application_id %s.\n", iface, debugstr_hstring( tag ),
            debugstr_hstring( group ), debugstr_hstring( application_id ) );
    toast_history_prune( tag, group );
    return S_OK;
}

static HRESULT WINAPI toast_notification_history_RemoveGroupedTag( IToastNotificationHistory *iface,
        HSTRING tag, HSTRING group )
{
    TRACE( "iface %p, tag %s, group %s.\n", iface, debugstr_hstring( tag ), debugstr_hstring( group ) );
    toast_history_prune( tag, group );
    return S_OK;
}

static HRESULT WINAPI toast_notification_history_Remove( IToastNotificationHistory *iface, HSTRING tag )
{
    TRACE( "iface %p, tag %s.\n", iface, debugstr_hstring( tag ) );
    toast_history_prune( tag, NULL );
    return S_OK;
}

static HRESULT WINAPI toast_notification_history_Clear( IToastNotificationHistory *iface )
{
    TRACE( "iface %p.\n", iface );
    toast_history_prune( NULL, NULL );
    return S_OK;
}

static HRESULT WINAPI toast_notification_history_ClearWithId( IToastNotificationHistory *iface,
        HSTRING application_id )
{
    TRACE( "iface %p, application_id %s.\n", iface, debugstr_hstring( application_id ) );
    toast_history_prune( NULL, NULL );
    return S_OK;
}

static const struct IToastNotificationHistoryVtbl toast_notification_history_vtbl =
{
    toast_notification_history_QueryInterface,
    toast_notification_history_AddRef,
    toast_notification_history_Release,
    /* IInspectable methods */
    toast_notification_history_GetIids,
    toast_notification_history_GetRuntimeClassName,
    toast_notification_history_GetTrustLevel,
    /* IToastNotificationHistory methods */
    toast_notification_history_RemoveGroup,
    toast_notification_history_RemoveGroupWithId,
    toast_notification_history_RemoveGroupedTagWithId,
    toast_notification_history_RemoveGroupedTag,
    toast_notification_history_Remove,
    toast_notification_history_Clear,
    toast_notification_history_ClearWithId,
};

FORWARD_IINSPECTABLE( toast_notification_history2, IToastNotificationHistory2, struct toast_notification_history,
        impl_from_IToastNotificationHistory2, &impl->IToastNotificationHistory_iface )

static HRESULT WINAPI toast_notification_history2_GetHistory( IToastNotificationHistory2 *iface,
        __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return toast_history_snapshot( result );
}

static HRESULT WINAPI toast_notification_history2_GetHistoryWithId( IToastNotificationHistory2 *iface,
        HSTRING application_id, __FIVectorView_1_Windows__CUI__CNotifications__CToastNotification **result )
{
    TRACE( "iface %p, application_id %s, result %p.\n", iface, debugstr_hstring( application_id ), result );
    return toast_history_snapshot( result );
}

static const struct IToastNotificationHistory2Vtbl toast_notification_history2_vtbl =
{
    toast_notification_history2_QueryInterface,
    toast_notification_history2_AddRef,
    toast_notification_history2_Release,
    /* IInspectable methods */
    toast_notification_history2_GetIids,
    toast_notification_history2_GetRuntimeClassName,
    toast_notification_history2_GetTrustLevel,
    /* IToastNotificationHistory2 methods */
    toast_notification_history2_GetHistory,
    toast_notification_history2_GetHistoryWithId,
};

static struct toast_notification_history toast_notification_history =
{
    { &toast_notification_history_vtbl },
    { &toast_notification_history2_vtbl },
    1
};

static HRESULT create_toast_notification_history( IToastNotificationHistory **out )
{
    if (!out) return E_POINTER;
    IToastNotificationHistory_AddRef( &toast_notification_history.IToastNotificationHistory_iface );
    *out = &toast_notification_history.IToastNotificationHistory_iface;
    return S_OK;
}

struct toast_notification_manager_statics
{
    IActivationFactory IActivationFactory_iface;
    IToastNotificationManagerStatics IToastNotificationManagerStatics_iface;
    IToastNotificationManagerStatics2 IToastNotificationManagerStatics2_iface;
    IToastNotificationManagerStatics4 IToastNotificationManagerStatics4_iface;
    IToastNotificationManagerStatics5 IToastNotificationManagerStatics5_iface;
    LONG ref;
};

static inline struct toast_notification_manager_statics *implTUM_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_manager_statics, IActivationFactory_iface );
}

static HRESULT WINAPI toast_notification_manager_statics_factory_QueryInterface( IActivationFactory *iface,
        REFIID iid, void **out )
{
    struct toast_notification_manager_statics *impl = implTUM_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationManagerStatics ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationManagerStatics_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationManagerStatics2 ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationManagerStatics2_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationManagerStatics4 ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationManagerStatics4_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationManagerStatics5 ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationManagerStatics5_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_notification_manager_statics_factory_AddRef( IActivationFactory *iface )
{
    struct toast_notification_manager_statics *impl = implTUM_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_notification_manager_statics_factory_Release( IActivationFactory *iface )
{
    struct toast_notification_manager_statics *impl = implTUM_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI toast_notification_manager_statics_factory_GetIids( IActivationFactory *iface,
        ULONG *iid_count, IID **iids )
{
    static const IID *const iids_arr[] =
    {
        &IID_IToastNotificationManagerStatics,
        &IID_IToastNotificationManagerStatics2,
        &IID_IToastNotificationManagerStatics4,
        &IID_IToastNotificationManagerStatics5,
    };

    if (!iid_count) return E_POINTER;
    return notifications_get_iids( iids_arr, ARRAY_SIZE( iids_arr ), iid_count, iids );
}

static HRESULT WINAPI toast_notification_manager_statics_factory_GetRuntimeClassName( IActivationFactory *iface,
        HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotificationManager,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotificationManager ) - 1,
                                class_name );
}

static HRESULT WINAPI toast_notification_manager_statics_factory_GetTrustLevel( IActivationFactory *iface,
        TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI toast_notification_manager_statics_factory_ActivateInstance( IActivationFactory *iface,
        IInspectable **instance )
{
    /* ToastNotificationManager exposes only static entry points. */
    FIXME( "iface %p, instance %p.\n", iface, instance );
    return E_NOINTERFACE;
}

static const struct IActivationFactoryVtbl toast_notification_manager_statics_factory_vtbl =
{
    toast_notification_manager_statics_factory_QueryInterface,
    toast_notification_manager_statics_factory_AddRef,
    toast_notification_manager_statics_factory_Release,
    /* IInspectable methods */
    toast_notification_manager_statics_factory_GetIids,
    toast_notification_manager_statics_factory_GetRuntimeClassName,
    toast_notification_manager_statics_factory_GetTrustLevel,
    /* IActivationFactory methods */
    toast_notification_manager_statics_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( toast_notification_manager_statics, IToastNotificationManagerStatics,
                     struct toast_notification_manager_statics, IActivationFactory_iface )

static HRESULT WINAPI toast_notification_manager_statics_CreateToastNotifier(
        IToastNotificationManagerStatics *iface, __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_toast_notifier( result );
}

static HRESULT WINAPI toast_notification_manager_statics_CreateToastNotifierWithId(
        IToastNotificationManagerStatics *iface, HSTRING application_id,
        __x_ABI_CWindows_CUI_CNotifications_CIToastNotifier **result )
{
    TRACE( "iface %p, application_id %s, result %p.\n", iface, debugstr_hstring( application_id ), result );
    return create_toast_notifier( result );
}

static HRESULT WINAPI toast_notification_manager_statics_GetTemplateContent(
        IToastNotificationManagerStatics *iface, __x_ABI_CWindows_CUI_CNotifications_CToastTemplateType type,
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument **result )
{
    TRACE( "iface %p, type %d, result %p.\n", iface, type, result );

    if (!result) return E_POINTER;
    if (type > ToastTemplateType_ToastText04) return E_INVALIDARG;
    return toast_create_xml_document( toast_template_xml[type], result );
}

static const struct IToastNotificationManagerStaticsVtbl toast_notification_manager_statics_vtbl =
{
    toast_notification_manager_statics_QueryInterface,
    toast_notification_manager_statics_AddRef,
    toast_notification_manager_statics_Release,
    /* IInspectable methods */
    toast_notification_manager_statics_GetIids,
    toast_notification_manager_statics_GetRuntimeClassName,
    toast_notification_manager_statics_GetTrustLevel,
    /* IToastNotificationManagerStatics methods */
    toast_notification_manager_statics_CreateToastNotifier,
    toast_notification_manager_statics_CreateToastNotifierWithId,
    toast_notification_manager_statics_GetTemplateContent,
};

/*
 * The later static interfaces of the class (windows.ui.notifications.idl:
 * IToastNotificationManagerStatics2 7ab93c52-0e48-4750-ba9d-1a4113981847:1280,
 * IToastNotificationManagerStatics4 8f993fd3-e516-45fb-8130-398e93fa52c3:1290,
 * IToastNotificationManagerStatics5 d6f5f569-d40d-407c-8989-88cab42cfd14:1301).
 * The .NET projection of the notification path reaches its notifier through
 * GetDefault()/GetForUser(), so those hand out the per-user manager; History()
 * hands out the app-wide toast store kept by IToastNotifier::Show above.
 */
static inline struct toast_notification_manager_statics *impl_from_IToastNotificationManagerStatics2(
        IToastNotificationManagerStatics2 *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_manager_statics,
            IToastNotificationManagerStatics2_iface );
}

static inline struct toast_notification_manager_statics *impl_from_IToastNotificationManagerStatics4(
        IToastNotificationManagerStatics4 *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_manager_statics,
            IToastNotificationManagerStatics4_iface );
}

static inline struct toast_notification_manager_statics *impl_from_IToastNotificationManagerStatics5(
        IToastNotificationManagerStatics5 *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_manager_statics,
            IToastNotificationManagerStatics5_iface );
}

static HRESULT create_toast_notification_manager_for_user( IToastNotificationManagerForUser **out );

FORWARD_IINSPECTABLE( toast_notification_manager_statics2, IToastNotificationManagerStatics2, struct toast_notification_manager_statics, impl_from_IToastNotificationManagerStatics2, &impl->IActivationFactory_iface )
FORWARD_IINSPECTABLE( toast_notification_manager_statics4, IToastNotificationManagerStatics4, struct toast_notification_manager_statics, impl_from_IToastNotificationManagerStatics4, &impl->IActivationFactory_iface )
FORWARD_IINSPECTABLE( toast_notification_manager_statics5, IToastNotificationManagerStatics5, struct toast_notification_manager_statics, impl_from_IToastNotificationManagerStatics5, &impl->IActivationFactory_iface )

static HRESULT WINAPI toast_notification_manager_statics2_get_History(
        IToastNotificationManagerStatics2 *iface, IToastNotificationHistory **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return create_toast_notification_history( value );
}

static const struct IToastNotificationManagerStatics2Vtbl toast_notification_manager_statics2_vtbl =
{
    toast_notification_manager_statics2_QueryInterface,
    toast_notification_manager_statics2_AddRef,
    toast_notification_manager_statics2_Release,
    /* IInspectable methods */
    toast_notification_manager_statics2_GetIids,
    toast_notification_manager_statics2_GetRuntimeClassName,
    toast_notification_manager_statics2_GetTrustLevel,
    /* IToastNotificationManagerStatics2 methods */
    toast_notification_manager_statics2_get_History,
};

static HRESULT WINAPI toast_notification_manager_statics4_GetForUser( IToastNotificationManagerStatics4 *iface,
        __x_ABI_CWindows_CSystem_CIUser *user, IToastNotificationManagerForUser **result )
{
    TRACE( "iface %p, user %p, result %p.\n", iface, user, result );

    /* A prefix has one interactive user and no user identity to key on, so every
     * user's manager is the same functional surface.
 */
    return create_toast_notification_manager_for_user( result );
}

static HRESULT WINAPI toast_notification_manager_statics4_ConfigureNotificationMirroring(
        IToastNotificationManagerStatics4 *iface, NotificationMirroring value )
{
    /* Nothing mirrors toasts from here; accepting the setting is the outcome. */
    TRACE( "iface %p, value %d.\n", iface, value );
    return S_OK;
}

static const struct IToastNotificationManagerStatics4Vtbl toast_notification_manager_statics4_vtbl =
{
    toast_notification_manager_statics4_QueryInterface,
    toast_notification_manager_statics4_AddRef,
    toast_notification_manager_statics4_Release,
    /* IInspectable methods */
    toast_notification_manager_statics4_GetIids,
    toast_notification_manager_statics4_GetRuntimeClassName,
    toast_notification_manager_statics4_GetTrustLevel,
    /* IToastNotificationManagerStatics4 methods */
    toast_notification_manager_statics4_GetForUser,
    toast_notification_manager_statics4_ConfigureNotificationMirroring,
};

static HRESULT WINAPI toast_notification_manager_statics5_GetDefault(
        IToastNotificationManagerStatics5 *iface, IToastNotificationManagerForUser **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_toast_notification_manager_for_user( result );
}

static const struct IToastNotificationManagerStatics5Vtbl toast_notification_manager_statics5_vtbl =
{
    toast_notification_manager_statics5_QueryInterface,
    toast_notification_manager_statics5_AddRef,
    toast_notification_manager_statics5_Release,
    /* IInspectable methods */
    toast_notification_manager_statics5_GetIids,
    toast_notification_manager_statics5_GetRuntimeClassName,
    toast_notification_manager_statics5_GetTrustLevel,
    /* IToastNotificationManagerStatics5 methods */
    toast_notification_manager_statics5_GetDefault,
};

/*
 * Windows.UI.Notifications.ToastNotificationManagerForUser
 * (IToastNotificationManagerForUser 79ab57f6-43fe-487b-8a7f-99567200ae94:1228):
 * the per-user notifier factory.  History() hands out the same app-wide toast
 * store as the statics; the user accessor reports E_NOTIMPL, the honest marker
 * for the identity this prefix has no answer for.
 */
struct toast_notification_manager_for_user
{
    IToastNotificationManagerForUser IToastNotificationManagerForUser_iface;
    LONG ref;
};

static inline struct toast_notification_manager_for_user *impl_from_IToastNotificationManagerForUser(
        IToastNotificationManagerForUser *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_manager_for_user,
            IToastNotificationManagerForUser_iface );
}

static HRESULT WINAPI for_user_QueryInterface( IToastNotificationManagerForUser *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IToastNotificationManagerForUser ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI for_user_AddRef( IToastNotificationManagerForUser *iface )
{
    struct toast_notification_manager_for_user *impl = impl_from_IToastNotificationManagerForUser( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI for_user_Release( IToastNotificationManagerForUser *iface )
{
    struct toast_notification_manager_for_user *impl = impl_from_IToastNotificationManagerForUser( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI for_user_GetIids( IToastNotificationManagerForUser *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI for_user_GetRuntimeClassName( IToastNotificationManagerForUser *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotificationManagerForUser,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotificationManagerForUser ) - 1,
                                class_name );
}

static HRESULT WINAPI for_user_GetTrustLevel( IToastNotificationManagerForUser *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI for_user_CreateToastNotifier( IToastNotificationManagerForUser *iface,
        IToastNotifier **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_toast_notifier( result );
}

static HRESULT WINAPI for_user_CreateToastNotifierWithId( IToastNotificationManagerForUser *iface,
        HSTRING application_id, IToastNotifier **result )
{
    TRACE( "iface %p, application_id %s, result %p.\n", iface, debugstr_hstring( application_id ), result );
    return create_toast_notifier( result );
}

static HRESULT WINAPI for_user_get_History( IToastNotificationManagerForUser *iface,
        IToastNotificationHistory **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    return create_toast_notification_history( value );
}

static HRESULT WINAPI for_user_get_User( IToastNotificationManagerForUser *iface,
        __x_ABI_CWindows_CSystem_CIUser **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static const struct IToastNotificationManagerForUserVtbl for_user_vtbl =
{
    for_user_QueryInterface,
    for_user_AddRef,
    for_user_Release,
    /* IInspectable methods */
    for_user_GetIids,
    for_user_GetRuntimeClassName,
    for_user_GetTrustLevel,
    /* IToastNotificationManagerForUser methods */
    for_user_CreateToastNotifier,
    for_user_CreateToastNotifierWithId,
    for_user_get_History,
    for_user_get_User,
};

static HRESULT create_toast_notification_manager_for_user( IToastNotificationManagerForUser **out )
{
    struct toast_notification_manager_for_user *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IToastNotificationManagerForUser_iface.lpVtbl = &for_user_vtbl;
    impl->ref = 1;
    *out = &impl->IToastNotificationManagerForUser_iface;
    return S_OK;
}

/*
 * Windows.UI.Notifications.ToastNotification
 * (IToastNotification 997e2675-059e-4e60-8b06-1760917c8b80:1081, IToastNotification2
 * 9dfb9fd1-143a-490e-90bf-b9fba7132de7:1105, IToastNotificationFactory
 * 04124b20-82c6-4229-b109-fd9ed4662b53:1168).
 *
 * The notification is the content document the notification path built (kept by
 * reference, so get_Content reports the same object it was handed) plus the
 * tag/group/suppression state the tray path reads back.  The dismissal,
 * activation and failure events are accepted and never fire: nothing is
 * delivered, so a handler registered here has nothing to report to.
 */
static inline struct toast_notification *impl_from_IToastNotification2( IToastNotification2 *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification, IToastNotification2_iface );
}

static UINT32 toast_snapshot_sinks( struct list *sinks, IUnknown ***out )
{
    struct toast_event_sink *sink;
    IUnknown **arr = NULL;
    UINT32 count = 0, i = 0;

    EnterCriticalSection( &toast_state_cs );
    LIST_FOR_EACH_ENTRY( sink, sinks, struct toast_event_sink, entry ) count++;
    if (count && (arr = malloc( count * sizeof(*arr) )))
    {
        LIST_FOR_EACH_ENTRY( sink, sinks, struct toast_event_sink, entry )
        {
            arr[i] = sink->handler;
            IUnknown_AddRef( arr[i] );
            i++;
        }
    }
    else
    {
        count = 0;
    }
    LeaveCriticalSection( &toast_state_cs );
    *out = arr;
    return count;
}

static void toast_notification_fire_activated( struct toast_notification *impl )
{
    __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler;
    struct toast_activated_args *args;
    IUnknown **handlers = NULL;
    UINT32 count, i;

    count = toast_snapshot_sinks( &impl->activated_sinks, &handlers );
    if (!count) return;
    if (SUCCEEDED( create_toast_activated_args( impl->launch_args, &args ) ))
    {
        for (i = 0; i < count; i++)
        {
            handler = (void *)handlers[i];
            handler->lpVtbl->Invoke( handler, &impl->IToastNotification_iface,
                                     (IInspectable *)&args->IToastActivatedEventArgs_iface );
        }
        IUnknown_Release( (IUnknown *)&args->IToastActivatedEventArgs_iface );
    }
    for (i = 0; i < count; i++) IUnknown_Release( handlers[i] );
    free( handlers );
}

static void toast_notification_fire_dismissed( struct toast_notification *impl,
        __x_ABI_CWindows_CUI_CNotifications_CToastDismissalReason reason )
{
    __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastDismissedEventArgs *handler;
    struct toast_dismissed_args *args;
    IUnknown **handlers = NULL;
    UINT32 count, i;

    count = toast_snapshot_sinks( &impl->dismissed_sinks, &handlers );
    if (!count) return;
    if (SUCCEEDED( create_toast_dismissed_args( reason, &args ) ))
    {
        for (i = 0; i < count; i++)
        {
            handler = (void *)handlers[i];
            handler->lpVtbl->Invoke( handler, &impl->IToastNotification_iface,
                                     &args->IToastDismissedEventArgs_iface );
        }
        IUnknown_Release( (IUnknown *)&args->IToastDismissedEventArgs_iface );
    }
    for (i = 0; i < count; i++) IUnknown_Release( handlers[i] );
    free( handlers );
}

/* NotificationSuppressed (TypedEventHandler<ToastNotification *, IInspectable *>):
 * fired when a Show() arrives while the toast is flagged SuppressPopup, the
 * app-launched-over-visible-toast case Windows reports through this event.
 * The args carry the launch arguments, as the activation args do. */
static void toast_notification_fire_suppressed( struct toast_notification *impl )
{
    __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler;
    struct toast_activated_args *args;
    IUnknown **handlers = NULL;
    UINT32 count, i;

    count = toast_snapshot_sinks( &impl->suppressed_sinks, &handlers );
    if (!count) return;
    if (SUCCEEDED( create_toast_activated_args( impl->launch_args, &args ) ))
    {
        for (i = 0; i < count; i++)
        {
            handler = (void *)handlers[i];
            handler->lpVtbl->Invoke( handler, &impl->IToastNotification_iface,
                                     (IInspectable *)&args->IToastActivatedEventArgs_iface );
        }
        IUnknown_Release( (IUnknown *)&args->IToastActivatedEventArgs_iface );
    }
    for (i = 0; i < count; i++) IUnknown_Release( handlers[i] );
    free( handlers );
}

/*
 * Pull every queued FreeDesktop Notifications signal and replay it onto the
 * ToastNotification shown under the matching daemon id: ActionInvoked fires
 * Activated (with the stored launch arguments), NotificationClosed fires
 * Dismissed with the mapped reason (expired→TimedOut, user→User,
 * app-closed→ApplicationInitiated).
 */
static void toast_notifier_drain_events( void )
{
    static const __x_ABI_CWindows_CUI_CNotifications_CToastDismissalReason reason_map[] =
    {
        ToastDismissalReason_UserCanceled,
        ToastDismissalReason_TimedOut,           /* 1: expired */
        ToastDismissalReason_UserCanceled,       /* 2: user clicked */
        ToastDismissalReason_ApplicationHidden   /* 3: closed */
    };
    struct fd_event_params params;
    struct toast_notification *impl;

    while (TWINAPI_CALL( fd_event, &params ) == STATUS_SUCCESS && params.got_event)
    {
        struct toast_notification *matched = NULL;
        EnterCriticalSection( &toast_state_cs );
        LIST_FOR_EACH_ENTRY( impl, &shown_toasts, struct toast_notification, shown_entry )
        {
            if (impl->fd_id != params.id) continue;
            matched = impl;
            IToastNotification_AddRef( &matched->IToastNotification_iface );
            break;
        }
        LeaveCriticalSection( &toast_state_cs );
        if (matched)
        {
            if (params.reason == 2) toast_notification_fire_activated( matched );
            else if (params.reason < ARRAY_SIZE( reason_map ))
                toast_notification_fire_dismissed( matched, reason_map[params.reason] );
            IToastNotification_Release( &matched->IToastNotification_iface );
        }
    }
}

static HRESULT WINAPI toast_notification_QueryInterface( IToastNotification *iface, REFIID iid, void **out )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IToastNotification ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotification2 ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotification2_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationSuppressed ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationSuppressed_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationTemplate ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationTemplate_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_notification_AddRef( IToastNotification *iface )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_notification_Release( IToastNotification *iface )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        if (impl->content) IInspectable_Release( (IInspectable *)impl->content );
        if (impl->expiration_time) IInspectable_Release( (IInspectable *)impl->expiration_time );
        toast_event_clear( &impl->activated_sinks );
        toast_event_clear( &impl->dismissed_sinks );
        toast_event_clear( &impl->failed_sinks );
        toast_event_clear( &impl->suppressed_sinks );
        EnterCriticalSection( &toast_state_cs );
        if (!list_empty( &impl->shown_entry ))
        {
            list_remove( &impl->shown_entry );
            list_init( &impl->shown_entry );
        }
        LeaveCriticalSection( &toast_state_cs );
        WindowsDeleteString( impl->tag );
        WindowsDeleteString( impl->group );
        WindowsDeleteString( impl->launch_args );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI toast_notification_GetIids( IToastNotification *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI toast_notification_GetRuntimeClassName( IToastNotification *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotification,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotification ) - 1,
                                class_name );
}

static HRESULT WINAPI toast_notification_GetTrustLevel( IToastNotification *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI toast_notification_get_Content( IToastNotification *iface,
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument **value )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!impl->content) return E_FAIL;
    IInspectable_AddRef( (IInspectable *)impl->content );
    *value = impl->content;
    return S_OK;
}

static HRESULT WINAPI toast_notification_put_ExpirationTime( IToastNotification *iface,
        __FIReference_1_DateTime *value )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (value) IInspectable_AddRef( (IInspectable *)value );
    if (impl->expiration_time) IInspectable_Release( (IInspectable *)impl->expiration_time );
    impl->expiration_time = value;
    return S_OK;
}

static HRESULT WINAPI toast_notification_get_ExpirationTime( IToastNotification *iface,
        __FIReference_1_DateTime **value )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->expiration_time) IInspectable_AddRef( (IInspectable *)impl->expiration_time );
    *value = impl->expiration_time;
    return S_OK;
}

/* The handlers are kept per event; the daemon events replay onto them. */
static HRESULT WINAPI toast_notification_add_event( IToastNotification *iface, struct list *sinks,
        IUnknown *handler, EventRegistrationToken *token )
{
    return toast_event_add( sinks, handler, token );
}

static HRESULT WINAPI toast_notification_Dismissed_add( IToastNotification *iface,
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastDismissedEventArgs *handler,
        EventRegistrationToken *token )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return toast_notification_add_event( iface, &impl->dismissed_sinks, (IUnknown *)handler, token );
}

static HRESULT WINAPI toast_notification_Activated_add( IToastNotification *iface,
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler,
        EventRegistrationToken *token )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return toast_notification_add_event( iface, &impl->activated_sinks, (IUnknown *)handler, token );
}

static HRESULT WINAPI toast_notification_Failed_add( IToastNotification *iface,
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastFailedEventArgs *handler,
        EventRegistrationToken *token )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return toast_notification_add_event( iface, &impl->failed_sinks, (IUnknown *)handler, token );
}

static HRESULT WINAPI toast_notification_Dismissed_remove( IToastNotification *iface,
        EventRegistrationToken token )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return toast_event_remove( &impl->dismissed_sinks, token );
}

static HRESULT WINAPI toast_notification_Activated_remove( IToastNotification *iface,
        EventRegistrationToken token )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return toast_event_remove( &impl->activated_sinks, token );
}

static HRESULT WINAPI toast_notification_Failed_remove( IToastNotification *iface,
        EventRegistrationToken token )
{
    struct toast_notification *impl = impl_from_IToastNotification( iface );

    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return toast_event_remove( &impl->failed_sinks, token );
}

static const struct IToastNotificationVtbl toast_notification_vtbl =
{
    toast_notification_QueryInterface,
    toast_notification_AddRef,
    toast_notification_Release,
    /* IInspectable methods */
    toast_notification_GetIids,
    toast_notification_GetRuntimeClassName,
    toast_notification_GetTrustLevel,
    /* IToastNotification methods */
    toast_notification_get_Content,
    toast_notification_put_ExpirationTime,
    toast_notification_get_ExpirationTime,
    toast_notification_Dismissed_add,
    toast_notification_Dismissed_remove,
    toast_notification_Activated_add,
    toast_notification_Activated_remove,
    toast_notification_Failed_add,
    toast_notification_Failed_remove,
};

FORWARD_IINSPECTABLE( toast_notification2, IToastNotification2, struct toast_notification,
                      impl_from_IToastNotification2, &impl->IToastNotification_iface )

/* The tag, group and suppression state of the notification round-trips; an
 * unset string reads back empty, as the projection reports it. */
static HRESULT WINAPI toast_notification2_put_Tag( IToastNotification2 *iface, HSTRING value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );
    HSTRING dup = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    if (value && FAILED( hr = WindowsDuplicateString( value, &dup ) )) return hr;
    WindowsDeleteString( impl->tag );
    impl->tag = dup;
    return S_OK;
}

static HRESULT WINAPI toast_notification2_get_Tag( IToastNotification2 *iface, HSTRING *value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->tag, value );
}

static HRESULT WINAPI toast_notification2_put_Group( IToastNotification2 *iface, HSTRING value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );
    HSTRING dup = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    if (value && FAILED( hr = WindowsDuplicateString( value, &dup ) )) return hr;
    WindowsDeleteString( impl->group );
    impl->group = dup;
    return S_OK;
}

static HRESULT WINAPI toast_notification2_get_Group( IToastNotification2 *iface, HSTRING *value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->group, value );
}

static HRESULT WINAPI toast_notification2_put_SuppressPopup( IToastNotification2 *iface, boolean value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );

    TRACE( "iface %p, value %d.\n", iface, value );

    impl->suppress_popup = value;
    return S_OK;
}

static HRESULT WINAPI toast_notification2_get_SuppressPopup( IToastNotification2 *iface, boolean *value )
{
    struct toast_notification *impl = impl_from_IToastNotification2( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = impl->suppress_popup;
    return S_OK;
}

static const struct IToastNotification2Vtbl toast_notification2_vtbl =
{
    toast_notification2_QueryInterface,
    toast_notification2_AddRef,
    toast_notification2_Release,
    /* IInspectable methods */
    toast_notification2_GetIids,
    toast_notification2_GetRuntimeClassName,
    toast_notification2_GetTrustLevel,
    /* IToastNotification2 methods */
    toast_notification2_put_Tag,
    toast_notification2_get_Tag,
    toast_notification2_put_Group,
    toast_notification2_get_Group,
    toast_notification2_put_SuppressPopup,
    toast_notification2_get_SuppressPopup,
};

/* The NotificationSuppressed handlers join the per-event sink lists kept by
 * the Activated/Dismissed/Failed accessors: token-keyed, AddRef'd, replayed
 * from toast_notifier_Show. */
static inline struct toast_notification *impl_from_IToastNotificationSuppressed(
        IToastNotificationSuppressed *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification, IToastNotificationSuppressed_iface );
}

static inline struct toast_notification *impl_from_IToastNotificationTemplate(
        IToastNotificationTemplate *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification, IToastNotificationTemplate_iface );
}

FORWARD_IINSPECTABLE( toast_notification_suppressed, IToastNotificationSuppressed, struct toast_notification,
                      impl_from_IToastNotificationSuppressed, &impl->IToastNotification_iface )
FORWARD_IINSPECTABLE( toast_notification_template, IToastNotificationTemplate, struct toast_notification,
                      impl_from_IToastNotificationTemplate, &impl->IToastNotification_iface )

static HRESULT WINAPI toast_notification_Suppressed_add( IToastNotificationSuppressed *iface,
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler,
        EventRegistrationToken *token )
{
    struct toast_notification *impl = impl_from_IToastNotificationSuppressed( iface );

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return toast_event_add( &impl->suppressed_sinks, (IUnknown *)handler, token );
}

static HRESULT WINAPI toast_notification_Suppressed_remove( IToastNotificationSuppressed *iface,
        EventRegistrationToken token )
{
    struct toast_notification *impl = impl_from_IToastNotificationSuppressed( iface );

    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return toast_event_remove( &impl->suppressed_sinks, token );
}

static const struct IToastNotificationSuppressedVtbl toast_notification_suppressed_vtbl =
{
    toast_notification_suppressed_QueryInterface,
    toast_notification_suppressed_AddRef,
    toast_notification_suppressed_Release,
    /* IInspectable methods */
    toast_notification_suppressed_GetIids,
    toast_notification_suppressed_GetRuntimeClassName,
    toast_notification_suppressed_GetTrustLevel,
    /* IToastNotificationSuppressed methods */
    toast_notification_Suppressed_add,
    toast_notification_Suppressed_remove,
};

/*
 * The ToastContent/ToastTemplate readback of the parsed payload: the binding
 * template attribute names the layout, mapped onto the ToastTemplateType
 * values (ToastImageAndText01..04, ToastText01..04) with ToastGeneric — the
 * only template the ToastGeneric-era payloads use — answering a document with
 * no recognizable binding template, as GetTemplateContent defaults to it.
 */
static HRESULT WINAPI toast_notification_template_get_ToastTemplate( IToastNotificationTemplate *iface,
        INT32 *value )
{
    static const WCHAR *const template_names[] =
    {
        L"ToastImageAndText01", L"ToastImageAndText02", L"ToastImageAndText03", L"ToastImageAndText04",
        L"ToastText01", L"ToastText02", L"ToastText03", L"ToastText04",
    };
    struct toast_notification *impl = impl_from_IToastNotificationTemplate( iface );
    HSTRING binding = NULL;
    INT32 i;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = ToastTemplate_ToastGeneric;
    if (!impl->content) return S_OK;

    if (SUCCEEDED( toast_xml_first_attribute( impl->content, L"binding", L"template", &binding ) ) && binding)
    {
        const WCHAR *raw = WindowsGetStringRawBuffer( binding, NULL );
        for (i = 0; i < ARRAY_SIZE( template_names ); ++i)
        {
            if (raw && !lstrcmpW( raw, template_names[i] ))
            {
                *value = i;
                break;
            }
        }
    }
    WindowsDeleteString( binding );
    return S_OK;
}

static HRESULT WINAPI toast_notification_template_get_ToastContent( IToastNotificationTemplate *iface,
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument **value )
{
    struct toast_notification *impl = impl_from_IToastNotificationTemplate( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!impl->content) return E_FAIL;
    IInspectable_AddRef( (IInspectable *)impl->content );
    *value = impl->content;
    return S_OK;
}

static const struct IToastNotificationTemplateVtbl toast_notification_template_vtbl =
{
    toast_notification_template_QueryInterface,
    toast_notification_template_AddRef,
    toast_notification_template_Release,
    /* IInspectable methods */
    toast_notification_template_GetIids,
    toast_notification_template_GetRuntimeClassName,
    toast_notification_template_GetTrustLevel,
    /* IToastNotificationTemplate methods */
    toast_notification_template_get_ToastTemplate,
    toast_notification_template_get_ToastContent,
};

static HRESULT create_toast_notification( __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *content,
                                          IToastNotification **out )
{
    static const WCHAR emptyW[] = L"";
    struct toast_notification *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IToastNotification_iface.lpVtbl = &toast_notification_vtbl;
    impl->IToastNotification2_iface.lpVtbl = &toast_notification2_vtbl;
    impl->IToastNotificationSuppressed_iface.lpVtbl = &toast_notification_suppressed_vtbl;
    impl->IToastNotificationTemplate_iface.lpVtbl = &toast_notification_template_vtbl;
    impl->ref = 1;
    list_init( &impl->activated_sinks );
    list_init( &impl->dismissed_sinks );
    list_init( &impl->failed_sinks );
    list_init( &impl->suppressed_sinks );
    list_init( &impl->shown_entry );

    if (content)
    {
        IInspectable_AddRef( (IInspectable *)content );
        impl->content = content;
    }
    if (FAILED( hr = WindowsCreateString( emptyW, 0, &impl->tag ) ))
    {
        if (content) IInspectable_Release( (IInspectable *)content );
        free( impl );
        return hr;
    }
    if (FAILED( hr = WindowsCreateString( emptyW, 0, &impl->group ) ))
    {
        WindowsDeleteString( impl->tag );
        if (content) IInspectable_Release( (IInspectable *)content );
        free( impl );
        return hr;
    }
    *out = &impl->IToastNotification_iface;
    return S_OK;
}

/*
 * The class factory of Windows.UI.Notifications.ToastNotification: the class is
 * activatable with an interface (windows.ui.notifications.idl:1655), so its
 * IActivationFactory answers IToastNotificationFactory and has no parameterless
 * activation.
 */
struct toast_notification_factory
{
    IActivationFactory IActivationFactory_iface;
    IToastNotificationFactory IToastNotificationFactory_iface;
    LONG ref;
};

static inline struct toast_notification_factory *impl_from_toast_notification_factory(
        IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_factory, IActivationFactory_iface );
}

static inline struct toast_notification_factory *impl_from_IToastNotificationFactory(
        IToastNotificationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct toast_notification_factory, IToastNotificationFactory_iface );
}

static HRESULT WINAPI toast_notification_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct toast_notification_factory *impl = impl_from_toast_notification_factory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IToastNotificationFactory ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IToastNotificationFactory_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI toast_notification_factory_AddRef( IActivationFactory *iface )
{
    struct toast_notification_factory *impl = impl_from_toast_notification_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI toast_notification_factory_Release( IActivationFactory *iface )
{
    struct toast_notification_factory *impl = impl_from_toast_notification_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI toast_notification_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI toast_notification_factory_GetRuntimeClassName( IActivationFactory *iface,
        HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Notifications_ToastNotification,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Notifications_ToastNotification ) - 1,
                                class_name );
}

static HRESULT WINAPI toast_notification_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI toast_notification_factory_ActivateInstance( IActivationFactory *iface,
        IInspectable **instance )
{
    /* ToastNotification is activatable with an interface, not without arguments. */
    FIXME( "iface %p, instance %p.\n", iface, instance );
    return E_NOINTERFACE;
}

static const struct IActivationFactoryVtbl toast_notification_factory_vtbl =
{
    toast_notification_factory_QueryInterface,
    toast_notification_factory_AddRef,
    toast_notification_factory_Release,
    /* IInspectable methods */
    toast_notification_factory_GetIids,
    toast_notification_factory_GetRuntimeClassName,
    toast_notification_factory_GetTrustLevel,
    /* IActivationFactory methods */
    toast_notification_factory_ActivateInstance,
};

FORWARD_IINSPECTABLE( toast_notification_factory_statics, IToastNotificationFactory, struct toast_notification_factory, impl_from_IToastNotificationFactory, &impl->IActivationFactory_iface )

static HRESULT WINAPI toast_notification_factory_CreateToastNotification( IToastNotificationFactory *iface,
        __x_ABI_CWindows_CData_CXml_CDom_CIXmlDocument *content, IToastNotification **value )
{
    TRACE( "iface %p, content %p, value %p.\n", iface, content, value );
    return create_toast_notification( content, value );
}

static const struct IToastNotificationFactoryVtbl toast_notification_factory_toast_vtbl =
{
    toast_notification_factory_statics_QueryInterface,
    toast_notification_factory_statics_AddRef,
    toast_notification_factory_statics_Release,
    /* IInspectable methods */
    toast_notification_factory_statics_GetIids,
    toast_notification_factory_statics_GetRuntimeClassName,
    toast_notification_factory_statics_GetTrustLevel,
    /* IToastNotificationFactory methods */
    toast_notification_factory_CreateToastNotification,
};

static struct toast_notification_manager_statics toast_notification_manager_statics =
{
    { &toast_notification_manager_statics_factory_vtbl },
    { &toast_notification_manager_statics_vtbl },
    { &toast_notification_manager_statics2_vtbl },
    { &toast_notification_manager_statics4_vtbl },
    { &toast_notification_manager_statics5_vtbl },
    1
};

static struct toast_notification_factory toast_notification_factory_obj =
{
    { &toast_notification_factory_vtbl },
    { &toast_notification_factory_toast_vtbl },
    1
};

IActivationFactory *toast_notification_manager_statics_factory =
    &toast_notification_manager_statics.IActivationFactory_iface;

IActivationFactory *toast_notification_factory =
    &toast_notification_factory_obj.IActivationFactory_iface;
