/*
 * WinRT Windows.Data.Xml.Dom.XmlDocument activation tests
 *
 * Copyright 2026 Wine project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stddef.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"

#include "winstring.h"
#include "roapi.h"
#include "activation.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Data_Xml_Dom
#include "windows.data.xml.dom.h"

#include "wine/test.h"

/* The interface ids the test queries with, copied from the include/ descriptions
 * (never invented): activation.idl:23, inspectable.idl:31, objidlbase.idl:91 and
 * the uuid() lines of windows.data.xml.dom.idl cited per entry below.  They are
 * file-local constants because the other tests of this directory define the
 * classic COM ids of the same test binary. */
static const IID IID_IActivationFactory_loc = {0x00000035, 0x0000, 0x0000, {0xc0,0x00, 0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID IID_IInspectable_loc       = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c,0x5a, 0xd7,0xaa,0x65,0x10,0x1e,0x90}};
static const IID IID_IAgileObject_loc       = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0,0xff, 0xee,0x64,0xca,0x8f,0x5b,0x90}};
/* windows.data.xml.dom.idl:266 IXmlDocumentStatics */
static const IID IID_IXmlDocumentStatics_loc = {0x5543d254, 0xd757, 0x4b79, {0x95,0x39, 0x23,0x2b,0x18,0xf5,0x0b,0xf1}};
/* :252 IXmlDocumentIO2 */
static const IID IID_IXmlDocumentIO2_loc     = {0x5d034661, 0x7bd8, 0x4ad5, {0x9e,0xbf, 0x81,0xe6,0x34,0x72,0x63,0xb1}};
/* :240 IXmlDocumentIO */
static const IID IID_IXmlDocumentIO_loc      = {0x6cd0e74e, 0xee65, 0x4489, {0x9e,0xbf, 0xca,0x43,0xe8,0x7b,0xa6,0x37}};
/* :197 IXmlDocument */
static const IID IID_IXmlDocument_loc        = {0xf7f3a506, 0x1e87, 0x42d6, {0xbc,0xfb, 0xb8,0xc8,0x09,0xfa,0x54,0x94}};
/* :314 IXmlElement */
static const IID IID_IXmlElement_loc         = {0x2dfb8a1f, 0x6b10, 0x4ef8, {0x9f,0x83, 0xef,0xcc,0xe8,0xfa,0xec,0x37}};
/* :370 IXmlNamedNodeMap */
static const IID IID_IXmlNamedNodeMap_loc    = {0xb3a69eb0, 0xaab0, 0x4b82, {0xa6,0xfa, 0xb1,0x45,0x3f,0x7c,0x02,0x1b}};
/* :422 IXmlNodeList */
static const IID IID_IXmlNodeList_loc        = {0x8c60ad77, 0x83a4, 0x4ec1, {0x9c,0x54, 0x7b,0xa4,0x29,0xe1,0x3d,0xa6}};
/* :434 IXmlNodeSelector */
static const IID IID_IXmlNodeSelector_loc    = {0x63dbba8b, 0xd0db, 0x4fe1, {0xb7,0x45, 0xf9,0x43,0x3a,0xfd,0xc2,0x5b}};
/* :446 IXmlNodeSerializer */
static const IID IID_IXmlNodeSerializer_loc  = {0x5cc5b382, 0xe6dd, 0x4991, {0xab,0xef, 0x06,0xd8,0xd2,0xe7,0xbd,0x0c}};
/* :388 IXmlNode */
static const IID IID_IXmlNode_loc            = {0x1c741d59, 0x2122, 0x47d5, {0xa8,0x56, 0x83,0xf3,0xd4,0x21,0x48,0x75}};
/* the closed pinterface instantiations of windows.foundation.collections.idl over
 * IXmlNode, as the generated windows.data.xml.dom.h emits them */
static const IID IID_IVectorView_IXmlNode_loc = {0x139d959e, 0xe7b5, 0x5cb6, {0xa5,0x96, 0x4b,0x54,0x44,0x78,0xda,0x9b}};
static const IID IID_IIterable_IXmlNode_loc   = {0xf1146ffc, 0x8c92, 0x56e8, {0x93,0xf1, 0x71,0x1f,0x86,0x72,0x26,0x33}};
static const IID IID_IIterator_IXmlNode_loc   = {0x3833a35e, 0x2c61, 0x56bd, {0xb0,0x93, 0x36,0x94,0x16,0x5f,0x88,0x98}};

