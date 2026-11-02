/* WinRT Windows.Data.Xml.Dom.XmlDocument implementation
 *
 * The toast/notification path of the app builds a toast payload out of a
 * Windows.Data.Xml.Dom.XmlDocument (the class Windows hosts in
 * Windows.Data.Xml.dll) and hands it to IToastNotificationFactory.  Wine
 * answers the class from msxml6.dll, because msxml6.dll is this tree's MSXML
 * DOM provider: every method below is a thin wrapper over the msxml6 COM DOM
 * (CLSID_DOMDocument60, served by the module's own DllGetClassObject export,
 * which the spec forwards to msxml3.dll), so the document is a real DOM:
 * LoadXml/GetXml round-trip, XPath SelectNodes and node navigation all run on
 * the msxml parser.
 *
 * The interface IIDs and the vtable layout are the ones include/windows.data.xml.dom.idl
 * describes (IXmlDocument f7f3a506-1e87-42d6-bcfb-b8c809fa5494:197, IXmlDocumentIO
 * 6cd0e74e-ee65-4489-9ebf-ca43e87ba637:240, IXmlDocumentIO2 5d034661-7bd8-4ad5-9ebf-81e6347263b1:252,
 * IXmlDocumentStatics 5543d254-d757-4b79-9539-232b18f50bf1:266, IXmlNode
 * 1c741d59-2122-47d5-a856-83f3d4214875:388, IXmlNodeList 8c60ad77-83a4-4ec1-9c54-7ba429e13da6:422,
 * IXmlNodeSelector 63dbba8b-d0db-4fe1-b745-f9433afdc25b:434, IXmlNodeSerializer
 * 5cc5b382-e6dd-4991-abef-06d8d2e7bd0c:446, IXmlNamedNodeMap b3a69eb0-aab0-4b82-a6fa-b1453f7c021b:370,
 * IXmlElement 2dfb8a1f-6b10-4ef8-9f83-efcce8faec37:314), and CLSID_DOMDocument60 is
 * include/msxml6.idl:3060 (88d96a05-f192-11d4-a65f-0040963251e5).
 *
 * Entry points that need machinery msxml6 does not have (the async loaders, the
 * Windows.Foundation.IReference boxing of the namespaced node accessors, saving to
 * a Windows.Storage file) stay E_NOTIMPL, the honest stub marker, rather than
 * answering a value the DOM does not have.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"

/* initguid.h first: the WinRT and msxml interface ids used below are defined in
 * this object from the include/ descriptions, not imported from a library. */
#include "initguid.h"
#include "winstring.h"
#include "ole2.h"
#include "oaidl.h"
#include "oleidl.h"
#include "servprov.h"
#include "activation.h"
#include "inspectable.h"
#include "weakreference.h"
#include "rpcproxy.h"
#include "msxml6.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Storage_Streams
#define WIDL_using_Windows_Data_Xml_Dom
#include "windows.foundation.h"
#include "windows.storage.h"
#include "windows.storage.streams.h"
#include "windows.data.xml.dom.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msxml6);

#define XSLT_PROCESSOR_CLASS L"Windows.Data.Xml.Xsl.XsltProcessor"

static inline BOOL is_element( IXMLDOMNode *node )
{
    DOMNodeType type;
    return SUCCEEDED( IXMLDOMNode_get_nodeType( node, &type )) && type == NODE_ELEMENT;
}

/* ------------------------------------------------------------------ helpers */

static HRESULT create_node_wrapper( IXMLDOMNode *node, IXmlNode **out );
static HRESULT create_node_list( IXMLDOMNodeList *list, IXmlNodeList **out );
static HRESULT create_document_wrapper( IXMLDOMDocument *document, IXmlDocument **out );
static HRESULT create_named_node_map( IXMLDOMNamedNodeMap *map, IXmlNamedNodeMap **out );


static HRESULT hstring_to_bstr( HSTRING str, BSTR *out )
{
    const WCHAR *buffer;
    UINT32 len;
    BSTR bstr;

    if (!out) return E_POINTER;
    buffer = WindowsGetStringRawBuffer( str, &len );
    if (!(bstr = SysAllocStringLen( buffer, len ))) return E_OUTOFMEMORY;
    *out = bstr;
    return S_OK;
}

/* msxml6's get_xml ends the document text with a newline; the projected GetXml
 * returns the markup, so the trailing line breaks are not carried over. */
static UINT32 xml_markup_len( const WCHAR *buffer, UINT32 len )
{
    while (len && (buffer[len - 1] == L'\n' || buffer[len - 1] == L'\r')) len--;
    return len;
}

static HRESULT bstr_to_hstring( BSTR bstr, HSTRING *out )
{
    if (!out) return E_POINTER;
    if (!bstr)
    {
        *out = NULL;
        return S_OK;
    }
    return WindowsCreateString( bstr, SysStringLen( bstr ), out );
}

/* The DOMDocument60 class object, through this module's own DllGetClassObject
 * export: the spec forwards it to msxml3.dll, so taking the export of
 * msxml6.dll is exactly the path a COM client of msxml6 takes, and it needs no
 * HKCR\CLSID registration of the prefix. */
static HRESULT create_dom_document( IXMLDOMDocument **out )
{
    IClassFactory *factory = NULL;
    IXMLDOMDocument *document = NULL;
    HRESULT (WINAPI *pDllGetClassObject)( REFCLSID, REFIID, LPVOID * );
    HMODULE hmodule;
    HRESULT hr;

    if (!out) return E_POINTER;
    if (!(hmodule = LoadLibraryW( L"msxml6.dll" ))) return REGDB_E_CLASSNOTREG;
    if (!(pDllGetClassObject = (void *)GetProcAddress( hmodule, "DllGetClassObject" )))
        return REGDB_E_CLASSNOTREG;

    if (FAILED( hr = pDllGetClassObject( &CLSID_DOMDocument60, &IID_IClassFactory, (void **)&factory )))
        return hr;

    hr = IClassFactory_CreateInstance( factory, NULL, &IID_IXMLDOMDocument, (void **)&document );
    IClassFactory_Release( factory );
    if (FAILED( hr )) return hr;

    /* The msxml parser defaults to asynchronous loading and to validating
     * against a DTD; the WinRT XmlDocument loads synchronously. */
    IXMLDOMDocument_put_async( document, VARIANT_FALSE );
    IXMLDOMDocument_put_validateOnParse( document, VARIANT_FALSE );
    IXMLDOMDocument_put_resolveExternals( document, VARIANT_FALSE );

    *out = document;
    return S_OK;
}

/* IInspectable forwarder for the secondary interfaces of an object: identity,
 * refcount and the IInspectable entry points all live on the object's
 * canonical interface, the way the WinRT projection requires. */
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

/* --------------------------------------------------------------- node lists */

struct xml_node_list
{
    IXmlNodeList IXmlNodeList_iface;
    __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode vectorview_iface;
    __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode iterable_iface;
    IXMLDOMNodeList *list;
    LONG ref;
};

static inline struct xml_node_list *impl_from_IXmlNodeList( IXmlNodeList *iface )
{
    return CONTAINING_RECORD( iface, struct xml_node_list, IXmlNodeList_iface );
}

static inline struct xml_node_list *impl_from_vectorview_IXmlNode(
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *iface )
{
    return CONTAINING_RECORD( iface, struct xml_node_list, vectorview_iface );
}

static inline struct xml_node_list *impl_from_iterable_IXmlNode(
        __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode *iface )
{
    return CONTAINING_RECORD( iface, struct xml_node_list, iterable_iface );
}

/* IIterator<IXmlNode*> over a node list or, for the attribute map, over a named
 * node map; positioned before the first item. */
struct xml_node_iterator
{
    __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode iface;
    IXMLDOMNodeList *list;
    IXMLDOMNamedNodeMap *map;
    LONG index;
    LONG ref;
};

static HRESULT iterator_length( struct xml_node_iterator *impl, LONG *length )
{
    *length = 0;
    if (impl->list) return IXMLDOMNodeList_get_length( impl->list, length );
    if (impl->map) return IXMLDOMNamedNodeMap_get_length( impl->map, length );
    return S_OK;
}

static HRESULT iterator_item( struct xml_node_iterator *impl, LONG index, IXmlNode **node )
{
    IXMLDOMNode *raw = NULL;
    HRESULT hr;

    *node = NULL;
    if (impl->list)
    {
        if (FAILED( hr = IXMLDOMNodeList_get_item( impl->list, index, &raw ))) return hr;
    }
    else if (impl->map)
    {
        if (FAILED( hr = IXMLDOMNamedNodeMap_get_item( impl->map, index, &raw ))) return hr;
    }
    else return E_BOUNDS;

    if (!raw) return E_BOUNDS;
    hr = create_node_wrapper( raw, node );
    IXMLDOMNode_Release( raw );
    return hr;
}

static inline struct xml_node_iterator *impl_from_iterator_IXmlNode(
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface )
{
    return CONTAINING_RECORD( iface, struct xml_node_iterator, iface );
}

static HRESULT WINAPI node_iterator_QueryInterface(
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID___FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI node_iterator_AddRef( __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface )
{
    struct xml_node_iterator *impl = impl_from_iterator_IXmlNode( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI node_iterator_Release( __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface )
{
    struct xml_node_iterator *impl = impl_from_iterator_IXmlNode( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        if (impl->list) IXMLDOMNodeList_Release( impl->list );
        if (impl->map) IXMLDOMNamedNodeMap_Release( impl->map );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI node_iterator_GetIids(
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI node_iterator_GetRuntimeClassName(
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface, HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Foundation.Collections.IIterator`1<Windows.Data.Xml.Dom.IXmlNode>";
    return WindowsCreateString( name, ARRAY_SIZE( name ) - 1, class_name );
}

static HRESULT WINAPI node_iterator_GetTrustLevel(
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI node_iterator_get_Current(
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface, IXmlNode **value )
{
    struct xml_node_iterator *impl = impl_from_iterator_IXmlNode( iface );

    if (!value) return E_POINTER;
    *value = NULL;
    if (impl->index < 0) return E_BOUNDS;
    return iterator_item( impl, impl->index, value );
}

static HRESULT WINAPI node_iterator_get_HasCurrent(
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface, BOOLEAN *value )
{
    struct xml_node_iterator *impl = impl_from_iterator_IXmlNode( iface );
    LONG length = 0;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = FALSE;
    if (impl->index < 0) return E_BOUNDS;
    if (FAILED( hr = iterator_length( impl, &length ))) return hr;
    *value = impl->index < length;
    return S_OK;
}

static HRESULT WINAPI node_iterator_MoveNext(
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface, BOOLEAN *value )
{
    struct xml_node_iterator *impl = impl_from_iterator_IXmlNode( iface );
    LONG length = 0;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = FALSE;
    if (FAILED( hr = iterator_length( impl, &length ))) return hr;
    if (impl->index + 1 >= length) return S_OK;
    impl->index++;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI node_iterator_GetMany(
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iface, UINT32 items_size,
        IXmlNode **items, UINT32 *value )
{
    struct xml_node_iterator *impl = impl_from_iterator_IXmlNode( iface );
    LONG length = 0, i;
    HRESULT hr;

    TRACE( "iface %p, items_size %u, items %p, value %p.\n", iface, items_size, items, value );

    if (!value) return E_POINTER;
    if (items_size && !items) return E_POINTER;
    *value = 0;
    if (FAILED( hr = iterator_length( impl, &length ))) return hr;

    for (i = impl->index; i < length && (UINT32)(i - impl->index) < items_size; i++)
    {
        if (FAILED( hr = iterator_item( impl, i, &items[i - impl->index] ))) return hr;
        (*value)++;
    }
    impl->index += *value;
    return S_OK;
}

static const __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNodeVtbl node_iterator_vtbl =
{
    node_iterator_QueryInterface,
    node_iterator_AddRef,
    node_iterator_Release,
    node_iterator_GetIids,
    node_iterator_GetRuntimeClassName,
    node_iterator_GetTrustLevel,
    node_iterator_get_Current,
    node_iterator_get_HasCurrent,
    node_iterator_MoveNext,
    node_iterator_GetMany,
};

static HRESULT WINAPI node_list_QueryInterface( IXmlNodeList *iface, REFIID iid, void **out )
{
    struct xml_node_list *impl = impl_from_IXmlNodeList( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IXmlNodeList ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->vectorview_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->iterable_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI node_list_AddRef( IXmlNodeList *iface )
{
    struct xml_node_list *impl = impl_from_IXmlNodeList( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI node_list_Release( IXmlNodeList *iface )
{
    struct xml_node_list *impl = impl_from_IXmlNodeList( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        if (impl->list) IXMLDOMNodeList_Release( impl->list );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI node_list_GetIids( IXmlNodeList *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI node_list_GetRuntimeClassName( IXmlNodeList *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Data_Xml_Dom_XmlNodeList,
                                ARRAY_SIZE( RuntimeClass_Windows_Data_Xml_Dom_XmlNodeList ) - 1, class_name );
}

static HRESULT WINAPI node_list_GetTrustLevel( IXmlNodeList *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI node_list_get_Length( IXmlNodeList *iface, UINT32 *value )
{
    struct xml_node_list *impl = impl_from_IXmlNodeList( iface );
    LONG length = 0;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->list)
    {
        if (FAILED( hr = IXMLDOMNodeList_get_length( impl->list, &length ))) return hr;
    }
    *value = length;
    return S_OK;
}

static HRESULT WINAPI node_list_Item( IXmlNodeList *iface, UINT32 index, IXmlNode **node )
{
    struct xml_node_list *impl = impl_from_IXmlNodeList( iface );
    IXMLDOMNode *raw = NULL;
    HRESULT hr;

    TRACE( "iface %p, index %u, node %p.\n", iface, index, node );

    if (!node) return E_POINTER;
    *node = NULL;
    if (!impl->list) return E_BOUNDS;
    if (FAILED( hr = IXMLDOMNodeList_get_item( impl->list, index, &raw ))) return hr;
    if (!raw) return E_BOUNDS;
    hr = create_node_wrapper( raw, node );
    IXMLDOMNode_Release( raw );
    return hr;
}

static const IXmlNodeListVtbl node_list_vtbl =
{
    node_list_QueryInterface,
    node_list_AddRef,
    node_list_Release,
    node_list_GetIids,
    node_list_GetRuntimeClassName,
    node_list_GetTrustLevel,
    node_list_get_Length,
    node_list_Item,
};

FORWARD_IINSPECTABLE( node_vectorview, __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode,
                      struct xml_node_list, impl_from_vectorview_IXmlNode, &impl->IXmlNodeList_iface )

static HRESULT WINAPI node_vectorview_GetAt(
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *iface, UINT32 index, IXmlNode **value )
{
    struct xml_node_list *impl = impl_from_vectorview_IXmlNode( iface );
    return node_list_Item( &impl->IXmlNodeList_iface, index, value );
}

static HRESULT WINAPI node_vectorview_get_Size(
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *iface, UINT32 *value )
{
    struct xml_node_list *impl = impl_from_vectorview_IXmlNode( iface );
    return node_list_get_Length( &impl->IXmlNodeList_iface, value );
}

static HRESULT WINAPI node_vectorview_IndexOf(
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *iface, IXmlNode *element, UINT32 *index, BOOLEAN *value )
{
    TRACE( "iface %p, element %p, index %p, value %p.\n", iface, element, index, value );

    /* Node identity is not comparable in the msxml DOM (every navigation hands
     * out a fresh wrapper), so no position of @element can be reported. */
    if (!index || !value) return E_POINTER;
    *index = 0;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI node_vectorview_GetMany(
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *iface, UINT32 start_index,
        UINT32 items_size, IXmlNode **items, UINT32 *value )
{
    struct xml_node_list *impl = impl_from_vectorview_IXmlNode( iface );
    LONG length = 0, i;
    HRESULT hr;

    TRACE( "iface %p, start_index %u, items_size %u, items %p, value %p.\n",
           iface, start_index, items_size, items, value );

    if (!value) return E_POINTER;
    if (items_size && !items) return E_POINTER;
    *value = 0;
    if (!impl->list) return S_OK;
    if (FAILED( hr = IXMLDOMNodeList_get_length( impl->list, &length ))) return hr;

    for (i = start_index; i < length && (UINT32)(i - start_index) < items_size; i++)
    {
        IXMLDOMNode *raw = NULL;
        if (FAILED( hr = IXMLDOMNodeList_get_item( impl->list, i, &raw ))) return hr;
        if (!raw) break;
        hr = create_node_wrapper( raw, &items[i - start_index] );
        IXMLDOMNode_Release( raw );
        if (FAILED( hr )) return hr;
        (*value)++;
    }
    return S_OK;
}

static const __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNodeVtbl node_vectorview_vtbl =
{
    node_vectorview_QueryInterface,
    node_vectorview_AddRef,
    node_vectorview_Release,
    node_vectorview_GetIids,
    node_vectorview_GetRuntimeClassName,
    node_vectorview_GetTrustLevel,
    node_vectorview_GetAt,
    node_vectorview_get_Size,
    node_vectorview_IndexOf,
    node_vectorview_GetMany,
};

FORWARD_IINSPECTABLE( node_iterable, __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode,
                      struct xml_node_list, impl_from_iterable_IXmlNode, &impl->IXmlNodeList_iface )

static HRESULT WINAPI node_iterable_First(
        __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode *iface,
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode **result )
{
    struct xml_node_list *impl = impl_from_iterable_IXmlNode( iface );
    struct xml_node_iterator *iter;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;

    iter->iface.lpVtbl = &node_iterator_vtbl;
    iter->ref = 1;
    iter->index = -1;
    if (impl->list)
    {
        IXMLDOMNodeList_AddRef( impl->list );
        iter->list = impl->list;
    }
    *result = &iter->iface;
    return S_OK;
}

static const __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNodeVtbl node_iterable_vtbl =
{
    node_iterable_QueryInterface,
    node_iterable_AddRef,
    node_iterable_Release,
    node_iterable_GetIids,
    node_iterable_GetRuntimeClassName,
    node_iterable_GetTrustLevel,
    node_iterable_First,
};

static HRESULT create_node_list( IXMLDOMNodeList *list, IXmlNodeList **out )
{
    struct xml_node_list *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IXmlNodeList_iface.lpVtbl = &node_list_vtbl;
    impl->vectorview_iface.lpVtbl = &node_vectorview_vtbl;
    impl->iterable_iface.lpVtbl = &node_iterable_vtbl;
    impl->ref = 1;
    if (list)
    {
        IXMLDOMNodeList_AddRef( list );
        impl->list = list;
    }
    *out = &impl->IXmlNodeList_iface;
    return S_OK;
}

/* ------------------------------------------------------------ attribute maps */

struct xml_named_node_map
{
    IXmlNamedNodeMap IXmlNamedNodeMap_iface;
    __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode vectorview_iface;
    __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode iterable_iface;
    IXMLDOMNamedNodeMap *map;
    LONG ref;
};

static inline struct xml_named_node_map *impl_from_IXmlNamedNodeMap( IXmlNamedNodeMap *iface )
{
    return CONTAINING_RECORD( iface, struct xml_named_node_map, IXmlNamedNodeMap_iface );
}

static inline struct xml_named_node_map *impl_from_map_vectorview(
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *iface )
{
    return CONTAINING_RECORD( iface, struct xml_named_node_map, vectorview_iface );
}

static inline struct xml_named_node_map *impl_from_map_iterable(
        __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode *iface )
{
    return CONTAINING_RECORD( iface, struct xml_named_node_map, iterable_iface );
}

static HRESULT WINAPI node_map_QueryInterface( IXmlNamedNodeMap *iface, REFIID iid, void **out )
{
    struct xml_named_node_map *impl = impl_from_IXmlNamedNodeMap( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IXmlNamedNodeMap ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->vectorview_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID___FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->iterable_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI node_map_AddRef( IXmlNamedNodeMap *iface )
{
    struct xml_named_node_map *impl = impl_from_IXmlNamedNodeMap( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI node_map_Release( IXmlNamedNodeMap *iface )
{
    struct xml_named_node_map *impl = impl_from_IXmlNamedNodeMap( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        if (impl->map) IXMLDOMNamedNodeMap_Release( impl->map );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI node_map_GetIids( IXmlNamedNodeMap *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI node_map_GetRuntimeClassName( IXmlNamedNodeMap *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Data_Xml_Dom_XmlNamedNodeMap,
                                ARRAY_SIZE( RuntimeClass_Windows_Data_Xml_Dom_XmlNamedNodeMap ) - 1, class_name );
}

static HRESULT WINAPI node_map_GetTrustLevel( IXmlNamedNodeMap *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI node_map_get_Length( IXmlNamedNodeMap *iface, UINT32 *value )
{
    struct xml_named_node_map *impl = impl_from_IXmlNamedNodeMap( iface );
    LONG length = 0;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->map && FAILED( IXMLDOMNamedNodeMap_get_length( impl->map, &length ))) return E_FAIL;
    *value = length;
    return S_OK;
}

static HRESULT WINAPI node_map_Item( IXmlNamedNodeMap *iface, UINT32 index, IXmlNode **node )
{
    struct xml_named_node_map *impl = impl_from_IXmlNamedNodeMap( iface );
    IXMLDOMNode *raw = NULL;
    HRESULT hr;

    TRACE( "iface %p, index %u, node %p.\n", iface, index, node );

    if (!node) return E_POINTER;
    *node = NULL;
    if (!impl->map) return E_BOUNDS;
    if (FAILED( hr = IXMLDOMNamedNodeMap_get_item( impl->map, index, &raw ))) return hr;
    if (!raw) return E_BOUNDS;
    hr = create_node_wrapper( raw, node );
    IXMLDOMNode_Release( raw );
    return hr;
}

static HRESULT WINAPI node_map_GetNamedItem( IXmlNamedNodeMap *iface, HSTRING name, IXmlNode **node )
{
    struct xml_named_node_map *impl = impl_from_IXmlNamedNodeMap( iface );
    IXMLDOMNode *raw = NULL;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, name %s, node %p.\n", iface, debugstr_hstring( name ), node );

    if (!node) return E_POINTER;
    *node = NULL;
    if (!impl->map) return E_INVALIDARG;
    if (FAILED( hr = hstring_to_bstr( name, &str ))) return hr;
    hr = IXMLDOMNamedNodeMap_getNamedItem( impl->map, str, &raw );
    SysFreeString( str );
    if (FAILED( hr )) return hr;
    if (!raw) return E_INVALIDARG;
    hr = create_node_wrapper( raw, node );
    IXMLDOMNode_Release( raw );
    return hr;
}

static HRESULT WINAPI node_map_SetNamedItem( IXmlNamedNodeMap *iface, IXmlNode *node, IXmlNode **previous )
{
    FIXME( "iface %p, node %p, previous %p stub!\n", iface, node, previous );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_map_RemoveNamedItem( IXmlNamedNodeMap *iface, HSTRING name, IXmlNode **previous )
{
    FIXME( "iface %p, name %s, previous %p stub!\n", iface, debugstr_hstring( name ), previous );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_map_GetNamedItemNS( IXmlNamedNodeMap *iface, IInspectable *namespace_uri,
                                               HSTRING name, IXmlNode **node )
{
    FIXME( "iface %p, namespace_uri %p, name %s, node %p stub!\n",
           iface, namespace_uri, debugstr_hstring( name ), node );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_map_RemoveNamedItemNS( IXmlNamedNodeMap *iface, IInspectable *namespace_uri,
                                                  HSTRING name, IXmlNode **previous )
{
    FIXME( "iface %p, namespace_uri %p, name %s, previous %p stub!\n",
           iface, namespace_uri, debugstr_hstring( name ), previous );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_map_SetNamedItemNS( IXmlNamedNodeMap *iface, IXmlNode *node, IXmlNode **previous )
{
    FIXME( "iface %p, node %p, previous %p stub!\n", iface, node, previous );
    return E_NOTIMPL;
}

static const IXmlNamedNodeMapVtbl node_map_vtbl =
{
    node_map_QueryInterface,
    node_map_AddRef,
    node_map_Release,
    node_map_GetIids,
    node_map_GetRuntimeClassName,
    node_map_GetTrustLevel,
    node_map_get_Length,
    node_map_Item,
    node_map_GetNamedItem,
    node_map_SetNamedItem,
    node_map_RemoveNamedItem,
    node_map_GetNamedItemNS,
    node_map_RemoveNamedItemNS,
    node_map_SetNamedItemNS,
};

FORWARD_IINSPECTABLE( map_vectorview, __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode,
                      struct xml_named_node_map, impl_from_map_vectorview, &impl->IXmlNamedNodeMap_iface )

static HRESULT WINAPI map_vectorview_GetAt(
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *iface, UINT32 index, IXmlNode **value )
{
    struct xml_named_node_map *impl = impl_from_map_vectorview( iface );
    return node_map_Item( &impl->IXmlNamedNodeMap_iface, index, value );
}

static HRESULT WINAPI map_vectorview_get_Size(
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *iface, UINT32 *value )
{
    struct xml_named_node_map *impl = impl_from_map_vectorview( iface );
    return node_map_get_Length( &impl->IXmlNamedNodeMap_iface, value );
}

static HRESULT WINAPI map_vectorview_IndexOf(
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *iface, IXmlNode *element, UINT32 *index, BOOLEAN *value )
{
    if (!index || !value) return E_POINTER;
    *index = 0;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI map_vectorview_GetMany(
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *iface, UINT32 start_index,
        UINT32 items_size, IXmlNode **items, UINT32 *value )
{
    struct xml_named_node_map *impl = impl_from_map_vectorview( iface );
    LONG length = 0, i;

    TRACE( "iface %p, start_index %u, items_size %u, items %p, value %p.\n",
           iface, start_index, items_size, items, value );

    if (!value) return E_POINTER;
    if (items_size && !items) return E_POINTER;
    *value = 0;
    if (!impl->map) return S_OK;
    if (FAILED( IXMLDOMNamedNodeMap_get_length( impl->map, &length ))) return E_FAIL;

    for (i = start_index; i < length && (UINT32)(i - start_index) < items_size; i++)
    {
        IXMLDOMNode *raw = NULL;
        HRESULT hr;
        if (FAILED( hr = IXMLDOMNamedNodeMap_get_item( impl->map, i, &raw ))) return hr;
        if (!raw) break;
        hr = create_node_wrapper( raw, &items[i - start_index] );
        IXMLDOMNode_Release( raw );
        if (FAILED( hr )) return hr;
        (*value)++;
    }
    return S_OK;
}

static const __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNodeVtbl map_vectorview_vtbl =
{
    map_vectorview_QueryInterface,
    map_vectorview_AddRef,
    map_vectorview_Release,
    map_vectorview_GetIids,
    map_vectorview_GetRuntimeClassName,
    map_vectorview_GetTrustLevel,
    map_vectorview_GetAt,
    map_vectorview_get_Size,
    map_vectorview_IndexOf,
    map_vectorview_GetMany,
};

FORWARD_IINSPECTABLE( map_iterable, __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode,
                      struct xml_named_node_map, impl_from_map_iterable, &impl->IXmlNamedNodeMap_iface )

static HRESULT WINAPI map_iterable_First(
        __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode *iface,
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode **result )
{
    struct xml_named_node_map *impl = impl_from_map_iterable( iface );
    struct xml_node_iterator *iter;

    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;

    /* The named node map is enumerable as a vector view of its items; the
     * iterator walks the map's own indexed items. */
    iter->iface.lpVtbl = &node_iterator_vtbl;
    iter->ref = 1;
    iter->index = -1;
    if (impl->map)
    {
        IXMLDOMNamedNodeMap_AddRef( impl->map );
        iter->map = impl->map;
    }
    *result = &iter->iface;
    return S_OK;
}

static const __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNodeVtbl map_iterable_vtbl =
{
    map_iterable_QueryInterface,
    map_iterable_AddRef,
    map_iterable_Release,
    map_iterable_GetIids,
    map_iterable_GetRuntimeClassName,
    map_iterable_GetTrustLevel,
    map_iterable_First,
};

static HRESULT create_named_node_map( IXMLDOMNamedNodeMap *map, IXmlNamedNodeMap **out )
{
    struct xml_named_node_map *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IXmlNamedNodeMap_iface.lpVtbl = &node_map_vtbl;
    impl->vectorview_iface.lpVtbl = &map_vectorview_vtbl;
    impl->iterable_iface.lpVtbl = &map_iterable_vtbl;
    impl->ref = 1;
    if (map)
    {
        IXMLDOMNamedNodeMap_AddRef( map );
        impl->map = map;
    }
    *out = &impl->IXmlNamedNodeMap_iface;
    return S_OK;
}

/* ---------------------------------------------------------------- node wrapper */

struct xml_node
{
    IXmlNode IXmlNode_iface;
    IXmlNodeSelector IXmlNodeSelector_iface;
    IXmlNodeSerializer IXmlNodeSerializer_iface;
    IXmlElement IXmlElement_iface;
    IXMLDOMNode *node;
    LONG ref;
};

static inline struct xml_node *impl_from_IXmlNode( IXmlNode *iface )
{
    return CONTAINING_RECORD( iface, struct xml_node, IXmlNode_iface );
}

static inline struct xml_node *impl_from_IXmlNodeSelector( IXmlNodeSelector *iface )
{
    return CONTAINING_RECORD( iface, struct xml_node, IXmlNodeSelector_iface );
}

static inline struct xml_node *impl_from_IXmlNodeSerializer( IXmlNodeSerializer *iface )
{
    return CONTAINING_RECORD( iface, struct xml_node, IXmlNodeSerializer_iface );
}

static inline struct xml_node *impl_from_IXmlElement( IXmlElement *iface )
{
    return CONTAINING_RECORD( iface, struct xml_node, IXmlElement_iface );
}

/* The projected class name of the node, by msxml node type. */
static const WCHAR *node_class_name( DOMNodeType type )
{
    switch (type)
    {
    case NODE_ELEMENT:               return RuntimeClass_Windows_Data_Xml_Dom_XmlElement;
    case NODE_ATTRIBUTE:             return RuntimeClass_Windows_Data_Xml_Dom_XmlAttribute;
    case NODE_TEXT:                  return RuntimeClass_Windows_Data_Xml_Dom_XmlText;
    case NODE_CDATA_SECTION:         return RuntimeClass_Windows_Data_Xml_Dom_XmlCDataSection;
    case NODE_COMMENT:               return RuntimeClass_Windows_Data_Xml_Dom_XmlComment;
    case NODE_PROCESSING_INSTRUCTION: return RuntimeClass_Windows_Data_Xml_Dom_XmlProcessingInstruction;
    case NODE_DOCUMENT_TYPE:         return RuntimeClass_Windows_Data_Xml_Dom_XmlDocumentType;
    case NODE_ENTITY_REFERENCE:      return RuntimeClass_Windows_Data_Xml_Dom_XmlEntityReference;
    case NODE_DOCUMENT_FRAGMENT:     return RuntimeClass_Windows_Data_Xml_Dom_XmlDocumentFragment;
    default:                         return NULL;
    }
}

static HRESULT node_get_type( struct xml_node *impl, DOMNodeType *type )
{
    *type = NODE_ELEMENT;
    if (!impl->node) return S_OK;
    return IXMLDOMNode_get_nodeType( impl->node, type );
}

static HRESULT wrap_node( IXMLDOMNode *raw, IXmlNode **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!raw) return S_OK;
    return create_node_wrapper( raw, out );
}

static HRESULT WINAPI node_QueryInterface( IXmlNode *iface, REFIID iid, void **out )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IXmlNode ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IXmlNodeSelector ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IXmlNodeSelector_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IXmlNodeSerializer ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IXmlNodeSerializer_iface;
        return S_OK;
    }

    /* IXmlElement is answered for element nodes only, as the projection of the
     * XmlElement class requires. */
    if (IsEqualGUID( iid, &IID_IXmlElement ) && impl->node && is_element( impl->node ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IXmlElement_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI node_AddRef( IXmlNode *iface )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI node_Release( IXmlNode *iface )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        if (impl->node) IXMLDOMNode_Release( impl->node );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI node_GetIids( IXmlNode *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI node_GetRuntimeClassName( IXmlNode *iface, HSTRING *class_name )
{
    static const WCHAR nodeW[] = L"Windows.Data.Xml.Dom.XmlNode";
    struct xml_node *impl = impl_from_IXmlNode( iface );
    DOMNodeType type;
    const WCHAR *name;

    if (FAILED( node_get_type( impl, &type ))) return E_FAIL;
    if (!(name = node_class_name( type ))) name = nodeW;
    return WindowsCreateString( name, wcslen( name ), class_name );
}

static HRESULT WINAPI node_GetTrustLevel( IXmlNode *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

/* The msxml node value is a VARIANT and the projected one a
 * Windows.Foundation.IInspectable reference; boxing is wintypes.dll's job and
 * is not wired here, so the accessors stay E_NOTIMPL. */
static HRESULT WINAPI node_get_NodeValue( IXmlNode *iface, IInspectable **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_put_NodeValue( IXmlNode *iface, IInspectable *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_get_NodeType( IXmlNode *iface, NodeType *value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    DOMNodeType type;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (FAILED( hr = node_get_type( impl, &type ))) return hr;
    *value = (NodeType)type;
    return S_OK;
}

static HRESULT WINAPI node_get_NodeName( IXmlNode *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    BSTR str = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return S_OK;
    if (FAILED( hr = IXMLDOMNode_get_nodeName( impl->node, &str ))) return hr;
    hr = bstr_to_hstring( str, value );
    SysFreeString( str );
    return hr;
}

static HRESULT WINAPI node_get_ParentNode( IXmlNode *iface, IXmlNode **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *raw = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return S_OK;
    if (FAILED( hr = IXMLDOMNode_get_parentNode( impl->node, &raw ))) return hr;
    hr = wrap_node( raw, value );
    if (raw) IXMLDOMNode_Release( raw );
    return hr;
}

static HRESULT WINAPI node_get_ChildNodes( IXmlNode *iface, IXmlNodeList **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNodeList *raw = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return create_node_list( NULL, value );
    if (FAILED( hr = IXMLDOMNode_get_childNodes( impl->node, &raw ))) return hr;
    hr = create_node_list( raw, value );
    if (raw) IXMLDOMNodeList_Release( raw );
    return hr;
}

static HRESULT WINAPI node_get_FirstChild( IXmlNode *iface, IXmlNode **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *raw = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return S_OK;
    if (FAILED( hr = IXMLDOMNode_get_firstChild( impl->node, &raw ))) return hr;
    hr = wrap_node( raw, value );
    if (raw) IXMLDOMNode_Release( raw );
    return hr;
}

static HRESULT WINAPI node_get_LastChild( IXmlNode *iface, IXmlNode **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *raw = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return S_OK;
    if (FAILED( hr = IXMLDOMNode_get_lastChild( impl->node, &raw ))) return hr;
    hr = wrap_node( raw, value );
    if (raw) IXMLDOMNode_Release( raw );
    return hr;
}

static HRESULT WINAPI node_get_PreviousSibling( IXmlNode *iface, IXmlNode **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *raw = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return S_OK;
    if (FAILED( hr = IXMLDOMNode_get_previousSibling( impl->node, &raw ))) return hr;
    hr = wrap_node( raw, value );
    if (raw) IXMLDOMNode_Release( raw );
    return hr;
}

static HRESULT WINAPI node_get_NextSibling( IXmlNode *iface, IXmlNode **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNode *raw = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return S_OK;
    if (FAILED( hr = IXMLDOMNode_get_nextSibling( impl->node, &raw ))) return hr;
    hr = wrap_node( raw, value );
    if (raw) IXMLDOMNode_Release( raw );
    return hr;
}

static HRESULT WINAPI node_get_Attributes( IXmlNode *iface, IXmlNamedNodeMap **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMNamedNodeMap *raw = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return create_named_node_map( NULL, value );
    if (FAILED( hr = IXMLDOMNode_get_attributes( impl->node, &raw ))) return hr;
    hr = create_named_node_map( raw, value );
    if (raw) IXMLDOMNamedNodeMap_Release( raw );
    return hr;
}

static HRESULT WINAPI node_HasChildNodes( IXmlNode *iface, boolean *value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    VARIANT_BOOL has = VARIANT_FALSE;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = FALSE;
    if (!impl->node) return S_OK;
    if (FAILED( hr = IXMLDOMNode_hasChildNodes( impl->node, &has ))) return hr;
    *value = has == VARIANT_TRUE;
    return S_OK;
}

static HRESULT WINAPI node_get_OwnerDocument( IXmlNode *iface, IXmlDocument **value )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMDocument *raw = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return S_OK;
    if (FAILED( hr = IXMLDOMNode_get_ownerDocument( impl->node, &raw ))) return hr;
    if (!raw) return S_OK;
    hr = create_document_wrapper( raw, value );
    IXMLDOMDocument_Release( raw );
    return hr;
}

/* The mutation entry points take a projected IXmlNode; recovering the msxml
 * node behind a foreign wrapper needs an unwrapping the module does not
 * implement, so they stay E_NOTIMPL rather than mutating the wrong node. */
static HRESULT WINAPI node_InsertBefore( IXmlNode *iface, IXmlNode *new_child, IXmlNode *reference_child,
                                         IXmlNode **inserted_child )
{
    FIXME( "iface %p, new_child %p, reference_child %p, inserted_child %p stub!\n",
           iface, new_child, reference_child, inserted_child );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_ReplaceChild( IXmlNode *iface, IXmlNode *new_child, IXmlNode *reference_child,
                                         IXmlNode **previous_child )
{
    FIXME( "iface %p, new_child %p, reference_child %p, previous_child %p stub!\n",
           iface, new_child, reference_child, previous_child );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_RemoveChild( IXmlNode *iface, IXmlNode *child_node, IXmlNode **removed_child )
{
    FIXME( "iface %p, child_node %p, removed_child %p stub!\n", iface, child_node, removed_child );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_AppendChild( IXmlNode *iface, IXmlNode *new_child, IXmlNode **appended_child )
{
    FIXME( "iface %p, new_child %p, appended_child %p stub!\n", iface, new_child, appended_child );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_CloneNode( IXmlNode *iface, boolean deep, IXmlNode **new_node )
{
    FIXME( "iface %p, deep %d, new_node %p stub!\n", iface, deep, new_node );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_get_NamespaceUri( IXmlNode *iface, IInspectable **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_get_LocalName( IXmlNode *iface, IInspectable **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_get_Prefix( IXmlNode *iface, IInspectable **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_Normalize( IXmlNode *iface )
{
    struct xml_node *impl = impl_from_IXmlNode( iface );
    IXMLDOMElement *element;
    HRESULT hr;

    TRACE( "iface %p.\n", iface );

    /* msxml6 declares normalize() on the element interface only; a node that is
     * not an element has no element children to merge, so it is normalized
     * already. */
    if (FAILED( IXMLDOMNode_QueryInterface( impl->node, &IID_IXMLDOMElement, (void **)&element )))
        return S_OK;
    hr = IXMLDOMElement_normalize( element );
    IXMLDOMElement_Release( element );
    return hr;
}

static HRESULT WINAPI node_put_Prefix( IXmlNode *iface, IInspectable *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static const IXmlNodeVtbl node_vtbl =
{
    node_QueryInterface,
    node_AddRef,
    node_Release,
    node_GetIids,
    node_GetRuntimeClassName,
    node_GetTrustLevel,
    node_get_NodeValue,
    node_put_NodeValue,
    node_get_NodeType,
    node_get_NodeName,
    node_get_ParentNode,
    node_get_ChildNodes,
    node_get_FirstChild,
    node_get_LastChild,
    node_get_PreviousSibling,
    node_get_NextSibling,
    node_get_Attributes,
    node_HasChildNodes,
    node_get_OwnerDocument,
    node_InsertBefore,
    node_ReplaceChild,
    node_RemoveChild,
    node_AppendChild,
    node_CloneNode,
    node_get_NamespaceUri,
    node_get_LocalName,
    node_get_Prefix,
    node_Normalize,
    node_put_Prefix,
};

FORWARD_IINSPECTABLE( node_selector, IXmlNodeSelector, struct xml_node, impl_from_IXmlNodeSelector,
                      &impl->IXmlNode_iface )

static HRESULT WINAPI node_selector_SelectSingleNode( IXmlNodeSelector *iface, HSTRING xpath, IXmlNode **node )
{
    struct xml_node *impl = impl_from_IXmlNodeSelector( iface );
    IXMLDOMNode *raw = NULL;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, xpath %s, node %p.\n", iface, debugstr_hstring( xpath ), node );

    if (!node) return E_POINTER;
    *node = NULL;
    if (!impl->node) return E_INVALIDARG;
    if (FAILED( hr = hstring_to_bstr( xpath, &str ))) return hr;
    hr = IXMLDOMNode_selectSingleNode( impl->node, str, &raw );
    SysFreeString( str );
    if (FAILED( hr )) return hr;
    hr = wrap_node( raw, node );
    if (raw) IXMLDOMNode_Release( raw );
    return hr;
}

static HRESULT WINAPI node_selector_SelectNodes( IXmlNodeSelector *iface, HSTRING xpath, IXmlNodeList **list )
{
    struct xml_node *impl = impl_from_IXmlNodeSelector( iface );
    IXMLDOMNodeList *raw = NULL;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, xpath %s, list %p.\n", iface, debugstr_hstring( xpath ), list );

    if (!list) return E_POINTER;
    *list = NULL;
    if (!impl->node) return E_INVALIDARG;
    if (FAILED( hr = hstring_to_bstr( xpath, &str ))) return hr;
    hr = IXMLDOMNode_selectNodes( impl->node, str, &raw );
    SysFreeString( str );
    /* msxml reports a failed expression as a null list; the projection reports
     * the empty result as an empty list. */
    if (FAILED( hr )) return hr;
    hr = create_node_list( raw, list );
    if (raw) IXMLDOMNodeList_Release( raw );
    return hr;
}

static HRESULT WINAPI node_selector_SelectSingleNodeNS( IXmlNodeSelector *iface, HSTRING xpath,
                                                        IInspectable *namespaces, IXmlNode **node )
{
    FIXME( "iface %p, xpath %s, namespaces %p, node %p stub!\n",
           iface, debugstr_hstring( xpath ), namespaces, node );
    return E_NOTIMPL;
}

static HRESULT WINAPI node_selector_SelectNodesNS( IXmlNodeSelector *iface, HSTRING xpath,
                                                   IInspectable *namespaces, IXmlNodeList **list )
{
    FIXME( "iface %p, xpath %s, namespaces %p, list %p stub!\n",
           iface, debugstr_hstring( xpath ), namespaces, list );
    return E_NOTIMPL;
}

static const IXmlNodeSelectorVtbl node_selector_vtbl =
{
    node_selector_QueryInterface,
    node_selector_AddRef,
    node_selector_Release,
    node_selector_GetIids,
    node_selector_GetRuntimeClassName,
    node_selector_GetTrustLevel,
    node_selector_SelectSingleNode,
    node_selector_SelectNodes,
    node_selector_SelectSingleNodeNS,
    node_selector_SelectNodesNS,
};

FORWARD_IINSPECTABLE( node_serializer, IXmlNodeSerializer, struct xml_node, impl_from_IXmlNodeSerializer,
                      &impl->IXmlNode_iface )

static HRESULT WINAPI node_serializer_GetXml( IXmlNodeSerializer *iface, HSTRING *outer_xml )
{
    struct xml_node *impl = impl_from_IXmlNodeSerializer( iface );
    BSTR str = NULL;
    UINT32 len;
    HRESULT hr;

    TRACE( "iface %p, outer_xml %p.\n", iface, outer_xml );

    if (!outer_xml) return E_POINTER;
    *outer_xml = NULL;
    if (!impl->node) return S_OK;
    if (FAILED( hr = IXMLDOMNode_get_xml( impl->node, &str ))) return hr;
    len = str ? SysStringLen( str ) : 0;
    hr = WindowsCreateString( str, xml_markup_len( str, len ), outer_xml );
    SysFreeString( str );
    return hr;
}

static HRESULT WINAPI node_serializer_get_InnerText( IXmlNodeSerializer *iface, HSTRING *value )
{
    struct xml_node *impl = impl_from_IXmlNodeSerializer( iface );
    BSTR str = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->node) return S_OK;
    if (FAILED( hr = IXMLDOMNode_get_text( impl->node, &str ))) return hr;
    hr = bstr_to_hstring( str, value );
    SysFreeString( str );
    return hr;
}

static HRESULT WINAPI node_serializer_put_InnerText( IXmlNodeSerializer *iface, HSTRING value )
{
    struct xml_node *impl = impl_from_IXmlNodeSerializer( iface );
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );

    if (!impl->node) return E_INVALIDARG;
    if (FAILED( hr = hstring_to_bstr( value, &str ))) return hr;
    hr = IXMLDOMNode_put_text( impl->node, str );
    SysFreeString( str );
    return hr;
}

static const IXmlNodeSerializerVtbl node_serializer_vtbl =
{
    node_serializer_QueryInterface,
    node_serializer_AddRef,
    node_serializer_Release,
    node_serializer_GetIids,
    node_serializer_GetRuntimeClassName,
    node_serializer_GetTrustLevel,
    node_serializer_GetXml,
    node_serializer_get_InnerText,
    node_serializer_put_InnerText,
};

FORWARD_IINSPECTABLE( element, IXmlElement, struct xml_node, impl_from_IXmlElement, &impl->IXmlNode_iface )

/* The msxml element interface of the wrapped node. */
static HRESULT element_iface( struct xml_node *impl, IXMLDOMElement **out )
{
    *out = NULL;
    if (!impl->node) return E_INVALIDARG;
    return IXMLDOMNode_QueryInterface( impl->node, &IID_IXMLDOMElement, (void **)out );
}

static HRESULT WINAPI element_get_TagName( IXmlElement *iface, HSTRING *value )
{
    /* The projected TagName is the msxml node name of an element. */
    return node_get_NodeName( &impl_from_IXmlElement( iface )->IXmlNode_iface, value );
}

static HRESULT WINAPI element_GetAttribute( IXmlElement *iface, HSTRING attribute_name, HSTRING *attribute_value )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    VARIANT value;
    BSTR name;
    HRESULT hr;

    TRACE( "iface %p, attribute_name %s, attribute_value %p.\n",
           iface, debugstr_hstring( attribute_name ), attribute_value );

    if (!attribute_value) return E_POINTER;
    *attribute_value = NULL;
    V_VT( &value ) = VT_EMPTY;
    if (FAILED( hr = element_iface( impl, &element ))) return hr;
    if (FAILED( hr = hstring_to_bstr( attribute_name, &name )))
    {
        IXMLDOMElement_Release( element );
        return hr;
    }
    hr = IXMLDOMElement_getAttribute( element, name, &value );
    SysFreeString( name );
    IXMLDOMElement_Release( element );
    if (FAILED( hr )) return hr;
    if (value.vt == VT_BSTR)
    {
        hr = bstr_to_hstring( value.bstrVal, attribute_value );
        SysFreeString( value.bstrVal );
    }
    return hr;
}

static HRESULT WINAPI element_SetAttribute( IXmlElement *iface, HSTRING attribute_name, HSTRING attribute_value )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    VARIANT value;
    BSTR name;
    HRESULT hr;

    TRACE( "iface %p, attribute_name %s, attribute_value %s.\n",
           iface, debugstr_hstring( attribute_name ), debugstr_hstring( attribute_value ) );

    if (FAILED( hr = element_iface( impl, &element ))) return hr;
    if (FAILED( hr = hstring_to_bstr( attribute_name, &name )))
    {
        IXMLDOMElement_Release( element );
        return hr;
    }
    V_VT( &value ) = VT_BSTR;
    V_BSTR( &value ) = NULL;
    if (attribute_value && FAILED( hr = hstring_to_bstr( attribute_value, &V_BSTR( &value ) )))
    {
        SysFreeString( name );
        IXMLDOMElement_Release( element );
        return hr;
    }
    hr = IXMLDOMElement_setAttribute( element, name, value );
    SysFreeString( name );
    if (V_VT( &value ) == VT_BSTR) SysFreeString( V_BSTR( &value ) );
    IXMLDOMElement_Release( element );
    return hr;
}

static HRESULT WINAPI element_RemoveAttribute( IXmlElement *iface, HSTRING attribute_name )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    BSTR name;
    HRESULT hr;

    TRACE( "iface %p, attribute_name %s.\n", iface, debugstr_hstring( attribute_name ) );

    if (FAILED( hr = element_iface( impl, &element ))) return hr;
    if (FAILED( hr = hstring_to_bstr( attribute_name, &name )))
    {
        IXMLDOMElement_Release( element );
        return hr;
    }
    hr = IXMLDOMElement_removeAttribute( element, name );
    SysFreeString( name );
    IXMLDOMElement_Release( element );
    return hr;
}

static HRESULT element_unsupported( const char *name, IXmlElement *iface )
{
    FIXME( "%s: iface %p not implemented (the msxml attribute node is not projected)\n", name, iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI element_GetAttributeNode( IXmlElement *iface, HSTRING attribute_name, IXmlAttribute **attribute_node )
{
    element_unsupported( "IXmlElement::GetAttributeNode", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI element_SetAttributeNode( IXmlElement *iface, IXmlAttribute *new_attribute,
                                                IXmlAttribute **previous_attribute )
{
    element_unsupported( "IXmlElement::SetAttributeNode", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI element_RemoveAttributeNode( IXmlElement *iface, IXmlAttribute *attribute_node,
                                                   IXmlAttribute **removed_attribute )
{
    element_unsupported( "IXmlElement::RemoveAttributeNode", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI element_GetElementsByTagName( IXmlElement *iface, HSTRING tag_name, IXmlNodeList **elements )
{
    struct xml_node *impl = impl_from_IXmlElement( iface );
    IXMLDOMElement *element;
    IXMLDOMNodeList *raw = NULL;
    BSTR name;
    HRESULT hr;

    TRACE( "iface %p, tag_name %s, elements %p.\n", iface, debugstr_hstring( tag_name ), elements );

    if (!elements) return E_POINTER;
    *elements = NULL;
    if (FAILED( hr = element_iface( impl, &element ))) return hr;
    if (FAILED( hr = hstring_to_bstr( tag_name, &name )))
    {
        IXMLDOMElement_Release( element );
        return hr;
    }
    hr = IXMLDOMElement_getElementsByTagName( element, name, &raw );
    SysFreeString( name );
    IXMLDOMElement_Release( element );
    if (FAILED( hr )) return hr;
    hr = create_node_list( raw, elements );
    if (raw) IXMLDOMNodeList_Release( raw );
    return hr;
}

static HRESULT WINAPI element_SetAttributeNS( IXmlElement *iface, IInspectable *namespace_uri,
                                              HSTRING qualified_name, HSTRING value )
{
    FIXME( "iface %p, namespace_uri %p, qualified_name %s, value %s stub!\n",
           iface, namespace_uri, debugstr_hstring( qualified_name ), debugstr_hstring( value ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI element_GetAttributeNS( IXmlElement *iface, IInspectable *namespace_uri,
                                              HSTRING local_name, HSTRING *value )
{
    FIXME( "iface %p, namespace_uri %p, local_name %s, value %p stub!\n",
           iface, namespace_uri, debugstr_hstring( local_name ), value );
    return E_NOTIMPL;
}

static HRESULT WINAPI element_RemoveAttributeNS( IXmlElement *iface, IInspectable *namespace_uri, HSTRING local_name )
{
    FIXME( "iface %p, namespace_uri %p, local_name %s stub!\n",
           iface, namespace_uri, debugstr_hstring( local_name ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI element_SetAttributeNodeNS( IXmlElement *iface, IXmlAttribute *new_attribute,
                                                  IXmlAttribute **previous_attribute )
{
    element_unsupported( "IXmlElement::SetAttributeNodeNS", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI element_GetAttributeNodeNS( IXmlElement *iface, IInspectable *namespace_uri,
                                                  HSTRING local_name, IXmlAttribute **value )
{
    FIXME( "iface %p, namespace_uri %p, local_name %s, value %p stub!\n",
           iface, namespace_uri, debugstr_hstring( local_name ), value );
    return E_NOTIMPL;
}

static const IXmlElementVtbl element_vtbl =
{
    element_QueryInterface,
    element_AddRef,
    element_Release,
    element_GetIids,
    element_GetRuntimeClassName,
    element_GetTrustLevel,
    element_get_TagName,
    element_GetAttribute,
    element_SetAttribute,
    element_RemoveAttribute,
    element_GetAttributeNode,
    element_SetAttributeNode,
    element_RemoveAttributeNode,
    element_GetElementsByTagName,
    element_SetAttributeNS,
    element_GetAttributeNS,
    element_RemoveAttributeNS,
    element_SetAttributeNodeNS,
    element_GetAttributeNodeNS,
};

static HRESULT create_node_wrapper( IXMLDOMNode *node, IXmlNode **out )
{
    struct xml_node *impl;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IXmlNode_iface.lpVtbl = &node_vtbl;
    impl->IXmlNodeSelector_iface.lpVtbl = &node_selector_vtbl;
    impl->IXmlNodeSerializer_iface.lpVtbl = &node_serializer_vtbl;
    impl->IXmlElement_iface.lpVtbl = &element_vtbl;
    impl->ref = 1;
    if (node)
    {
        IXMLDOMNode_AddRef( node );
        impl->node = node;
    }
    *out = &impl->IXmlNode_iface;
    return S_OK;
}

/* ------------------------------------------------------------ document object */

struct xml_document
{
    IXmlDocument IXmlDocument_iface;
    IXmlDocumentIO IXmlDocumentIO_iface;
    IXmlDocumentIO2 IXmlDocumentIO2_iface;
    IXmlNode IXmlNode_iface;
    IXmlNodeSelector IXmlNodeSelector_iface;
    IXmlNodeSerializer IXmlNodeSerializer_iface;
    struct xml_node *node;
    IXMLDOMDocument *document;
    LONG ref;
};

static inline struct xml_document *impl_from_IXmlDocument( IXmlDocument *iface )
{
    return CONTAINING_RECORD( iface, struct xml_document, IXmlDocument_iface );
}

static inline struct xml_document *impl_from_IXmlDocumentIO( IXmlDocumentIO *iface )
{
    return CONTAINING_RECORD( iface, struct xml_document, IXmlDocumentIO_iface );
}

static inline struct xml_document *impl_from_IXmlDocumentIO2( IXmlDocumentIO2 *iface )
{
    return CONTAINING_RECORD( iface, struct xml_document, IXmlDocumentIO2_iface );
}

static inline struct xml_document *impl_from_document_node( IXmlNode *iface )
{
    return CONTAINING_RECORD( iface, struct xml_document, IXmlNode_iface );
}

static inline struct xml_document *impl_from_document_selector( IXmlNodeSelector *iface )
{
    return CONTAINING_RECORD( iface, struct xml_document, IXmlNodeSelector_iface );
}

static inline struct xml_document *impl_from_document_serializer( IXmlNodeSerializer *iface )
{
    return CONTAINING_RECORD( iface, struct xml_document, IXmlNodeSerializer_iface );
}

static HRESULT WINAPI document_QueryInterface( IXmlDocument *iface, REFIID iid, void **out )
{
    struct xml_document *impl = impl_from_IXmlDocument( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject )
            || IsEqualGUID( iid, &IID_IXmlDocument ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IXmlDocumentIO ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IXmlDocumentIO_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IXmlDocumentIO2 ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IXmlDocumentIO2_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IXmlNode ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IXmlNode_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IXmlNodeSelector ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IXmlNodeSelector_iface;
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IXmlNodeSerializer ))
    {
        IUnknown_AddRef( (IUnknown *)iface );
        *out = &impl->IXmlNodeSerializer_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI document_AddRef( IXmlDocument *iface )
{
    struct xml_document *impl = impl_from_IXmlDocument( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI document_Release( IXmlDocument *iface )
{
    struct xml_document *impl = impl_from_IXmlDocument( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        if (impl->node) IXmlNode_Release( &impl->node->IXmlNode_iface );
        if (impl->document) IXMLDOMDocument_Release( impl->document );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI document_GetIids( IXmlDocument *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI document_GetRuntimeClassName( IXmlDocument *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Data_Xml_Dom_XmlDocument,
                                ARRAY_SIZE( RuntimeClass_Windows_Data_Xml_Dom_XmlDocument ) - 1, class_name );
}

static HRESULT WINAPI document_GetTrustLevel( IXmlDocument *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI document_get_Doctype( IXmlDocument *iface, IXmlDocumentType **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_get_Implementation( IXmlDocument *iface, IXmlDomImplementation **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_get_DocumentElement( IXmlDocument *iface, IXmlElement **value )
{
    struct xml_document *impl = impl_from_IXmlDocument( iface );
    IXMLDOMElement *raw = NULL;
    IXmlNode *node;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->document) return S_OK;
    if (FAILED( hr = IXMLDOMDocument_get_documentElement( impl->document, &raw ))) return hr;
    if (!raw) return S_OK;
    hr = create_node_wrapper( (IXMLDOMNode *)raw, &node );
    IXMLDOMElement_Release( raw );
    if (FAILED( hr )) return hr;
    /* The projected pointer is the element interface of the wrapper. */
    hr = IXmlNode_QueryInterface( node, &IID_IXmlElement, (void **)value );
    IXmlNode_Release( node );
    return hr;
}

static HRESULT WINAPI document_CreateElement( IXmlDocument *iface, HSTRING tag_name, IXmlElement **new_element )
{
    struct xml_document *impl = impl_from_IXmlDocument( iface );
    IXMLDOMElement *raw = NULL;
    IXmlNode *node;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, tag_name %s, new_element %p.\n", iface, debugstr_hstring( tag_name ), new_element );

    if (!new_element) return E_POINTER;
    *new_element = NULL;
    if (!impl->document) return E_INVALIDARG;
    if (FAILED( hr = hstring_to_bstr( tag_name, &str ))) return hr;
    hr = IXMLDOMDocument_createElement( impl->document, str, &raw );
    SysFreeString( str );
    if (FAILED( hr )) return hr;
    if (!raw) return E_FAIL;
    hr = create_node_wrapper( (IXMLDOMNode *)raw, &node );
    IXMLDOMElement_Release( raw );
    if (FAILED( hr )) return hr;
    hr = IXmlNode_QueryInterface( node, &IID_IXmlElement, (void **)new_element );
    IXmlNode_Release( node );
    return hr;
}

/* The node-creation entry points return projected node classes (XmlText,
 * XmlComment, XmlAttribute, ...) whose projection interfaces this module does
 * not implement; they stay E_NOTIMPL instead of handing out an object that
 * answers only IXmlNode. */
static HRESULT WINAPI document_CreateDocumentFragment( IXmlDocument *iface, IXmlDocumentFragment **out )
{
    FIXME( "iface %p, out %p stub!\n", iface, out );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_CreateTextNode( IXmlDocument *iface, HSTRING data, IXmlText **new_text_node )
{
    FIXME( "iface %p, data %s, new_text_node %p stub!\n", iface, debugstr_hstring( data ), new_text_node );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_CreateComment( IXmlDocument *iface, HSTRING data, IXmlComment **new_comment )
{
    FIXME( "iface %p, data %s, new_comment %p stub!\n", iface, debugstr_hstring( data ), new_comment );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_CreateProcessingInstruction( IXmlDocument *iface, HSTRING target, HSTRING data,
                                                            IXmlProcessingInstruction **new_processing_instruction )
{
    FIXME( "iface %p, target %s, data %s, new_processing_instruction %p stub!\n",
           iface, debugstr_hstring( target ), debugstr_hstring( data ), new_processing_instruction );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_CreateAttribute( IXmlDocument *iface, HSTRING name, IXmlAttribute **new_attribute )
{
    FIXME( "iface %p, name %s, new_attribute %p stub!\n", iface, debugstr_hstring( name ), new_attribute );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_CreateEntityReference( IXmlDocument *iface, HSTRING name,
                                                      IXmlEntityReference **new_entity_reference )
{
    FIXME( "iface %p, name %s, new_entity_reference %p stub!\n",
           iface, debugstr_hstring( name ), new_entity_reference );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_GetElementsByTagName( IXmlDocument *iface, HSTRING tag_name, IXmlNodeList **elements )
{
    struct xml_document *impl = impl_from_IXmlDocument( iface );
    IXMLDOMNodeList *raw = NULL;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, tag_name %s, elements %p.\n", iface, debugstr_hstring( tag_name ), elements );

    if (!elements) return E_POINTER;
    *elements = NULL;
    if (!impl->document) return E_INVALIDARG;
    if (FAILED( hr = hstring_to_bstr( tag_name, &str ))) return hr;
    hr = IXMLDOMDocument_getElementsByTagName( impl->document, str, &raw );
    SysFreeString( str );
    if (FAILED( hr )) return hr;
    hr = create_node_list( raw, elements );
    if (raw) IXMLDOMNodeList_Release( raw );
    return hr;
}

static HRESULT WINAPI document_CreateCDataSection( IXmlDocument *iface, HSTRING data, IXmlCDataSection **out )
{
    FIXME( "iface %p, data %s, out %p stub!\n", iface, debugstr_hstring( data ), out );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_get_DocumentUri( IXmlDocument *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    /* The document is never loaded from a URI, it has none. */
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI document_CreateAttributeNS( IXmlDocument *iface, IInspectable *namespace_uri,
                                                  HSTRING qualified_name, IXmlAttribute **new_attribute )
{
    FIXME( "iface %p, namespace_uri %p, qualified_name %s, new_attribute %p stub!\n",
           iface, namespace_uri, debugstr_hstring( qualified_name ), new_attribute );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_CreateElementNS( IXmlDocument *iface, IInspectable *namespace_uri,
                                                HSTRING qualified_name, IXmlElement **new_element )
{
    FIXME( "iface %p, namespace_uri %p, qualified_name %s, new_element %p stub!\n",
           iface, namespace_uri, debugstr_hstring( qualified_name ), new_element );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_GetElementById( IXmlDocument *iface, HSTRING element_id, IXmlElement **element )
{
    FIXME( "iface %p, element_id %s, element %p stub!\n", iface, debugstr_hstring( element_id ), element );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_ImportNode( IXmlDocument *iface, IXmlNode *node, boolean deep, IXmlNode **new_node )
{
    FIXME( "iface %p, node %p, deep %d, new_node %p stub!\n", iface, node, deep, new_node );
    return E_NOTIMPL;
}

static const IXmlDocumentVtbl document_vtbl =
{
    document_QueryInterface,
    document_AddRef,
    document_Release,
    document_GetIids,
    document_GetRuntimeClassName,
    document_GetTrustLevel,
    document_get_Doctype,
    document_get_Implementation,
    document_get_DocumentElement,
    document_CreateElement,
    document_CreateDocumentFragment,
    document_CreateTextNode,
    document_CreateComment,
    document_CreateProcessingInstruction,
    document_CreateAttribute,
    document_CreateEntityReference,
    document_GetElementsByTagName,
    document_CreateCDataSection,
    document_get_DocumentUri,
    document_CreateAttributeNS,
    document_CreateElementNS,
    document_GetElementById,
    document_ImportNode,
};

FORWARD_IINSPECTABLE( document_io, IXmlDocumentIO, struct xml_document, impl_from_IXmlDocumentIO,
                      &impl->IXmlDocument_iface )

static HRESULT WINAPI document_io_LoadXml( IXmlDocumentIO *iface, HSTRING xml )
{
    struct xml_document *impl = impl_from_IXmlDocumentIO( iface );
    VARIANT_BOOL status = VARIANT_FALSE;
    BSTR str;
    HRESULT hr;

    TRACE( "iface %p, xml %s.\n", iface, debugstr_hstring( xml ) );

    if (!impl->document) return E_INVALIDARG;
    if (FAILED( hr = hstring_to_bstr( xml, &str ))) return hr;
    hr = IXMLDOMDocument_loadXML( impl->document, str, &status );
    SysFreeString( str );
    if (FAILED( hr )) return hr;
    /* msxml reports a parse failure through the load status, not through the
     * code; the projection reports a rejected document as a failed call. */
    return status == VARIANT_TRUE ? S_OK : E_INVALIDARG;
}

static HRESULT WINAPI document_io_LoadXmlWithSettings( IXmlDocumentIO *iface, HSTRING xml,
                                                       IXmlLoadSettings *load_settings )
{
    /* The settings msxml6 exposes by default (no DTD validation, no external
     * resolution, synchronous load) are the XmlLoadSettings defaults, so the
     * settings object carries no behaviour this load path has to apply. */
    TRACE( "iface %p, xml %s, load_settings %p.\n", iface, debugstr_hstring( xml ), load_settings );
    return document_io_LoadXml( iface, xml );
}

static HRESULT WINAPI document_io_SaveToFileAsync( IXmlDocumentIO *iface,
        __x_ABI_CWindows_CStorage_CIStorageFile *file, __x_ABI_CWindows_CFoundation_CIAsyncAction **async_info )
{
    FIXME( "iface %p, file %p, async_info %p stub!\n", iface, file, async_info );
    return E_NOTIMPL;
}

static const IXmlDocumentIOVtbl document_io_vtbl =
{
    document_io_QueryInterface,
    document_io_AddRef,
    document_io_Release,
    document_io_GetIids,
    document_io_GetRuntimeClassName,
    document_io_GetTrustLevel,
    document_io_LoadXml,
    document_io_LoadXmlWithSettings,
    document_io_SaveToFileAsync,
};

FORWARD_IINSPECTABLE( document_io2, IXmlDocumentIO2, struct xml_document, impl_from_IXmlDocumentIO2,
                      &impl->IXmlDocument_iface )

static HRESULT WINAPI document_io2_LoadXmlFromBuffer( IXmlDocumentIO2 *iface,
                                                      __x_ABI_CWindows_CStorage_CStreams_CIBuffer *buffer )
{
    FIXME( "iface %p, buffer %p stub!\n", iface, buffer );
    return E_NOTIMPL;
}

static HRESULT WINAPI document_io2_LoadXmlFromBufferWithSettings( IXmlDocumentIO2 *iface,
                                                                  __x_ABI_CWindows_CStorage_CStreams_CIBuffer *buffer,
                                                                  IXmlLoadSettings *load_settings )
{
    FIXME( "iface %p, buffer %p, load_settings %p stub!\n", iface, buffer, load_settings );
    return E_NOTIMPL;
}

static const IXmlDocumentIO2Vtbl document_io2_vtbl =
{
    document_io2_QueryInterface,
    document_io2_AddRef,
    document_io2_Release,
    document_io2_GetIids,
    document_io2_GetRuntimeClassName,
    document_io2_GetTrustLevel,
    document_io2_LoadXmlFromBuffer,
    document_io2_LoadXmlFromBufferWithSettings,
};

/* IXmlNode/IXmlNodeSelector/IXmlNodeSerializer of the document class delegate
 * to the node wrapper over the same msxml document: the projection requires
 * the document to answer the interfaces the XmlElement class answers. */
#define DELEGATE_ARGS( ... ) __VA_ARGS__
#define DOCUMENT_NODE_DELEGATE( name, args, call )                                                 \
    static HRESULT WINAPI document_node_##name( IXmlNode *iface, DELEGATE_ARGS args )               \
    {                                                                                              \
        struct xml_document *impl = impl_from_document_node( iface );                              \
        return call;                                                                               \
    }

DOCUMENT_NODE_DELEGATE( get_NodeValue, (IInspectable **value),
        IXmlNode_get_NodeValue( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( put_NodeValue, (IInspectable *value),
        IXmlNode_put_NodeValue( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_NodeType, (NodeType *value),
        IXmlNode_get_NodeType( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_NodeName, (HSTRING *value),
        IXmlNode_get_NodeName( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_ParentNode, (IXmlNode **value),
        IXmlNode_get_ParentNode( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_ChildNodes, (IXmlNodeList **value),
        IXmlNode_get_ChildNodes( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_FirstChild, (IXmlNode **value),
        IXmlNode_get_FirstChild( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_LastChild, (IXmlNode **value),
        IXmlNode_get_LastChild( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_PreviousSibling, (IXmlNode **value),
        IXmlNode_get_PreviousSibling( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_NextSibling, (IXmlNode **value),
        IXmlNode_get_NextSibling( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_Attributes, (IXmlNamedNodeMap **value),
        IXmlNode_get_Attributes( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( HasChildNodes, (boolean *value),
        IXmlNode_HasChildNodes( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_OwnerDocument, (IXmlDocument **value),
        IXmlNode_get_OwnerDocument( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( InsertBefore, (IXmlNode *new_child, IXmlNode *reference_child, IXmlNode **inserted_child),
        IXmlNode_InsertBefore( &impl->node->IXmlNode_iface, new_child, reference_child, inserted_child ))
DOCUMENT_NODE_DELEGATE( ReplaceChild, (IXmlNode *new_child, IXmlNode *reference_child, IXmlNode **previous_child),
        IXmlNode_ReplaceChild( &impl->node->IXmlNode_iface, new_child, reference_child, previous_child ))
DOCUMENT_NODE_DELEGATE( RemoveChild, (IXmlNode *child_node, IXmlNode **removed_child),
        IXmlNode_RemoveChild( &impl->node->IXmlNode_iface, child_node, removed_child ))
DOCUMENT_NODE_DELEGATE( AppendChild, (IXmlNode *new_child, IXmlNode **appended_child),
        IXmlNode_AppendChild( &impl->node->IXmlNode_iface, new_child, appended_child ))
DOCUMENT_NODE_DELEGATE( CloneNode, (boolean deep, IXmlNode **new_node),
        IXmlNode_CloneNode( &impl->node->IXmlNode_iface, deep, new_node ))
DOCUMENT_NODE_DELEGATE( get_NamespaceUri, (IInspectable **value),
        IXmlNode_get_NamespaceUri( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_LocalName, (IInspectable **value),
        IXmlNode_get_LocalName( &impl->node->IXmlNode_iface, value ))
DOCUMENT_NODE_DELEGATE( get_Prefix, (IInspectable **value),
        IXmlNode_get_Prefix( &impl->node->IXmlNode_iface, value ))

DOCUMENT_NODE_DELEGATE( put_Prefix, (IInspectable *value),
        IXmlNode_put_Prefix( &impl->node->IXmlNode_iface, value ))

static HRESULT WINAPI document_node_Normalize( IXmlNode *iface )
{
    struct xml_document *impl = impl_from_document_node( iface );
    return IXmlNode_Normalize( &impl->node->IXmlNode_iface );
}

FORWARD_IINSPECTABLE( document_node, IXmlNode, struct xml_document, impl_from_document_node,
                      &impl->IXmlDocument_iface )

static const IXmlNodeVtbl document_node_vtbl =
{
    document_node_QueryInterface,
    document_node_AddRef,
    document_node_Release,
    document_node_GetIids,
    document_node_GetRuntimeClassName,
    document_node_GetTrustLevel,
    document_node_get_NodeValue,
    document_node_put_NodeValue,
    document_node_get_NodeType,
    document_node_get_NodeName,
    document_node_get_ParentNode,
    document_node_get_ChildNodes,
    document_node_get_FirstChild,
    document_node_get_LastChild,
    document_node_get_PreviousSibling,
    document_node_get_NextSibling,
    document_node_get_Attributes,
    document_node_HasChildNodes,
    document_node_get_OwnerDocument,
    document_node_InsertBefore,
    document_node_ReplaceChild,
    document_node_RemoveChild,
    document_node_AppendChild,
    document_node_CloneNode,
    document_node_get_NamespaceUri,
    document_node_get_LocalName,
    document_node_get_Prefix,
    document_node_Normalize,
    document_node_put_Prefix,
};

FORWARD_IINSPECTABLE( document_selector, IXmlNodeSelector, struct xml_document, impl_from_document_selector,
                      &impl->IXmlDocument_iface )

#define DOCUMENT_SELECTOR_DELEGATE( name, args, call )                                             \
    static HRESULT WINAPI document_selector_##name( IXmlNodeSelector *iface, DELEGATE_ARGS args )  \
    {                                                                                              \
        struct xml_document *impl = impl_from_document_selector( iface );                          \
        return call;                                                                               \
    }

DOCUMENT_SELECTOR_DELEGATE( SelectSingleNode, (HSTRING xpath, IXmlNode **node),
        IXmlNodeSelector_SelectSingleNode( &impl->node->IXmlNodeSelector_iface, xpath, node ))
DOCUMENT_SELECTOR_DELEGATE( SelectNodes, (HSTRING xpath, IXmlNodeList **list),
        IXmlNodeSelector_SelectNodes( &impl->node->IXmlNodeSelector_iface, xpath, list ))
DOCUMENT_SELECTOR_DELEGATE( SelectSingleNodeNS, (HSTRING xpath, IInspectable *namespaces, IXmlNode **node),
        IXmlNodeSelector_SelectSingleNodeNS( &impl->node->IXmlNodeSelector_iface, xpath, namespaces, node ))
DOCUMENT_SELECTOR_DELEGATE( SelectNodesNS, (HSTRING xpath, IInspectable *namespaces, IXmlNodeList **list),
        IXmlNodeSelector_SelectNodesNS( &impl->node->IXmlNodeSelector_iface, xpath, namespaces, list ))

static const IXmlNodeSelectorVtbl document_selector_vtbl =
{
    document_selector_QueryInterface,
    document_selector_AddRef,
    document_selector_Release,
    document_selector_GetIids,
    document_selector_GetRuntimeClassName,
    document_selector_GetTrustLevel,
    document_selector_SelectSingleNode,
    document_selector_SelectNodes,
    document_selector_SelectSingleNodeNS,
    document_selector_SelectNodesNS,
};

FORWARD_IINSPECTABLE( document_serializer, IXmlNodeSerializer, struct xml_document, impl_from_document_serializer,
                      &impl->IXmlDocument_iface )

#define DOCUMENT_SERIALIZER_DELEGATE( name, args, call )                                           \
    static HRESULT WINAPI document_serializer_##name( IXmlNodeSerializer *iface, DELEGATE_ARGS args ) \
    {                                                                                              \
        struct xml_document *impl = impl_from_document_serializer( iface );                        \
        return call;                                                                               \
    }

DOCUMENT_SERIALIZER_DELEGATE( GetXml, (HSTRING *outer_xml),
        IXmlNodeSerializer_GetXml( &impl->node->IXmlNodeSerializer_iface, outer_xml ))
DOCUMENT_SERIALIZER_DELEGATE( get_InnerText, (HSTRING *value),
        IXmlNodeSerializer_get_InnerText( &impl->node->IXmlNodeSerializer_iface, value ))
DOCUMENT_SERIALIZER_DELEGATE( put_InnerText, (HSTRING value),
        IXmlNodeSerializer_put_InnerText( &impl->node->IXmlNodeSerializer_iface, value ))

static const IXmlNodeSerializerVtbl document_serializer_vtbl =
{
    document_serializer_QueryInterface,
    document_serializer_AddRef,
    document_serializer_Release,
    document_serializer_GetIids,
    document_serializer_GetRuntimeClassName,
    document_serializer_GetTrustLevel,
    document_serializer_GetXml,
    document_serializer_get_InnerText,
    document_serializer_put_InnerText,
};

static HRESULT create_document_wrapper( IXMLDOMDocument *document, IXmlDocument **out )
{
    struct xml_document *impl;
    IXmlNode *node;
    HRESULT hr;

    if (!out) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IXmlDocument_iface.lpVtbl = &document_vtbl;
    impl->IXmlDocumentIO_iface.lpVtbl = &document_io_vtbl;
    impl->IXmlDocumentIO2_iface.lpVtbl = &document_io2_vtbl;
    impl->IXmlNode_iface.lpVtbl = &document_node_vtbl;
    impl->IXmlNodeSelector_iface.lpVtbl = &document_selector_vtbl;
    impl->IXmlNodeSerializer_iface.lpVtbl = &document_serializer_vtbl;
    impl->ref = 1;

    if (document)
    {
        IXMLDOMDocument_AddRef( document );
        impl->document = document;
    }
    if (SUCCEEDED( hr = create_node_wrapper( (IXMLDOMNode *)document, &node )))
        impl->node = impl_from_IXmlNode( node );
    if (FAILED( hr ))
    {
        if (document) IXMLDOMDocument_Release( document );
        free( impl );
        return hr;
    }
    *out = &impl->IXmlDocument_iface;
    return S_OK;
}

/* --------------------------------------------------------------- factories */

struct xml_document_factory
{
    IActivationFactory IActivationFactory_iface;
    IXmlDocumentStatics IXmlDocumentStatics_iface;
    LONG ref;
};

static inline struct xml_document_factory *impl_from_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct xml_document_factory, IActivationFactory_iface );
}

static inline struct xml_document_factory *impl_from_statics( IXmlDocumentStatics *iface )
{
    return CONTAINING_RECORD( iface, struct xml_document_factory, IXmlDocumentStatics_iface );
}

static HRESULT WINAPI xml_document_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct xml_document_factory *impl = impl_from_factory( iface );

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

    if (IsEqualGUID( iid, &IID_IXmlDocumentStatics ))
    {
        IUnknown_AddRef( iface );
        *out = &impl->IXmlDocumentStatics_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI xml_document_factory_AddRef( IActivationFactory *iface )
{
    struct xml_document_factory *impl = impl_from_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI xml_document_factory_Release( IActivationFactory *iface )
{
    struct xml_document_factory *impl = impl_from_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI xml_document_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI xml_document_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( RuntimeClass_Windows_Data_Xml_Dom_XmlDocument,
                                ARRAY_SIZE( RuntimeClass_Windows_Data_Xml_Dom_XmlDocument ) - 1, class_name );
}

static HRESULT WINAPI xml_document_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI xml_document_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    IXMLDOMDocument *document = NULL;
    IXmlDocument *xml_document;
    HRESULT hr;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = NULL;
    if (FAILED( hr = create_dom_document( &document ))) return hr;
    hr = create_document_wrapper( document, &xml_document );
    IXMLDOMDocument_Release( document );
    if (FAILED( hr )) return hr;
    *instance = (IInspectable *)xml_document;
    return S_OK;
}

static const IActivationFactoryVtbl xml_document_factory_vtbl =
{
    xml_document_factory_QueryInterface,
    xml_document_factory_AddRef,
    xml_document_factory_Release,
    xml_document_factory_GetIids,
    xml_document_factory_GetRuntimeClassName,
    xml_document_factory_GetTrustLevel,
    xml_document_factory_ActivateInstance,
};

FORWARD_IINSPECTABLE( xml_document_statics, IXmlDocumentStatics, struct xml_document_factory,
                      impl_from_statics, &impl->IActivationFactory_iface )

/* The static loaders read a Windows.Storage file or a Uri asynchronously; that
 * is windows.storage.dll's machinery, not msxml6's, so they stay E_NOTIMPL. */
static HRESULT WINAPI xml_document_statics_LoadFromUriAsync( IXmlDocumentStatics *iface,
        __x_ABI_CWindows_CFoundation_CIUriRuntimeClass *uri,
        __FIAsyncOperation_1_Windows__CData__CXml__CDom__CXmlDocument **async_info )
{
    FIXME( "iface %p, uri %p, async_info %p stub!\n", iface, uri, async_info );
    return E_NOTIMPL;
}

static HRESULT WINAPI xml_document_statics_LoadFromUriWithSettingsAsync( IXmlDocumentStatics *iface,
        __x_ABI_CWindows_CFoundation_CIUriRuntimeClass *uri, IXmlLoadSettings *load_settings,
        __FIAsyncOperation_1_Windows__CData__CXml__CDom__CXmlDocument **async_info )
{
    FIXME( "iface %p, uri %p, load_settings %p, async_info %p stub!\n",
           iface, uri, load_settings, async_info );
    return E_NOTIMPL;
}

static HRESULT WINAPI xml_document_statics_LoadFromFileAsync( IXmlDocumentStatics *iface,
        __x_ABI_CWindows_CStorage_CIStorageFile *file,
        __FIAsyncOperation_1_Windows__CData__CXml__CDom__CXmlDocument **async_info )
{
    FIXME( "iface %p, file %p, async_info %p stub!\n", iface, file, async_info );
    return E_NOTIMPL;
}

static HRESULT WINAPI xml_document_statics_LoadFromFileWithSettingsAsync( IXmlDocumentStatics *iface,
        __x_ABI_CWindows_CStorage_CIStorageFile *file, IXmlLoadSettings *load_settings,
        __FIAsyncOperation_1_Windows__CData__CXml__CDom__CXmlDocument **async_info )
{
    FIXME( "iface %p, file %p, load_settings %p, async_info %p stub!\n",
           iface, file, load_settings, async_info );
    return E_NOTIMPL;
}

static const IXmlDocumentStaticsVtbl xml_document_statics_vtbl =
{
    xml_document_statics_QueryInterface,
    xml_document_statics_AddRef,
    xml_document_statics_Release,
    xml_document_statics_GetIids,
    xml_document_statics_GetRuntimeClassName,
    xml_document_statics_GetTrustLevel,
    xml_document_statics_LoadFromUriAsync,
    xml_document_statics_LoadFromUriWithSettingsAsync,
    xml_document_statics_LoadFromFileAsync,
    xml_document_statics_LoadFromFileWithSettingsAsync,
};

static struct xml_document_factory xml_document_factory =
{
    { &xml_document_factory_vtbl },
    { &xml_document_statics_vtbl },
    1
};

/* Windows.Data.Xml.Xsl.XsltProcessor.  include/ has no windows.data.xml.xsl.idl
 * in this tree, so the class's projected interface (IXsltProcessor) has no IID
 * to answer: the factory hands out the instance, whose IInspectable surface is
 * complete, and every interface query for a projection this tree cannot name is
 * answered E_NOINTERFACE. */
struct xslt_processor
{
    IInspectable IInspectable_iface;
    LONG ref;
};

static inline struct xslt_processor *impl_from_xslt_processor( IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct xslt_processor, IInspectable_iface );
}

static HRESULT WINAPI xslt_processor_QueryInterface( IInspectable *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown )
            || IsEqualGUID( iid, &IID_IInspectable )
            || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI xslt_processor_AddRef( IInspectable *iface )
{
    struct xslt_processor *impl = impl_from_xslt_processor( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI xslt_processor_Release( IInspectable *iface )
{
    struct xslt_processor *impl = impl_from_xslt_processor( iface );
    LONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        TRACE( "destroying %p\n", iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI xslt_processor_GetIids( IInspectable *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI xslt_processor_GetRuntimeClassName( IInspectable *iface, HSTRING *class_name )
{
    return WindowsCreateString( XSLT_PROCESSOR_CLASS, wcslen( XSLT_PROCESSOR_CLASS ), class_name );
}

static HRESULT WINAPI xslt_processor_GetTrustLevel( IInspectable *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static const IInspectableVtbl xslt_processor_vtbl =
{
    xslt_processor_QueryInterface,
    xslt_processor_AddRef,
    xslt_processor_Release,
    xslt_processor_GetIids,
    xslt_processor_GetRuntimeClassName,
    xslt_processor_GetTrustLevel,
};

struct xslt_processor_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct xslt_processor_factory *impl_from_xslt_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct xslt_processor_factory, IActivationFactory_iface );
}

static HRESULT WINAPI xslt_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
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

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI xslt_factory_AddRef( IActivationFactory *iface )
{
    struct xslt_processor_factory *impl = impl_from_xslt_factory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI xslt_factory_Release( IActivationFactory *iface )
{
    struct xslt_processor_factory *impl = impl_from_xslt_factory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI xslt_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    *iid_count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI xslt_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    return WindowsCreateString( XSLT_PROCESSOR_CLASS, wcslen( XSLT_PROCESSOR_CLASS ), class_name );
}

static HRESULT WINAPI xslt_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (trust_level) *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI xslt_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct xslt_processor *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IInspectable_iface.lpVtbl = &xslt_processor_vtbl;
    impl->ref = 1;
    *instance = &impl->IInspectable_iface;
    return S_OK;
}

static const IActivationFactoryVtbl xslt_factory_vtbl =
{
    xslt_factory_QueryInterface,
    xslt_factory_AddRef,
    xslt_factory_Release,
    xslt_factory_GetIids,
    xslt_factory_GetRuntimeClassName,
    xslt_factory_GetTrustLevel,
    xslt_factory_ActivateInstance,
};

static struct xslt_processor_factory xslt_processor_factory =
{
    { &xslt_factory_vtbl },
    1
};

/* ------------------------------------------------------------------ exports */

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    if (!factory) return E_POINTER;
    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Data_Xml_Dom_XmlDocument ))
    {
        IUnknown_AddRef( (IUnknown *)&xml_document_factory.IActivationFactory_iface );
        *factory = &xml_document_factory.IActivationFactory_iface;
    }
    else if (!wcscmp( buffer, XSLT_PROCESSOR_CLASS ))
    {
        IUnknown_AddRef( (IUnknown *)&xslt_processor_factory.IActivationFactory_iface );
        *factory = &xslt_processor_factory.IActivationFactory_iface;
    }

    if (*factory) return S_OK;
    return E_NOINTERFACE;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

/* The module's activatable class registration (classes.idl) is carried in its
 * registry resource, so registering the module seeds the classes; the COM
 * classes msxml6 stands in for are msxml3's, forwarded by the spec. */
HRESULT WINAPI DllRegisterServer(void)
{
    return __wine_register_resources();
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return __wine_unregister_resources();
}