#define check_interface( a, b, c ) check_interface_( __LINE__, a, b, c )
static void check_interface_( unsigned int line, void *iface_ptr, REFIID iid, BOOL supported )
{
    HRESULT hr, expected;
    IUnknown *iface = iface_ptr, *unk;

    expected = supported ? S_OK : E_NOINTERFACE;
    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == expected, "got hr %#lx, expected %#lx.\n", hr, expected );
    if (SUCCEEDED( hr )) IUnknown_Release( unk );
}

static void check_hstring_( unsigned int line, HSTRING str, const WCHAR *expected )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( str, NULL );

    ok_(__FILE__, line)( !lstrcmpW( buffer, expected ), "got %s, expected %s.\n",
                         wine_dbgstr_w( buffer ), wine_dbgstr_w( expected ) );
    WindowsDeleteString( str );
}

#define check_hstring( a, b ) check_hstring_( __LINE__, a, b )

static void test_XmlDocument(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_Data_Xml_Dom_XmlDocument;
    __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode *vectorview;
    __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode *iterable;
    __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode *iterator;
    static const WCHAR xmlW[] = L"<a><b/></a>";
    IInspectable *instance;
    IXmlDocumentIO *document_io;
    IXmlNodeSerializer *serializer;
    IXmlNodeSelector *selector;
    IXmlNamedNodeMap *attributes;
    IXmlNodeList *node_list;
    IXmlDocument *document;
    IActivationFactory *factory;
    IXmlElement *element;
    IXmlNode *doc_node, *element_node, *item_node;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory_loc, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (hr != S_OK)
    {
        win_skip( "%s runtimeclass not available, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    /* the factory answers the class's statics and activates a document */
    check_interface( factory, &IID_IActivationFactory_loc, TRUE );
    check_interface( factory, &IID_IXmlDocumentStatics_loc, TRUE );

    hr = IActivationFactory_ActivateInstance( factory, &instance );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IInspectable_QueryInterface( instance, &IID_IXmlDocument_loc, (void **)&document );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( instance, &IID_IXmlDocumentIO_loc, (void **)&document_io );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( instance, &IID_IXmlNode_loc, (void **)&doc_node );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( instance, &IID_IXmlNodeSelector_loc, (void **)&selector );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( instance, &IID_IXmlNodeSerializer_loc, (void **)&serializer );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    /* the document is not an element */
    check_interface( instance, &IID_IXmlElement_loc, FALSE );

    /* a loaded document round-trips through GetXml */
    hr = WindowsCreateString( xmlW, wcslen( xmlW ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IXmlDocumentIO_LoadXml( document_io, str );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IXmlNodeSerializer_GetXml( serializer, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_hstring( str, L"<a><b/></a>" );

    /* the document element is the projected XmlElement */
    hr = IXmlDocument_get_DocumentElement( document, &element );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IXmlElement_get_TagName( element, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_hstring( str, L"a" );

    hr = IXmlElement_QueryInterface( element, &IID_IXmlNode_loc, (void **)&element_node );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IXmlNode_get_NodeName( element_node, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_hstring( str, L"a" );

    /* the element node reports its type and the document as its parent */
    {
        NodeType type = (NodeType)0xc0ffee, parent_type = (NodeType)0xc0ffee;
        IXmlNode *parent = (IXmlNode *)0x1;

        hr = IXmlNode_get_NodeType( element_node, &type );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( type == NodeType_ElementNode, "got type %d.\n", type );

        hr = IXmlNode_get_ParentNode( element_node, &parent );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( parent, "got parent %p.\n", parent );
        if (parent)
        {
            hr = IXmlNode_get_NodeType( parent, &parent_type );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            ok( parent_type == NodeType_DocumentNode, "got type %d.\n", parent_type );
            IXmlNode_Release( parent );
        }
        IXmlNode_Release( element_node );

        hr = IXmlNode_get_Attributes( element_node, &attributes );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (attributes)
        {
            UINT32 length = 0xc0ffee;
            hr = IXmlNamedNodeMap_get_Length( attributes, &length );
            ok( hr == S_OK && !length, "got hr %#lx, length %u.\n", hr, length );
            check_interface( attributes,
                    &IID_IVectorView_IXmlNode_loc, TRUE );
            check_interface( attributes,
                    &IID_IIterable_IXmlNode_loc, TRUE );
            IXmlNamedNodeMap_Release( attributes );
        }
    }
    IXmlElement_Release( element );

    /* XPath selects the child element */
    hr = WindowsCreateString( L"//b", wcslen( L"//b" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IXmlNodeSelector_SelectNodes( selector, str, &node_list );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    {
        UINT32 length = 0xc0ffee, size = 0xc0ffee, count = 0xc0ffee, index = 0xc0ffee;
        BOOLEAN has_current = TRUE, moved = TRUE, found = TRUE;

        hr = IXmlNodeList_get_Length( node_list, &length );
        ok( hr == S_OK && length == 1, "got hr %#lx, length %u.\n", hr, length );

        check_interface( node_list, &IID_IXmlNodeList_loc, TRUE );
        check_interface( node_list, &IID_IVectorView_IXmlNode_loc, TRUE );
        check_interface( node_list, &IID_IIterable_IXmlNode_loc, TRUE );

        hr = IXmlNodeList_QueryInterface( node_list,
                &IID_IVectorView_IXmlNode_loc, (void **)&vectorview );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode_get_Size( vectorview, &size );
        ok( hr == S_OK && size == 1, "got hr %#lx, size %u.\n", hr, size );
        hr = __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode_GetAt( vectorview, 0, &item_node );
        ok( hr == S_OK && item_node, "got hr %#lx, node %p.\n", hr, item_node );
        if (item_node)
        {
            hr = IXmlNode_get_NodeName( item_node, &str );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            check_hstring( str, L"b" );
            IXmlNode_Release( item_node );
        }
        hr = __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode_GetAt( vectorview, 1, &item_node );
        ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );
        hr = __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode_IndexOf( vectorview, NULL, &index, &found );
        ok( hr == S_OK && !found, "got hr %#lx, found %d.\n", hr, found );
        hr = __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode_GetMany( vectorview, 0, 4, NULL, &count );
        ok( hr == E_POINTER, "got hr %#lx.\n", hr );
        __FIVectorView_1_Windows__CData__CXml__CDom__CIXmlNode_Release( vectorview );

        /* the same set enumerates through the closed pinterface instantiation */
        hr = IXmlNodeList_QueryInterface( node_list,
                &IID_IIterable_IXmlNode_loc, (void **)&iterable );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode_First( iterable, &iterator );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode_MoveNext( iterator, &moved );
        ok( hr == S_OK && moved, "got hr %#lx, moved %d.\n", hr, moved );
        hr = __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode_get_HasCurrent( iterator, &has_current );
        ok( hr == S_OK && has_current, "got hr %#lx, has_current %d.\n", hr, has_current );
        hr = __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode_get_Current( iterator, &item_node );
        ok( hr == S_OK && item_node, "got hr %#lx, node %p.\n", hr, item_node );
        if (item_node)
        {
            hr = IXmlNode_get_NodeName( item_node, &str );
            ok( hr == S_OK, "got hr %#lx.\n", hr );
            check_hstring( str, L"b" );
            IXmlNode_Release( item_node );
        }
        hr = __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode_MoveNext( iterator, &moved );
        ok( hr == S_OK && !moved, "got hr %#lx, moved %d.\n", hr, moved );
        __FIIterator_1_Windows__CData__CXml__CDom__CIXmlNode_Release( iterator );
        __FIIterable_1_Windows__CData__CXml__CDom__CIXmlNode_Release( iterable );
    }
    IXmlNodeList_Release( node_list );

    /* the projected document element through the serializer of the document node */
    hr = IXmlNodeSerializer_GetXml( serializer, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_hstring( str, L"<a><b/></a>" );

    ref = IXmlNodeSerializer_Release( serializer );
    ok( ref >= 1, "got ref %ld.\n", ref );
    ref = IXmlNodeSelector_Release( selector );
    ok( ref >= 1, "got ref %ld.\n", ref );
    ref = IXmlNode_Release( doc_node );
    ok( ref >= 1, "got ref %ld.\n", ref );
    ref = IXmlDocumentIO_Release( document_io );
    ok( ref >= 1, "got ref %ld.\n", ref );
    ref = IXmlDocument_Release( document );
    ok( ref >= 1, "got ref %ld.\n", ref );
    ref = IInspectable_Release( instance );
    ok( !ref, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref >= 1, "got ref %ld.\n", ref );
}

static void test_XsltProcessor(void)
{
    static const WCHAR class_name[] = L"Windows.Data.Xml.Xsl.XsltProcessor";
    IInspectable *instance;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory_loc, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (hr != S_OK)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    hr = IActivationFactory_ActivateInstance( factory, &instance );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( instance, &IID_IInspectable_loc, TRUE );
    check_interface( instance, &IID_IAgileObject_loc, TRUE );

    IInspectable_Release( instance );
    IActivationFactory_Release( factory );
}

START_TEST(xmlwinrt)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    test_XmlDocument();
    test_XsltProcessor();

    RoUninitialize();
}
