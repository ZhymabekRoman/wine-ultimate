/*
 * Copyright (C) 2024 Mohamad Al-Jaf
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
#define COBJMACROS
#include "initguid.h"
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Data_Json
#include "windows.data.json.h"

#include "wine/test.h"

#define check_interface( obj, iid ) check_interface_( __LINE__, obj, iid )
static void check_interface_( unsigned int line, void *obj, const IID *iid )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    IUnknown_Release( unk );
}

static void test_JsonArrayStatics(void)
{
    static const WCHAR *json_value_statics_name = L"Windows.Data.Json.JsonValue";
    static const WCHAR *json_array_name = L"Windows.Data.Json.JsonArray";
    IJsonValueStatics *json_value_statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    IJsonObject *child_object = (void *)0xdeadbeef;
    IJsonArray *child_array = (void *)0xdeadbeef;
    IJsonArray *json_array = (void *)0xdeadbeef;
    IJsonValue *json_value = (void *)0xdeadbeef;
    BOOLEAN child_boolean;
    HSTRING child_string;
    DOUBLE child_number;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( json_value_statics_name, wcslen( json_value_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( json_value_statics_name ) );
        return;
    }

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonValueStatics, (void **)&json_value_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IActivationFactory_Release( factory );

    hr = WindowsCreateString( json_array_name, wcslen( json_array_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( json_array_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonArray, (void **)&json_array );
    ok( hr == E_NOINTERFACE, "got hr %#lx.\n", hr );

    hr = WindowsCreateString( json_array_name, wcslen( json_array_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( str, &inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    hr = IInspectable_QueryInterface( inspectable, &IID_IJsonArray, (void **)&json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( inspectable, &IID_IAgileObject );

    hr = IJsonArray_GetObjectAt( json_array, 0, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );

    IJsonArray_Release( json_array );
    IInspectable_Release( inspectable );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );

    hr = WindowsCreateString( L"[{}]", wcslen( L"[{}]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = IJsonValue_GetArray( json_value, &json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( json_value );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonObject_Release( child_object );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    IJsonArray_Release( json_array );

    hr = WindowsCreateString( L"[[]]", wcslen( L"[[]]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = IJsonValue_GetArray( json_value, &json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( json_value );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonArray_Release( child_array );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    IJsonArray_Release( json_array );

    hr = WindowsCreateString( L"[\"Hello, World!\"]", wcslen( L"[\"Hello, World!\"]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = IJsonValue_GetArray( json_value, &json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( json_value );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( child_string );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    IJsonArray_Release( json_array );

    hr = WindowsCreateString( L"[12.6]", wcslen( L"[12.6]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = IJsonValue_GetArray( json_value, &json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( json_value );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    IJsonArray_Release( json_array );

    hr = WindowsCreateString( L"[true]", wcslen( L"[true]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = IJsonValue_GetArray( json_value, &json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( json_value );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonArray_Release( json_array );

    IJsonValueStatics_Release( json_value_statics );
}

static void test_JsonObjectStatics(void)
{
    static const WCHAR *json_value_statics_name = L"Windows.Data.Json.JsonValue";
    static const WCHAR *json_object_name = L"Windows.Data.Json.JsonObject";
    IJsonValueStatics *json_value_statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    IJsonObject *child_object = (void *)0xdeadbeef;
    IJsonObject *json_object = (void *)0xdeadbeef;
    IJsonArray *child_array = (void *)0xdeadbeef;
    IJsonValue *child_value = (void *)0xdeadbeef;
    BOOLEAN child_boolean;
    HSTRING child_string;
    DOUBLE child_number;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( json_value_statics_name, wcslen( json_value_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( json_value_statics_name ) );
        return;
    }

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonValueStatics, (void **)&json_value_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IActivationFactory_Release( factory );

    hr = WindowsCreateString( json_object_name, wcslen( json_object_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( json_object_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonObject, (void **)&json_object );
    ok( hr == E_NOINTERFACE, "got hr %#lx.\n", hr );

    hr = WindowsCreateString( json_object_name, wcslen( json_object_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( str, &inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    hr = IInspectable_QueryInterface( inspectable, &IID_IJsonObject, (void **)&json_object );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( inspectable, &IID_IAgileObject );
    IInspectable_Release( inspectable );

    hr = WindowsCreateString( L"key", wcslen( L"key" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* key pair does not exist */

    hr = IJsonObject_GetNamedValue( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedValue( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedValue( json_object, NULL, &child_value );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    hr = IJsonObject_GetNamedObject( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedObject( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedObject( json_object, NULL, &child_object );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    hr = IJsonObject_GetNamedArray( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, NULL, &child_array );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    hr = IJsonObject_GetNamedString( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, NULL, &child_string );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    hr = IJsonObject_GetNamedNumber( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, NULL, &child_number );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    hr = IJsonObject_GetNamedBoolean( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, NULL, &child_boolean );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    /* key pair exists */

    WindowsDeleteString( str );
    hr = WindowsCreateString( L"{}", wcslen( L"{}" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = WindowsCreateString( L"key", wcslen( L"key" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_SetNamedValue( json_object, str, child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonObject_Release( child_object );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

    WindowsDeleteString( str );
    hr = WindowsCreateString( L"[]", wcslen( L"[]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = WindowsCreateString( L"key", wcslen( L"key" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_SetNamedValue( json_object, str, child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonArray_Release( child_array );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

    hr = IJsonValueStatics_CreateStringValue( json_value_statics, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_SetNamedValue( json_object, str, child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( child_string );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

    hr = IJsonValueStatics_CreateNumberValue( json_value_statics, 10, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_SetNamedValue( json_object, str, child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

    hr = IJsonValueStatics_CreateBooleanValue( json_value_statics, FALSE, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_SetNamedValue( json_object, str, child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    IJsonObject_Release( json_object );
    IJsonValueStatics_Release( json_value_statics );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

#define check_json( json_value_statics, json, expected_json_value_type, valid ) check_json_( __LINE__, json_value_statics, json, expected_json_value_type, valid )
static void check_json_( unsigned int line, IJsonValueStatics *json_value_statics, const WCHAR *json, JsonValueType expected_json_value_type, boolean valid )
{
    HSTRING str = NULL, parsed_str = NULL, empty_space = NULL;
    IJsonObject *json_object = (void *)0xdeadbeef;
    IJsonArray *json_array = (void *)0xdeadbeef;
    IJsonValue *json_value = (void *)0xdeadbeef;
    boolean parsed_boolean, expected_boolean;
    JsonValueType json_value_type;
    DOUBLE parsed_num;
    HRESULT hr;
    LONG ref;
    int res;

    hr = WindowsCreateString( json, wcslen( json ), &str );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    if (!valid)
    {
        if (expected_json_value_type == JsonValueType_Number)
            ok_(__FILE__, line)( hr == WEB_E_INVALID_JSON_NUMBER, "got hr %#lx.\n", hr );
        else
            ok_(__FILE__, line)( hr == WEB_E_INVALID_JSON_STRING, "got hr %#lx.\n", hr );

        WindowsDeleteString( str );
        return;
    }

    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr)) return;
    hr = IJsonValue_get_ValueType( json_value, &json_value_type );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    ok_(__FILE__, line)( json_value_type == expected_json_value_type, "got json_value_type %d.\n", json_value_type );

    switch (expected_json_value_type)
    {
        case JsonValueType_Null:
            hr = IJsonValue_GetString( json_value, NULL );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetString( json_value, &parsed_str );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

            hr = IJsonValue_GetNumber( json_value, NULL );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetNumber( json_value, &parsed_num );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

            hr = IJsonValue_GetBoolean( json_value, NULL );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetBoolean( json_value, &parsed_boolean );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

            hr = IJsonValue_GetArray( json_value, NULL );
            ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetArray( json_value, &json_array );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
            if (hr == S_OK) IJsonArray_Release( json_array );

            hr = IJsonValue_GetObject( json_value, NULL );
            ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetObject( json_value, &json_object );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
            if (hr == S_OK) IJsonObject_Release( json_object );
            break;
        case JsonValueType_Boolean:
            hr = WindowsCreateString( L" ", wcslen( L" " ), &empty_space );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            hr = WindowsTrimStringStart( str, empty_space, &parsed_str );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            hr = WindowsTrimStringEnd( parsed_str, empty_space, &parsed_str );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            expected_boolean = !wcscmp( L"true", WindowsGetStringRawBuffer( parsed_str, NULL ) );

            hr = IJsonValue_GetBoolean( json_value, NULL );
            ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetBoolean( json_value, &parsed_boolean );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            ok_(__FILE__, line)( parsed_boolean == expected_boolean, "boolean mismatch, got %d, expected %d.\n", parsed_boolean, expected_boolean );
            break;
        case JsonValueType_Number:
            parsed_num = 0xdeadbeef;
            hr = IJsonValue_GetNumber( json_value, NULL );
            ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetNumber( json_value, &parsed_num );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            ok_(__FILE__, line)( parsed_num != 0xdeadbeef, "failed to get parsed_num\n" );
            break;
        case JsonValueType_String:
            hr = IJsonValue_GetString( json_value, NULL );
            ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetString( json_value, &parsed_str );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            hr = WindowsCompareStringOrdinal( str, parsed_str, &res );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            ok_(__FILE__, line)( res != 0, "got same HSTRINGS str = %s, parsed_str = %s.\n", wine_dbgstr_hstring( str ), wine_dbgstr_hstring( parsed_str ) );
            break;
        case JsonValueType_Array:
            hr = IJsonValue_GetArray( json_value, &json_array );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            if (hr == S_OK) IJsonArray_Release( json_array );
            break;
        case JsonValueType_Object:
            hr = IJsonValue_GetObject( json_value, &json_object );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            if (hr == S_OK) IJsonObject_Release( json_object );
            break;
    }

    WindowsDeleteString( empty_space );
    WindowsDeleteString( parsed_str );
    WindowsDeleteString( str );
    ref = IJsonValue_Release( json_value );
    ok_(__FILE__, line)( ref == 0, "got ref %ld.\n", ref );
}

WCHAR *create_non_null_terminated( const WCHAR *str )
{
    UINT len = wcslen( str );
    WCHAR *buffer = malloc( (len + 1) * sizeof( WCHAR ) );
    if (buffer)
    {
        memcpy( buffer, str, len * sizeof( WCHAR ) );
        buffer[len] = 1;
        return buffer;
    }
    trace( "create_non_null_terminated failed to return a string\n" );
    return NULL;
}

#define check_non_null_terminated_json( json_value_statics, json, expected_json_value_type ) \
        check_non_null_terminated_json_( __LINE__, json_value_statics, json, expected_json_value_type )
static void check_non_null_terminated_json_( unsigned int line, IJsonValueStatics *json_value_statics, const WCHAR *json, JsonValueType expected_json_value_type )
{
    WCHAR *str = create_non_null_terminated( json );
    check_json_( line, json_value_statics, str, expected_json_value_type, FALSE );
    free( str );
}

static void test_JsonValueStatics(void)
{
    static const WCHAR *json_value_statics_name = L"Windows.Data.Json.JsonValue";
    IJsonValueStatics *json_value_statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IJsonValue *json_value = (void *)0xdeadbeef;
    JsonValueType json_value_type;
    HSTRING str = NULL;
    const WCHAR *json;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( json_value_statics_name, wcslen( json_value_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( json_value_statics_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonValueStatics, (void **)&json_value_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IJsonValueStatics_CreateBooleanValue( json_value_statics, FALSE, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_CreateBooleanValue( json_value_statics, FALSE, &json_value );
    ok( hr == S_OK, "got hr %#lx,\n", hr );
    hr = IJsonValue_get_ValueType( json_value, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValue_get_ValueType( json_value, &json_value_type );
    ok( json_value_type == JsonValueType_Boolean, "got JsonValueType %d.\n", json_value_type );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IJsonValue_Release( json_value );
    ok( ref == 0, "got ref %ld.\n", ref );

    hr = IJsonValueStatics_CreateNumberValue( json_value_statics, 0, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_CreateNumberValue( json_value_statics, 0, &json_value );
    ok( hr == S_OK, "got hr %#lx,\n", hr );
    hr = IJsonValue_get_ValueType( json_value, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValue_get_ValueType( json_value, &json_value_type );
    ok( json_value_type == JsonValueType_Number, "got JsonValueType %d.\n", json_value_type );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IJsonValue_Release( json_value );
    ok( ref == 0, "got ref %ld.\n", ref );

    hr = IJsonValueStatics_CreateStringValue( json_value_statics, NULL, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValue_get_ValueType( json_value, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValue_get_ValueType( json_value, &json_value_type );
    ok( json_value_type == JsonValueType_String, "got JsonValueType %d.\n", json_value_type );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IJsonValue_Release( json_value );
    ok( ref == 0, "got ref %ld.\n", ref );
    hr = WindowsCreateString( L"Wine", wcslen( L"Wine" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_CreateStringValue( json_value_statics, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_CreateStringValue( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValue_get_ValueType( json_value, &json_value_type );
    ok( json_value_type == JsonValueType_String, "got JsonValueType %d.\n", json_value_type );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    ref = IJsonValue_Release( json_value );
    ok( ref == 0, "got ref %ld.\n", ref );

    hr = IJsonValueStatics_Parse( json_value_statics, NULL, &json_value );
    ok( hr == WEB_E_INVALID_JSON_STRING, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( L"Wine", wcslen( L"Wine" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == WEB_E_INVALID_JSON_STRING, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    /* Valid JSON */

    json = L"\"Wine\\\"\"";
    hr = WindowsCreateString( json, wcslen( json ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    if (SUCCEEDED(hr))
    {
        HSTRING parsed_str = NULL;
        int res;

        json = L"Wine\"";
        hr = WindowsCreateString( json, wcslen( json ), &str );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IJsonValue_GetString( json_value, &parsed_str );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = WindowsCompareStringOrdinal( str, parsed_str, &res );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( res == 0, "got different HSTRINGS str = %s, parsed_str = %s.\n", wine_dbgstr_hstring( str ), wine_dbgstr_hstring( parsed_str ) );

        WindowsDeleteString( parsed_str );
        WindowsDeleteString( str );
        IJsonValue_Release( json_value );
    }

    json = L"\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0000\\u0057\\u0069\\u006e\\u0065\\udAbC\\uDcEf\"";
    hr = WindowsCreateString( json, wcslen( json ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    if (SUCCEEDED(hr))
    {
        HSTRING parsed_str = NULL;
        int res;

        json = L"\"\\/\b\f\n\r\t\0Wine\U000BF0EF";
        hr = WindowsCreateString( json, 15, &str );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IJsonValue_GetString( json_value, &parsed_str );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = WindowsCompareStringOrdinal( str, parsed_str, &res );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( res == 0, "got different HSTRINGS str = %s, parsed_str = %s.\n", wine_dbgstr_hstring( str ), wine_dbgstr_hstring( parsed_str ) );

        WindowsDeleteString( parsed_str );
        WindowsDeleteString( str );
        IJsonValue_Release( json_value );
    }

    json = L"null";
    check_json( json_value_statics, json, JsonValueType_Null, TRUE );
    json = L"false";
    check_json( json_value_statics, json, JsonValueType_Boolean, TRUE );
    json = L" true ";
    check_json( json_value_statics, json, JsonValueType_Boolean, TRUE );
    json = L"\"true\"";
    check_json( json_value_statics, json, JsonValueType_String, TRUE );
    json = L" 9.22 ";
    check_json( json_value_statics, json, JsonValueType_Number, TRUE );
    json = L" \"The Wine     Project\"";
    check_json( json_value_statics, json, JsonValueType_String, TRUE );
    json = L"\r\t\n \"The Wine     Project\"";
    check_json( json_value_statics, json, JsonValueType_String, TRUE );
    json = L"[\"Wine\", \"Linux\"]";
    check_json( json_value_statics, json, JsonValueType_Array, TRUE );
    json = L"{"
            "    \"Wine\": \"The Wine Project\","
            "    \"Linux\": [\"Arch\", \"BTW\"]"
            "}";
    check_json( json_value_statics, json, JsonValueType_Object, TRUE );

    /* Invalid JSON */

    json = L"null";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_Null );
    json = L"false";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_Boolean );
    json = L" true ";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_Boolean );
    json = L"\"true\"";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_String );
    json = L" 9.22 ";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_String );
    json = L" \"Wine\"";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_String );
    json = L"[\"Wine\", \"Linux\"]";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_Array );
    json = L"{"
            "    \"Wine\": \"The Wine Project\","
            "    \"Linux\": [\"Arch\", \"BTW\"]"
            "}";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_Object );

    json = L"\" \"\"";
    hr = WindowsCreateString( json, wcslen( json ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == WEB_E_INVALID_JSON_STRING, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    json = L"True";
    check_json( json_value_statics, json, JsonValueType_Boolean, FALSE );
    json = L"1.7976931348623158e+3080";
    check_json( json_value_statics, json, JsonValueType_Number, FALSE );
    json = L"2.2250738585072014e-3080";
    check_json( json_value_statics, json, JsonValueType_Number, FALSE );
    json = L" \"Wine\":";
    check_json( json_value_statics, json, JsonValueType_String, FALSE );
    json = L" \"The Wine \t Project\"";
    check_json( json_value_statics, json, JsonValueType_String, FALSE );
    json = L"\v \"The Wine     Project\"";
    check_json( json_value_statics, json, JsonValueType_String, FALSE );
    json = L"\"\\\"";
    check_json( json_value_statics, json, JsonValueType_String, FALSE );
    json = L"\"\\u123\"";
    check_json( json_value_statics, json, JsonValueType_String, FALSE );
    json = L"[\"Wine\" \"Linux\"]";
    check_json( json_value_statics, json, JsonValueType_Array, FALSE );
    json = L"[\"Wine\", \"Linux\",]";
    check_json( json_value_statics, json, JsonValueType_Array, FALSE );
    json = L"{"
            "    \"Wine\": \"The Wine Project\","
            "    \"Linux\": [\"Arch\", \"BTW\"]"
            "";
    check_json( json_value_statics, json, JsonValueType_Object, FALSE );
    json = L"{"
            "    \"Wine\": \"The Wine Project\","
            "    \"Linux\": [\"Arch\", \"BTW\"],"
            "}";
    check_json( json_value_statics, json, JsonValueType_Object, FALSE );

    ref = IJsonValueStatics_Release( json_value_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

/* -------------------------------------------------------------------------
 * Windows.Web.Http
 * ------------------------------------------------------------------------- */

static const GUID IID_web_IClosable = {0x3028b027, 0x6029, 0x47e6, {0x81, 0xa1, 0x65, 0x5b, 0x3b, 0x3c, 0x52, 0x0f}};
static const GUID IID_web_IStringable = {0x96369f54, 0x8eb6, 0x48f0, {0xac, 0x91, 0xdd, 0x13, 0x7f, 0x61, 0xdf, 0xb7}};

static const GUID IID_IHttpClient = {0x7fda1151, 0x3574, 0x4880, {0xa8, 0xba, 0xe6, 0xb1, 0xe0, 0x06, 0x1f, 0x3d}};
static const GUID IID_IHttpClient2 = {0xcdd83348, 0xe8b7, 0x4cec, {0xb1, 0xb0, 0xdc, 0x45, 0x5f, 0xe7, 0x2c, 0x92}};
static const GUID IID_IHttpClientFactory = {0xc30c4eca, 0xe3fa, 0x4f99, {0xaf, 0xb4, 0x63, 0xcc, 0x65, 0x00, 0x94, 0x62}};

static const GUID IID_IHttpRequestMessage = {0xf5762b3c, 0x74d4, 0x4811, {0xb5, 0xdc, 0x9f, 0x8b, 0x4e, 0x2f, 0x9a, 0xbf}};
static const GUID IID_IHttpRequestMessageFactory = {0x5bac994e, 0x3886, 0x412e, {0xae, 0xc3, 0x52, 0xec, 0x7f, 0x25, 0x61, 0x6f}};

static const GUID IID_IHttpResponseMessage = {0xfee200fb, 0x8664, 0x44e0, {0x95, 0xd9, 0x42, 0x69, 0x61, 0x99, 0xbf, 0xfc}};
static const GUID IID_IHttpResponseMessageFactory = {0x52a8af99, 0xf095, 0x43da, {0xb6, 0x0f, 0x7c, 0xfc, 0x2b, 0xc7, 0xea, 0x2f}};

static const GUID IID_IHttpContent = {0x6b14a441, 0xfba7, 0x4bd2, {0xaf, 0x0a, 0x83, 0x9d, 0xe7, 0xc2, 0x95, 0xda}};
static const GUID IID_IHttpStringContentFactory = {0x46649d5b, 0x2e93, 0x48eb, {0x8e, 0x61, 0x19, 0x67, 0x78, 0x78, 0xe5, 0x7f}};

static const GUID IID_IHttpMethod = {0x728d4022, 0x700d, 0x4fe0, {0xaf, 0xa5, 0x40, 0x29, 0x9c, 0x58, 0xdb, 0xfd}};
static const GUID IID_IHttpMethodFactory = {0x3c51d10d, 0x36d7, 0x40f8, {0xa8, 0x6d, 0xe7, 0x59, 0xca, 0xf2, 0xf8, 0x3f}};
static const GUID IID_IHttpMethodStatics = {0x64d171f0, 0xd99a, 0x4153, {0x8d, 0xc6, 0xd6, 0x8c, 0xc4, 0xcc, 0xe3, 0x17}};

static const GUID IID_IHttpRequestHeaderCollection = {0xaf40329b, 0xb544, 0x469b, {0x86, 0xb9, 0xac, 0x3d, 0x46, 0x6f, 0xea, 0x36}};
static const GUID IID_IHttpResponseHeaderCollection = {0x7a990969, 0xfa3f, 0x41ed, {0xaa, 0xc6, 0xbf, 0x95, 0x79, 0x75, 0xc1, 0x6b}};
static const GUID IID_IHttpContentHeaderCollection = {0x40612a44, 0x47ae, 0x4b7e, {0x91, 0x24, 0x69, 0x62, 0x8b, 0x64, 0xaa, 0x18}};

static const GUID IID_IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress =
    {0x9d213fdb, 0xaa1c, 0x555a, {0x96, 0x1f, 0xcb, 0x0f, 0x1d, 0xcc, 0xb6, 0x06}};
static const GUID IID_IReferenceTracker = {0x11d3b13a, 0x180e, 0x4789, {0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6}};

/* Local declarations of the Windows.Web.Http interface surface, following
 * the widl-generated ABI of the Windows SDK metadata. */

typedef struct IHttpContent IHttpContent;
typedef struct IHttpMethod IHttpMethod;
typedef struct IHttpRequestMessage IHttpRequestMessage;
typedef struct IHttpResponseMessage IHttpResponseMessage;
typedef struct IHttpHeaderCollection IHttpHeaderCollection;
typedef struct IHttpClient IHttpClient;
typedef struct IHttpClient2 IHttpClient2;
typedef struct IWebClosable IWebClosable;
typedef struct IWebStringable IWebStringable;
typedef struct IAsyncOpHttp IAsyncOpHttp;

typedef struct IHttpContentVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IHttpContent *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IHttpContent *This);
    ULONG (STDMETHODCALLTYPE *Release)(IHttpContent *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IHttpContent *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IHttpContent *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IHttpContent *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *BufferAllAsync)(IHttpContent *This, void **op);
    HRESULT (STDMETHODCALLTYPE *ReadAsBufferAsync)(IHttpContent *This, void **op);
    HRESULT (STDMETHODCALLTYPE *ReadAsInputStreamAsync)(IHttpContent *This, void **op);
    HRESULT (STDMETHODCALLTYPE *ReadAsStringAsync)(IHttpContent *This, void **op);
    HRESULT (STDMETHODCALLTYPE *TryComputeLength)(IHttpContent *This, UINT64 *length, boolean *succeeded);
    HRESULT (STDMETHODCALLTYPE *WriteToStreamAsync)(IHttpContent *This, void *stream, void **op);
    HRESULT (STDMETHODCALLTYPE *get_Headers)(IHttpContent *This, IHttpHeaderCollection **value);
} IHttpContentVtbl;

struct IHttpContent
{
    const IHttpContentVtbl *lpVtbl;
};

typedef struct IHttpHeaderCollectionVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IHttpHeaderCollection *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IHttpHeaderCollection *This);
    ULONG (STDMETHODCALLTYPE *Release)(IHttpHeaderCollection *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IHttpHeaderCollection *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IHttpHeaderCollection *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IHttpHeaderCollection *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *Append)(IHttpHeaderCollection *This, HSTRING name, HSTRING value);
    HRESULT (STDMETHODCALLTYPE *TryAppendWithoutValidation)(IHttpHeaderCollection *This, HSTRING name, HSTRING value, boolean *succeeded);
} IHttpHeaderCollectionVtbl;

struct IHttpHeaderCollection
{
    const IHttpHeaderCollectionVtbl *lpVtbl;
};

typedef struct IHttpMethodVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IHttpMethod *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IHttpMethod *This);
    ULONG (STDMETHODCALLTYPE *Release)(IHttpMethod *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IHttpMethod *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IHttpMethod *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IHttpMethod *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_Method)(IHttpMethod *This, HSTRING *value);
} IHttpMethodVtbl;

struct IHttpMethod
{
    const IHttpMethodVtbl *lpVtbl;
};

typedef struct IHttpRequestMessageVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IHttpRequestMessage *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IHttpRequestMessage *This);
    ULONG (STDMETHODCALLTYPE *Release)(IHttpRequestMessage *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IHttpRequestMessage *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IHttpRequestMessage *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IHttpRequestMessage *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *get_Content)(IHttpRequestMessage *This, IHttpContent **value);
    HRESULT (STDMETHODCALLTYPE *put_Content)(IHttpRequestMessage *This, IHttpContent *value);
    HRESULT (STDMETHODCALLTYPE *get_Headers)(IHttpRequestMessage *This, IHttpHeaderCollection **value);
    HRESULT (STDMETHODCALLTYPE *get_Method)(IHttpRequestMessage *This, IHttpMethod **value);
    HRESULT (STDMETHODCALLTYPE *put_Method)(IHttpRequestMessage *This, IHttpMethod *value);
    HRESULT (STDMETHODCALLTYPE *get_Properties)(IHttpRequestMessage *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_RequestUri)(IHttpRequestMessage *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_RequestUri)(IHttpRequestMessage *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_TransportInformation)(IHttpRequestMessage *This, void **value);
} IHttpRequestMessageVtbl;

struct IHttpRequestMessage
{
    const IHttpRequestMessageVtbl *lpVtbl;
};

typedef struct IHttpResponseMessageVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IHttpResponseMessage *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IHttpResponseMessage *This);
    ULONG (STDMETHODCALLTYPE *Release)(IHttpResponseMessage *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IHttpResponseMessage *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IHttpResponseMessage *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IHttpResponseMessage *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *EnsureSuccessStatusCode)(IHttpResponseMessage *This, IHttpResponseMessage **value);
    HRESULT (STDMETHODCALLTYPE *get_Content)(IHttpResponseMessage *This, IHttpContent **value);
    HRESULT (STDMETHODCALLTYPE *put_Content)(IHttpResponseMessage *This, IHttpContent *value);
    HRESULT (STDMETHODCALLTYPE *get_Headers)(IHttpResponseMessage *This, IHttpHeaderCollection **value);
    HRESULT (STDMETHODCALLTYPE *get_IsSuccessStatusCode)(IHttpResponseMessage *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_ReasonPhrase)(IHttpResponseMessage *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *put_ReasonPhrase)(IHttpResponseMessage *This, HSTRING value);
    HRESULT (STDMETHODCALLTYPE *get_RequestMessage)(IHttpResponseMessage *This, IHttpRequestMessage **value);
    HRESULT (STDMETHODCALLTYPE *put_RequestMessage)(IHttpResponseMessage *This, IHttpRequestMessage *value);
    HRESULT (STDMETHODCALLTYPE *get_Source)(IHttpResponseMessage *This, INT32 *value);
    HRESULT (STDMETHODCALLTYPE *put_Source)(IHttpResponseMessage *This, INT32 value);
    HRESULT (STDMETHODCALLTYPE *get_StatusCode)(IHttpResponseMessage *This, INT32 *value);
    HRESULT (STDMETHODCALLTYPE *put_StatusCode)(IHttpResponseMessage *This, INT32 value);
    HRESULT (STDMETHODCALLTYPE *get_Version)(IHttpResponseMessage *This, INT32 *value);
    HRESULT (STDMETHODCALLTYPE *put_Version)(IHttpResponseMessage *This, INT32 value);
} IHttpResponseMessageVtbl;

struct IHttpResponseMessage
{
    const IHttpResponseMessageVtbl *lpVtbl;
};

typedef struct IHttpClientVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IHttpClient *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IHttpClient *This);
    ULONG (STDMETHODCALLTYPE *Release)(IHttpClient *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IHttpClient *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IHttpClient *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IHttpClient *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *DeleteAsync)(IHttpClient *This, void *uri, void **op);
    HRESULT (STDMETHODCALLTYPE *GetAsync)(IHttpClient *This, void *uri, void **op);
    HRESULT (STDMETHODCALLTYPE *GetAsyncCompletionOption)(IHttpClient *This, void *uri, INT32 option, void **op);
    HRESULT (STDMETHODCALLTYPE *GetBufferAsync)(IHttpClient *This, void *uri, void **op);
    HRESULT (STDMETHODCALLTYPE *GetInputStreamAsync)(IHttpClient *This, void *uri, void **op);
    HRESULT (STDMETHODCALLTYPE *GetStringAsync)(IHttpClient *This, void *uri, void **op);
    HRESULT (STDMETHODCALLTYPE *PostAsync)(IHttpClient *This, void *uri, IHttpContent *content, void **op);
    HRESULT (STDMETHODCALLTYPE *PutAsync)(IHttpClient *This, void *uri, IHttpContent *content, void **op);
    HRESULT (STDMETHODCALLTYPE *SendRequestAsync)(IHttpClient *This, IHttpRequestMessage *request, void **op);
    HRESULT (STDMETHODCALLTYPE *SendRequestAsyncCompletionOption)(IHttpClient *This, IHttpRequestMessage *request, INT32 option, void **op);
    HRESULT (STDMETHODCALLTYPE *get_DefaultRequestHeaders)(IHttpClient *This, IHttpHeaderCollection **value);
} IHttpClientVtbl;

struct IHttpClient
{
    const IHttpClientVtbl *lpVtbl;
};

typedef struct IHttpClient2Vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IHttpClient2 *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IHttpClient2 *This);
    ULONG (STDMETHODCALLTYPE *Release)(IHttpClient2 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IHttpClient2 *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IHttpClient2 *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IHttpClient2 *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *TryDeleteAsync)(IHttpClient2 *This, void *uri, void **op);
    HRESULT (STDMETHODCALLTYPE *TryGetAsync)(IHttpClient2 *This, void *uri, void **op);
    HRESULT (STDMETHODCALLTYPE *TryGetAsyncCompletionOption)(IHttpClient2 *This, void *uri, INT32 option, void **op);
    HRESULT (STDMETHODCALLTYPE *TryGetBufferAsync)(IHttpClient2 *This, void *uri, void **op);
    HRESULT (STDMETHODCALLTYPE *TryGetInputStreamAsync)(IHttpClient2 *This, void *uri, void **op);
    HRESULT (STDMETHODCALLTYPE *TryGetStringAsync)(IHttpClient2 *This, void *uri, void **op);
    HRESULT (STDMETHODCALLTYPE *TryPostAsync)(IHttpClient2 *This, void *uri, IHttpContent *content, void **op);
    HRESULT (STDMETHODCALLTYPE *TryPutAsync)(IHttpClient2 *This, void *uri, IHttpContent *content, void **op);
    HRESULT (STDMETHODCALLTYPE *TrySendRequestAsync)(IHttpClient2 *This, IHttpRequestMessage *request, void **op);
    HRESULT (STDMETHODCALLTYPE *TrySendRequestAsyncCompletionOption)(IHttpClient2 *This, IHttpRequestMessage *request, INT32 option, void **op);
} IHttpClient2Vtbl;

struct IHttpClient2
{
    const IHttpClient2Vtbl *lpVtbl;
};

typedef struct IHttpClientFactory IHttpClientFactory;

typedef struct IHttpClientFactoryVtbl
{
    IActivationFactoryVtbl BasedVtbl;
    HRESULT (STDMETHODCALLTYPE *Create)(IHttpClientFactory *This, void *filter, IHttpClient **value);
} IHttpClientFactoryVtbl;

struct IHttpClientFactory
{
    const IHttpClientFactoryVtbl *lpVtbl;
};

typedef struct IHttpRequestMessageFactory IHttpRequestMessageFactory;

typedef struct IHttpRequestMessageFactoryVtbl
{
    IActivationFactoryVtbl BasedVtbl;
    HRESULT (STDMETHODCALLTYPE *Create)(IHttpRequestMessageFactory *This, IHttpMethod *method, void *uri, IHttpRequestMessage **value);
} IHttpRequestMessageFactoryVtbl;

struct IHttpRequestMessageFactory
{
    const IHttpRequestMessageFactoryVtbl *lpVtbl;
};

typedef struct IHttpResponseMessageFactory IHttpResponseMessageFactory;

typedef struct IHttpResponseMessageFactoryVtbl
{
    IActivationFactoryVtbl BasedVtbl;
    HRESULT (STDMETHODCALLTYPE *Create)(IHttpResponseMessageFactory *This, INT32 status_code, IHttpResponseMessage **value);
} IHttpResponseMessageFactoryVtbl;

struct IHttpResponseMessageFactory
{
    const IHttpResponseMessageFactoryVtbl *lpVtbl;
};

typedef struct IHttpStringContentFactory IHttpStringContentFactory;

typedef struct IHttpStringContentFactoryVtbl
{
    IActivationFactoryVtbl BasedVtbl;
    HRESULT (STDMETHODCALLTYPE *CreateFromString)(IHttpStringContentFactory *This, HSTRING content, IHttpContent **value);
    HRESULT (STDMETHODCALLTYPE *CreateFromStringWithEncoding)(IHttpStringContentFactory *This, HSTRING content, INT32 encoding, IHttpContent **value);
    HRESULT (STDMETHODCALLTYPE *CreateFromStringWithEncodingAndMediaType)(IHttpStringContentFactory *This, HSTRING content, INT32 encoding, HSTRING media_type, IHttpContent **value);
} IHttpStringContentFactoryVtbl;

struct IHttpStringContentFactory
{
    const IHttpStringContentFactoryVtbl *lpVtbl;
};

typedef struct IHttpMethodFactory IHttpMethodFactory;

typedef struct IHttpMethodFactoryVtbl
{
    IActivationFactoryVtbl BasedVtbl;
    HRESULT (STDMETHODCALLTYPE *Create)(IHttpMethodFactory *This, HSTRING method, IHttpMethod **value);
    HRESULT (STDMETHODCALLTYPE *get_Delete)(IHttpMethodFactory *This, IHttpMethod **value);
    HRESULT (STDMETHODCALLTYPE *get_Get)(IHttpMethodFactory *This, IHttpMethod **value);
    HRESULT (STDMETHODCALLTYPE *get_Head)(IHttpMethodFactory *This, IHttpMethod **value);
    HRESULT (STDMETHODCALLTYPE *get_Options)(IHttpMethodFactory *This, IHttpMethod **value);
    HRESULT (STDMETHODCALLTYPE *get_Patch)(IHttpMethodFactory *This, IHttpMethod **value);
    HRESULT (STDMETHODCALLTYPE *get_Post)(IHttpMethodFactory *This, IHttpMethod **value);
    HRESULT (STDMETHODCALLTYPE *get_Put)(IHttpMethodFactory *This, IHttpMethod **value);
} IHttpMethodFactoryVtbl;

struct IHttpMethodFactory
{
    const IHttpMethodFactoryVtbl *lpVtbl;
};

typedef struct IWebClosableVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWebClosable *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWebClosable *This);
    ULONG (STDMETHODCALLTYPE *Release)(IWebClosable *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IWebClosable *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IWebClosable *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IWebClosable *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *Close)(IWebClosable *This);
} IWebClosableVtbl;

struct IWebClosable
{
    const IWebClosableVtbl *lpVtbl;
};

typedef struct IWebStringableVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWebStringable *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWebStringable *This);
    ULONG (STDMETHODCALLTYPE *Release)(IWebStringable *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IWebStringable *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IWebStringable *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IWebStringable *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *ToString)(IWebStringable *This, HSTRING *value);
} IWebStringableVtbl;

struct IWebStringable
{
    const IWebStringableVtbl *lpVtbl;
};

/* IAsyncOperationWithProgress<HttpResponseMessage, HttpProgress> */
typedef struct IAsyncOpHttpVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IAsyncOpHttp *This, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IAsyncOpHttp *This);
    ULONG (STDMETHODCALLTYPE *Release)(IAsyncOpHttp *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IAsyncOpHttp *This, ULONG *count, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IAsyncOpHttp *This, HSTRING *name);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IAsyncOpHttp *This, TrustLevel *level);
    HRESULT (STDMETHODCALLTYPE *put_Progress)(IAsyncOpHttp *This, void *handler);
    HRESULT (STDMETHODCALLTYPE *get_Progress)(IAsyncOpHttp *This, void **handler);
    HRESULT (STDMETHODCALLTYPE *put_Completed)(IAsyncOpHttp *This, void *handler);
    HRESULT (STDMETHODCALLTYPE *get_Completed)(IAsyncOpHttp *This, void **handler);
    HRESULT (STDMETHODCALLTYPE *GetResults)(IAsyncOpHttp *This, void *results);
} IAsyncOpHttpVtbl;
/* COBJMACROS-style call macros for the locally declared interfaces. */

#define IHttpContent_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IHttpContent_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IHttpContent_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IHttpContent_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IHttpContent_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IHttpContent_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IHttpContent_BufferAllAsync(This,...) (This)->lpVtbl->BufferAllAsync(This, ##__VA_ARGS__)
#define IHttpContent_ReadAsBufferAsync(This,...) (This)->lpVtbl->ReadAsBufferAsync(This, ##__VA_ARGS__)
#define IHttpContent_ReadAsInputStreamAsync(This,...) (This)->lpVtbl->ReadAsInputStreamAsync(This, ##__VA_ARGS__)
#define IHttpContent_ReadAsStringAsync(This,...) (This)->lpVtbl->ReadAsStringAsync(This, ##__VA_ARGS__)
#define IHttpContent_TryComputeLength(This,...) (This)->lpVtbl->TryComputeLength(This, ##__VA_ARGS__)
#define IHttpContent_WriteToStreamAsync(This,...) (This)->lpVtbl->WriteToStreamAsync(This, ##__VA_ARGS__)
#define IHttpContent_get_Headers(This,...) (This)->lpVtbl->get_Headers(This, ##__VA_ARGS__)
#define IHttpHeaderCollection_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IHttpHeaderCollection_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IHttpHeaderCollection_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IHttpHeaderCollection_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IHttpHeaderCollection_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IHttpHeaderCollection_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IHttpHeaderCollection_Append(This,...) (This)->lpVtbl->Append(This, ##__VA_ARGS__)
#define IHttpHeaderCollection_TryAppendWithoutValidation(This,...) (This)->lpVtbl->TryAppendWithoutValidation(This, ##__VA_ARGS__)
#define IHttpMethod_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IHttpMethod_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IHttpMethod_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IHttpMethod_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IHttpMethod_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IHttpMethod_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IHttpMethod_get_Method(This,...) (This)->lpVtbl->get_Method(This, ##__VA_ARGS__)
#define IHttpRequestMessage_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IHttpRequestMessage_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IHttpRequestMessage_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IHttpRequestMessage_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IHttpRequestMessage_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IHttpRequestMessage_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IHttpRequestMessage_get_Content(This,...) (This)->lpVtbl->get_Content(This, ##__VA_ARGS__)
#define IHttpRequestMessage_put_Content(This,...) (This)->lpVtbl->put_Content(This, ##__VA_ARGS__)
#define IHttpRequestMessage_get_Headers(This,...) (This)->lpVtbl->get_Headers(This, ##__VA_ARGS__)
#define IHttpRequestMessage_get_Method(This,...) (This)->lpVtbl->get_Method(This, ##__VA_ARGS__)
#define IHttpRequestMessage_put_Method(This,...) (This)->lpVtbl->put_Method(This, ##__VA_ARGS__)
#define IHttpRequestMessage_get_Properties(This,...) (This)->lpVtbl->get_Properties(This, ##__VA_ARGS__)
#define IHttpRequestMessage_get_RequestUri(This,...) (This)->lpVtbl->get_RequestUri(This, ##__VA_ARGS__)
#define IHttpRequestMessage_put_RequestUri(This,...) (This)->lpVtbl->put_RequestUri(This, ##__VA_ARGS__)
#define IHttpRequestMessage_get_TransportInformation(This,...) (This)->lpVtbl->get_TransportInformation(This, ##__VA_ARGS__)
#define IHttpResponseMessage_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IHttpResponseMessage_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IHttpResponseMessage_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IHttpResponseMessage_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IHttpResponseMessage_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IHttpResponseMessage_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IHttpResponseMessage_EnsureSuccessStatusCode(This,...) (This)->lpVtbl->EnsureSuccessStatusCode(This, ##__VA_ARGS__)
#define IHttpResponseMessage_get_Content(This,...) (This)->lpVtbl->get_Content(This, ##__VA_ARGS__)
#define IHttpResponseMessage_put_Content(This,...) (This)->lpVtbl->put_Content(This, ##__VA_ARGS__)
#define IHttpResponseMessage_get_Headers(This,...) (This)->lpVtbl->get_Headers(This, ##__VA_ARGS__)
#define IHttpResponseMessage_get_IsSuccessStatusCode(This,...) (This)->lpVtbl->get_IsSuccessStatusCode(This, ##__VA_ARGS__)
#define IHttpResponseMessage_get_ReasonPhrase(This,...) (This)->lpVtbl->get_ReasonPhrase(This, ##__VA_ARGS__)
#define IHttpResponseMessage_put_ReasonPhrase(This,...) (This)->lpVtbl->put_ReasonPhrase(This, ##__VA_ARGS__)
#define IHttpResponseMessage_get_RequestMessage(This,...) (This)->lpVtbl->get_RequestMessage(This, ##__VA_ARGS__)
#define IHttpResponseMessage_put_RequestMessage(This,...) (This)->lpVtbl->put_RequestMessage(This, ##__VA_ARGS__)
#define IHttpResponseMessage_get_Source(This,...) (This)->lpVtbl->get_Source(This, ##__VA_ARGS__)
#define IHttpResponseMessage_put_Source(This,...) (This)->lpVtbl->put_Source(This, ##__VA_ARGS__)
#define IHttpResponseMessage_get_StatusCode(This,...) (This)->lpVtbl->get_StatusCode(This, ##__VA_ARGS__)
#define IHttpResponseMessage_put_StatusCode(This,...) (This)->lpVtbl->put_StatusCode(This, ##__VA_ARGS__)
#define IHttpResponseMessage_get_Version(This,...) (This)->lpVtbl->get_Version(This, ##__VA_ARGS__)
#define IHttpResponseMessage_put_Version(This,...) (This)->lpVtbl->put_Version(This, ##__VA_ARGS__)
#define IHttpClient_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IHttpClient_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IHttpClient_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IHttpClient_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IHttpClient_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IHttpClient_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IHttpClient_DeleteAsync(This,...) (This)->lpVtbl->DeleteAsync(This, ##__VA_ARGS__)
#define IHttpClient_GetAsync(This,...) (This)->lpVtbl->GetAsync(This, ##__VA_ARGS__)
#define IHttpClient_GetAsyncCompletionOption(This,...) (This)->lpVtbl->GetAsyncCompletionOption(This, ##__VA_ARGS__)
#define IHttpClient_GetBufferAsync(This,...) (This)->lpVtbl->GetBufferAsync(This, ##__VA_ARGS__)
#define IHttpClient_GetInputStreamAsync(This,...) (This)->lpVtbl->GetInputStreamAsync(This, ##__VA_ARGS__)
#define IHttpClient_GetStringAsync(This,...) (This)->lpVtbl->GetStringAsync(This, ##__VA_ARGS__)
#define IHttpClient_PostAsync(This,...) (This)->lpVtbl->PostAsync(This, ##__VA_ARGS__)
#define IHttpClient_PutAsync(This,...) (This)->lpVtbl->PutAsync(This, ##__VA_ARGS__)
#define IHttpClient_SendRequestAsync(This,...) (This)->lpVtbl->SendRequestAsync(This, ##__VA_ARGS__)
#define IHttpClient_SendRequestAsyncCompletionOption(This,...) (This)->lpVtbl->SendRequestAsyncCompletionOption(This, ##__VA_ARGS__)
#define IHttpClient_get_DefaultRequestHeaders(This,...) (This)->lpVtbl->get_DefaultRequestHeaders(This, ##__VA_ARGS__)
#define IHttpClient2_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IHttpClient2_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IHttpClient2_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IHttpClient2_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IHttpClient2_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IHttpClient2_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IHttpClient2_TryDeleteAsync(This,...) (This)->lpVtbl->TryDeleteAsync(This, ##__VA_ARGS__)
#define IHttpClient2_TryGetAsync(This,...) (This)->lpVtbl->TryGetAsync(This, ##__VA_ARGS__)
#define IHttpClient2_TryGetAsyncCompletionOption(This,...) (This)->lpVtbl->TryGetAsyncCompletionOption(This, ##__VA_ARGS__)
#define IHttpClient2_TryGetBufferAsync(This,...) (This)->lpVtbl->TryGetBufferAsync(This, ##__VA_ARGS__)
#define IHttpClient2_TryGetInputStreamAsync(This,...) (This)->lpVtbl->TryGetInputStreamAsync(This, ##__VA_ARGS__)
#define IHttpClient2_TryGetStringAsync(This,...) (This)->lpVtbl->TryGetStringAsync(This, ##__VA_ARGS__)
#define IHttpClient2_TryPostAsync(This,...) (This)->lpVtbl->TryPostAsync(This, ##__VA_ARGS__)
#define IHttpClient2_TryPutAsync(This,...) (This)->lpVtbl->TryPutAsync(This, ##__VA_ARGS__)
#define IHttpClient2_TrySendRequestAsync(This,...) (This)->lpVtbl->TrySendRequestAsync(This, ##__VA_ARGS__)
#define IHttpClient2_TrySendRequestAsyncCompletionOption(This,...) (This)->lpVtbl->TrySendRequestAsyncCompletionOption(This, ##__VA_ARGS__)
#define IHttpClientFactory_Create(This,...) (This)->lpVtbl->Create(This, ##__VA_ARGS__)
#define IHttpRequestMessageFactory_Create(This,...) (This)->lpVtbl->Create(This, ##__VA_ARGS__)
#define IHttpResponseMessageFactory_Create(This,...) (This)->lpVtbl->Create(This, ##__VA_ARGS__)
#define IHttpStringContentFactory_CreateFromString(This,...) (This)->lpVtbl->CreateFromString(This, ##__VA_ARGS__)
#define IHttpStringContentFactory_CreateFromStringWithEncoding(This,...) (This)->lpVtbl->CreateFromStringWithEncoding(This, ##__VA_ARGS__)
#define IHttpStringContentFactory_CreateFromStringWithEncodingAndMediaType(This,...) (This)->lpVtbl->CreateFromStringWithEncodingAndMediaType(This, ##__VA_ARGS__)
#define IHttpMethodFactory_Create(This,...) (This)->lpVtbl->Create(This, ##__VA_ARGS__)
#define IHttpMethodFactory_get_Delete(This,...) (This)->lpVtbl->get_Delete(This, ##__VA_ARGS__)
#define IHttpMethodFactory_get_Get(This,...) (This)->lpVtbl->get_Get(This, ##__VA_ARGS__)
#define IHttpMethodFactory_get_Head(This,...) (This)->lpVtbl->get_Head(This, ##__VA_ARGS__)
#define IHttpMethodFactory_get_Options(This,...) (This)->lpVtbl->get_Options(This, ##__VA_ARGS__)
#define IHttpMethodFactory_get_Patch(This,...) (This)->lpVtbl->get_Patch(This, ##__VA_ARGS__)
#define IHttpMethodFactory_get_Post(This,...) (This)->lpVtbl->get_Post(This, ##__VA_ARGS__)
#define IHttpMethodFactory_get_Put(This,...) (This)->lpVtbl->get_Put(This, ##__VA_ARGS__)
#define IWebClosable_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IWebClosable_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IWebClosable_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IWebClosable_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IWebClosable_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IWebClosable_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IWebClosable_Close(This,...) (This)->lpVtbl->Close(This, ##__VA_ARGS__)
#define IWebStringable_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IWebStringable_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IWebStringable_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IWebStringable_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IWebStringable_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IWebStringable_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IWebStringable_ToString(This,...) (This)->lpVtbl->ToString(This, ##__VA_ARGS__)
#define IAsyncOpHttp_QueryInterface(This,...) (This)->lpVtbl->QueryInterface(This, ##__VA_ARGS__)
#define IAsyncOpHttp_AddRef(This,...) (This)->lpVtbl->AddRef(This, ##__VA_ARGS__)
#define IAsyncOpHttp_Release(This,...) (This)->lpVtbl->Release(This, ##__VA_ARGS__)
#define IAsyncOpHttp_GetIids(This,...) (This)->lpVtbl->GetIids(This, ##__VA_ARGS__)
#define IAsyncOpHttp_GetRuntimeClassName(This,...) (This)->lpVtbl->GetRuntimeClassName(This, ##__VA_ARGS__)
#define IAsyncOpHttp_GetTrustLevel(This,...) (This)->lpVtbl->GetTrustLevel(This, ##__VA_ARGS__)
#define IAsyncOpHttp_put_Progress(This,...) (This)->lpVtbl->put_Progress(This, ##__VA_ARGS__)
#define IAsyncOpHttp_get_Progress(This,...) (This)->lpVtbl->get_Progress(This, ##__VA_ARGS__)
#define IAsyncOpHttp_put_Completed(This,...) (This)->lpVtbl->put_Completed(This, ##__VA_ARGS__)
#define IAsyncOpHttp_get_Completed(This,...) (This)->lpVtbl->get_Completed(This, ##__VA_ARGS__)
#define IAsyncOpHttp_GetResults(This,...) (This)->lpVtbl->GetResults(This, ##__VA_ARGS__)


struct IAsyncOpHttp
{
    const IAsyncOpHttpVtbl *lpVtbl;
};

struct async_completed_handler
{
    const struct async_completed_handler_vtbl *lpVtbl;
    BOOL invoked;
    void *async;
    int status;
};

static HRESULT WINAPI async_completed_handler_QueryInterface( struct async_completed_handler *This, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ))
    {
        *out = This;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI async_completed_handler_AddRef( struct async_completed_handler *This ) { return 2; }
static ULONG WINAPI async_completed_handler_Release( struct async_completed_handler *This ) { return 1; }
static HRESULT WINAPI async_completed_handler_Invoke( struct async_completed_handler *This, void *async, int status )
{
    This->invoked = TRUE;
    This->async = async;
    This->status = status;
    return S_OK;
}

static const struct async_completed_handler_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( struct async_completed_handler *This, REFIID iid, void **out );
    ULONG (STDMETHODCALLTYPE *AddRef)( struct async_completed_handler *This );
    ULONG (STDMETHODCALLTYPE *Release)( struct async_completed_handler *This );
    HRESULT (STDMETHODCALLTYPE *Invoke)( struct async_completed_handler *This, void *async, int status );
} async_completed_handler_vtbl =
{
    async_completed_handler_QueryInterface,
    async_completed_handler_AddRef,
    async_completed_handler_Release,
    async_completed_handler_Invoke,
};

static void check_http_string( unsigned int line, HSTRING str, const WCHAR *expected )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( str, NULL );
    ok_(__FILE__, line)( !wcscmp( buffer, expected ), "got string %s, expected %s.\n", debugstr_w(buffer), debugstr_w(expected) );
}

#define check_string( str, expected ) check_http_string( __LINE__, str, expected )

static void check_runtimeclass_str( unsigned int line, IInspectable *inspectable, const WCHAR *class_name )
{
    HSTRING str;
    HRESULT hr;

    hr = IInspectable_GetRuntimeClassName( inspectable, &str );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    check_http_string( __LINE__, str, class_name );
    WindowsDeleteString( str );
}

static void check_nointerface( void *obj, const GUID *iid )
{
    IInspectable *iface = obj;
    IInspectable *inspectable;
    HRESULT hr;

    inspectable = (void *)0xdeadbeef;
    hr = IInspectable_QueryInterface( iface, iid, (void **)&inspectable );
    ok( hr == E_NOINTERFACE, "got hr %#lx.\n", hr );
    ok( inspectable == NULL, "got interface %p.\n", inspectable );
}

static void get_factory( const WCHAR *class_name, IActivationFactory **factory )
{
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr != S_OK) *factory = NULL;
}

static void test_HttpClientFactory(void)
{
    static const struct
    {
        const WCHAR *class_name;
        const GUID *factory_iid;
    }
    classes[] =
    {
        { L"Windows.Web.Http.HttpClient", &IID_IHttpClientFactory },
        { L"Windows.Web.Http.HttpRequestMessage", &IID_IHttpRequestMessageFactory },
        { L"Windows.Web.Http.HttpResponseMessage", &IID_IHttpResponseMessageFactory },
        { L"Windows.Web.Http.HttpStringContent", &IID_IHttpStringContentFactory },
        { L"Windows.Web.Http.HttpMethod", &IID_IHttpMethodFactory },
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE( classes ); i++)
    {
        IInspectable *inspectable = (void *)0xdeadbeef;
        IActivationFactory *factory;
        HSTRING str;
        HRESULT hr;
        LONG ref;

        get_factory( classes[i].class_name, &factory );
        if (!factory)
        {
            win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( classes[i].class_name ) );
            continue;
        }

        check_interface( factory, &IID_IUnknown );
        check_interface( factory, &IID_IInspectable );
        check_interface( factory, &IID_IAgileObject );
        check_interface( factory, &IID_IActivationFactory );

        /* The activation factory must not expose instance interfaces. */
        check_nointerface( factory, &IID_IHttpClient );

        hr = IActivationFactory_QueryInterface( factory, classes[i].factory_iid, (void **)&inspectable );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (SUCCEEDED(hr)) IInspectable_Release( inspectable );

        hr = WindowsCreateString( classes[i].class_name, wcslen( classes[i].class_name ), &str );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = RoActivateInstance( str, &inspectable );
        WindowsDeleteString( str );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (hr == S_OK)
        {
            check_runtimeclass_str( __LINE__, inspectable, classes[i].class_name );
            check_interface( inspectable, &IID_IAgileObject );
            IInspectable_Release( inspectable );
        }

        ref = IActivationFactory_Release( factory );
        ok( ref == 1, "got ref %ld.\n", ref );
    }
}

static void test_HttpRequestMessage(void)
{
    IHttpRequestMessageFactory *req_factory = (void *)0xdeadbeef;
    IHttpStringContentFactory *str_factory = (void *)0xdeadbeef;
    IHttpHeaderCollection *headers = (void *)0xdeadbeef;
    IHttpRequestMessage *request = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IHttpContent *content = (void *)0xdeadbeef;
    IWebStringable *stringable = (void *)0xdeadbeef;
    IHttpMethod *method = (void *)0xdeadbeef;
    IWebClosable *closable = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    boolean succeeded;
    UINT64 length;
    HSTRING str, str2;
    HRESULT hr;
    LONG ref;

    get_factory( L"Windows.Web.Http.HttpRequestMessage", &factory );
    if (!factory)
    {
        win_skip( "Windows.Web.Http.HttpRequestMessage runtimeclass not registered, skipping tests.\n" );
        return;
    }
    hr = IActivationFactory_QueryInterface( factory, &IID_IHttpRequestMessageFactory, (void **)&req_factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IActivationFactory_Release( factory );

    get_factory( L"Windows.Web.Http.HttpStringContent", &factory );
    hr = IActivationFactory_QueryInterface( factory, &IID_IHttpStringContentFactory, (void **)&str_factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IActivationFactory_Release( factory );

    /* RoActivateInstance default-constructs the request */
    hr = WindowsCreateString( L"Windows.Web.Http.HttpRequestMessage", 35, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( str, &inspectable );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_runtimeclass_str( __LINE__, inspectable, L"Windows.Web.Http.HttpRequestMessage" );
    check_interface( inspectable, &IID_IHttpRequestMessage );
    check_interface( inspectable, &IID_web_IClosable );
    check_nointerface( inspectable, &IID_IHttpResponseMessage );

    hr = IInspectable_QueryInterface( inspectable, &IID_IHttpRequestMessage, (void **)&request );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IInspectable_Release( inspectable );

    /* Method defaults to GET */
    hr = IHttpRequestMessage_get_Method( request, &method );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( method, &IID_IHttpMethod );
    check_interface( method, &IID_web_IStringable );
    hr = IHttpMethod_get_Method( method, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_string( str, L"GET" );
    WindowsDeleteString( str );
    IHttpMethod_Release( method );

    /* Content roundtrip */
    hr = IHttpRequestMessage_get_Content( request, &content );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( content == NULL, "got content %p.\n", content );

    hr = WindowsCreateString( L"Hello Wine", 10, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpStringContentFactory_CreateFromString( str_factory, str, &content );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( content, &IID_IHttpContent );
    check_interface( content, &IID_web_IClosable );
    check_interface( content, &IID_web_IStringable );
    check_runtimeclass_str( __LINE__, (IInspectable *)content, L"Windows.Web.Http.HttpStringContent" );

    hr = IHttpRequestMessage_put_Content( request, content );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    content = NULL;
    hr = IHttpRequestMessage_get_Content( request, &content );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( content != NULL, "got content %p.\n", content );
    IHttpContent_Release( content ); /* get ref */
    IHttpContent_Release( content ); /* create ref, request holds its own */

    /* Content string manipulation */
    hr = WindowsCreateString( L"Test", 4, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpStringContentFactory_CreateFromString( str_factory, str, &content );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpContent_QueryInterface( content, &IID_web_IStringable, (void **)&stringable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IWebStringable_ToString( stringable, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_string( str, L"Test" );
    WindowsDeleteString( str );
    IWebStringable_Release( stringable );

    length = 0;
    succeeded = FALSE;
    hr = IHttpContent_TryComputeLength( content, &length, &succeeded );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( succeeded, "compute length failed.\n" );
    ok( length == 4, "got length %s.\n", wine_dbgstr_longlong( length ) ); /* UTF-8 encoding, as on Windows */

    /* ReadAsStringAsync returns the content string through the async operation */
    hr = IHttpContent_ReadAsStringAsync( content, (void **)&inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr) && inspectable)
    {
        IAsyncOpHttp *op = (IAsyncOpHttp *)inspectable;
        HSTRING result = NULL;

        hr = IAsyncOpHttp_GetResults( op, (void **)&result );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (SUCCEEDED(hr))
        {
            check_string( result, L"Test" );
            WindowsDeleteString( result );
        }
        ref = IAsyncOpHttp_Release( op );
        ok( ref == 0, "got ref %ld.\n", ref );
    }

    /* Content headers */
    hr = IHttpContent_get_Headers( content, &headers );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( headers, &IID_IHttpContentHeaderCollection );
    hr = WindowsCreateString( L"Content-Type", 12, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( L"text/plain", 10, &str2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpHeaderCollection_Append( headers, str, str2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    succeeded = FALSE;
    hr = IHttpHeaderCollection_TryAppendWithoutValidation( headers, str, str2, &succeeded );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( succeeded, "TryAppendWithoutValidation failed.\n" );
    WindowsDeleteString( str );
    WindowsDeleteString( str2 );
    IHttpHeaderCollection_Release( headers );

    hr = IHttpContent_QueryInterface( content, &IID_web_IClosable, (void **)&closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IWebClosable_Close( closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IWebClosable_Release( closable );
    IHttpContent_Release( content );

    /* Request headers */
    hr = IHttpRequestMessage_get_Headers( request, &headers );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( headers, &IID_IHttpRequestHeaderCollection );
    check_nointerface( headers, &IID_IHttpResponseHeaderCollection );
    IHttpHeaderCollection_Release( headers );

    /* Properties and Uri accessors are reachable */
    hr = IHttpRequestMessage_get_Properties( request, (void **)&inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (inspectable) IInspectable_Release( inspectable );

    inspectable = (void *)0xdeadbeef;
    hr = IHttpRequestMessage_get_RequestUri( request, (void **)&inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( inspectable == NULL, "got uri %p.\n", inspectable );
    hr = IHttpRequestMessage_put_RequestUri( request, NULL );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* put_Method with a custom verb */
    get_factory( L"Windows.Web.Http.HttpMethod", &factory );
    hr = IActivationFactory_QueryInterface( factory, &IID_IHttpMethodFactory, (void **)&inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( L"CUSTOM", 6, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpMethodFactory_Create( (IHttpMethodFactory *)inspectable, str, &method );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IInspectable_Release( inspectable );
    IActivationFactory_Release( factory );

    hr = IHttpRequestMessage_put_Method( request, method );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpRequestMessage_get_Method( request, &method );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpMethod_get_Method( method, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_string( str, L"CUSTOM" );
    WindowsDeleteString( str );
    IHttpMethod_Release( method ); /* get ref */
    IHttpMethod_Release( method ); /* create ref, request holds its own */

    ref = IHttpRequestMessage_Release( request );
    ok( ref == 0, "got ref %ld.\n", ref );

    /* Factory Create path */
    hr = IHttpRequestMessageFactory_Create( req_factory, NULL, NULL, &request );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        check_runtimeclass_str( __LINE__, (IInspectable *)request, L"Windows.Web.Http.HttpRequestMessage" );
        ref = IHttpRequestMessage_Release( request );
        ok( ref == 0, "got ref %ld.\n", ref );
    }

    ref = IActivationFactory_Release( (IActivationFactory *)req_factory );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( (IActivationFactory *)str_factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_HttpResponseMessage(void)
{
    IHttpResponseMessageFactory *resp_factory = (void *)0xdeadbeef;
    IHttpHeaderCollection *headers = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IHttpContent *content = (void *)0xdeadbeef;
    IWebClosable *closable = (void *)0xdeadbeef;
    IHttpResponseMessage *self = (void *)0xdeadbeef;
    IHttpResponseMessage *resp = (void *)0xdeadbeef;
    IHttpRequestMessage *request = (void *)0xdeadbeef;
    boolean success;
    INT32 value;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    get_factory( L"Windows.Web.Http.HttpResponseMessage", &factory );
    if (!factory)
    {
        win_skip( "Windows.Web.Http.HttpResponseMessage runtimeclass not registered, skipping tests.\n" );
        return;
    }
    hr = IActivationFactory_QueryInterface( factory, &IID_IHttpResponseMessageFactory, (void **)&resp_factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IHttpResponseMessageFactory_Create( resp_factory, 404 /* NotFound */, &resp );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_runtimeclass_str( __LINE__, (IInspectable *)resp, L"Windows.Web.Http.HttpResponseMessage" );
    check_interface( resp, &IID_IHttpResponseMessage );
    check_interface( resp, &IID_web_IClosable );

    value = 0;
    hr = IHttpResponseMessage_get_StatusCode( resp, &value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( value == 404, "got status code %d.\n", value );

    success = TRUE;
    hr = IHttpResponseMessage_get_IsSuccessStatusCode( resp, &success );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !success, "got success %u.\n", success );

    self = (void *)0xdeadbeef;
    hr = IHttpResponseMessage_EnsureSuccessStatusCode( resp, &self );
    ok( hr == E_FAIL, "got hr %#lx.\n", hr );

    hr = IHttpResponseMessage_put_StatusCode( resp, 200 /* Ok */ );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    success = FALSE;
    hr = IHttpResponseMessage_get_IsSuccessStatusCode( resp, &success );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( success, "got success %u.\n", success );
    self = NULL;
    hr = IHttpResponseMessage_EnsureSuccessStatusCode( resp, &self );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( self == resp, "got response %p.\n", self );
    if (SUCCEEDED(hr) && self) IHttpResponseMessage_Release( self );

    hr = IHttpResponseMessage_get_ReasonPhrase( resp, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_string( str, L"OK" );
    WindowsDeleteString( str );
    hr = WindowsCreateString( L"Custom Reason", 13, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpResponseMessage_put_ReasonPhrase( resp, str );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpResponseMessage_get_ReasonPhrase( resp, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_string( str, L"Custom Reason" );
    WindowsDeleteString( str );

    hr = IHttpResponseMessage_get_Content( resp, &content );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( content != NULL, "got content %p.\n", content );
    check_interface( content, &IID_IHttpContent );
    IHttpContent_Release( content );

    hr = IHttpResponseMessage_get_Headers( resp, &headers );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( headers, &IID_IHttpResponseHeaderCollection );
    check_nointerface( headers, &IID_IHttpRequestHeaderCollection );
    IHttpHeaderCollection_Release( headers );

    value = 0xdeadbeef;
    hr = IHttpResponseMessage_get_Source( resp, &value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( value == 2 /* HttpResponseMessageSource Network */, "got source %d.\n", value );
    value = 0xdeadbeef;
    hr = IHttpResponseMessage_get_Version( resp, &value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( value == 1 /* Http11 */, "got version %d.\n", value );

    request = (void *)0xdeadbeef;
    hr = IHttpResponseMessage_get_RequestMessage( resp, &request );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( request == NULL, "got request %p.\n", request );

    hr = IHttpResponseMessage_QueryInterface( resp, &IID_web_IClosable, (void **)&closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IWebClosable_Close( closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IWebClosable_Release( closable );
    ok( ref == 1, "got ref %ld.\n", ref );

    ref = IHttpResponseMessage_Release( resp );
    ok( ref == 0, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( (IActivationFactory *)resp_factory );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_HttpClient(void)
{
    static const WCHAR *async_op_class =
        L"Windows.Foundation.IAsyncOperationWithProgress`2<Windows.Web.Http.HttpResponseMessage, Windows.Web.Http.HttpProgress>";
    IHttpRequestMessageFactory *req_factory = (void *)0xdeadbeef;
    IHttpStringContentFactory *str_factory = (void *)0xdeadbeef;
    IHttpHeaderCollection *headers = (void *)0xdeadbeef;
    IActivationFactory *str_f = (void *)0xdeadbeef;
    IActivationFactory *req_f = (void *)0xdeadbeef;
    struct async_completed_handler handler;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IHttpResponseMessage *resp = (void *)0xdeadbeef;
    IHttpClient2 *client2 = (void *)0xdeadbeef;
    IHttpRequestMessage *request = (void *)0xdeadbeef;
    IAsyncOpHttp *op = (void *)0xdeadbeef;
    IHttpContent *content = (void *)0xdeadbeef;
    IWebClosable *closable = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    IHttpClient *client = (void *)0xdeadbeef;
    IAsyncInfo *info = (void *)0xdeadbeef;
    AsyncStatus status;
    HSTRING str;
    HRESULT hr, error;
    INT32 code;
    UINT32 id;
    LONG ref;

    get_factory( L"Windows.Web.Http.HttpClient", &factory );
    if (!factory)
    {
        win_skip( "Windows.Web.Http.HttpClient runtimeclass not registered, skipping tests.\n" );
        return;
    }
    get_factory( L"Windows.Web.Http.HttpRequestMessage", &req_f );
    hr = IActivationFactory_QueryInterface( req_f, &IID_IHttpRequestMessageFactory, (void **)&req_factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IActivationFactory_Release( req_f );
    get_factory( L"Windows.Web.Http.HttpStringContent", &str_f );
    hr = IActivationFactory_QueryInterface( str_f, &IID_IHttpStringContentFactory, (void **)&str_factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IActivationFactory_Release( str_f );

    /* Factory Create produces an IHttpClient */
    hr = IActivationFactory_QueryInterface( factory, &IID_IHttpClientFactory, (void **)&inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpClientFactory_Create( (IHttpClientFactory *)inspectable, NULL, &client );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IInspectable_Release( inspectable );

    check_runtimeclass_str( __LINE__, (IInspectable *)client, L"Windows.Web.Http.HttpClient" );
    check_interface( client, &IID_IHttpClient );
    check_interface( client, &IID_IHttpClient2 );
    check_interface( client, &IID_web_IClosable );
    check_nointerface( client, &IID_IReferenceTracker );

    hr = IHttpClient_QueryInterface( client, &IID_IHttpClient2, (void **)&client2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* Default request headers */
    hr = IHttpClient_get_DefaultRequestHeaders( client, &headers );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( headers, &IID_IHttpRequestHeaderCollection );
    hr = WindowsCreateString( L"User-Agent", 10, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpHeaderCollection_Append( headers, str, str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    IHttpHeaderCollection_Release( headers );

    /* GetAsync returns IAsyncOperationWithProgress<HttpResponseMessage, HttpProgress> */
    hr = IHttpClient_GetAsync( client, NULL, (void **)&op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( op, &IID_IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress );
    check_interface( op, &IID_IAsyncInfo );
    check_nointerface( op, &IID_IReferenceTracker );
    check_runtimeclass_str( __LINE__, (IInspectable *)op, async_op_class );

    hr = IAsyncOpHttp_QueryInterface( op, &IID_IAsyncInfo, (void **)&info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    status = (AsyncStatus)0xdeadbeef;
    hr = IAsyncInfo_get_Status( info, &status );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( status == Started || status == Completed, "got status %u.\n", status );

    id = 0;
    hr = IAsyncInfo_get_Id( info, &id );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( id != 0, "got id %u.\n", id );

    error = 0xdeadbeef;
    hr = IAsyncInfo_get_ErrorCode( info, &error );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( error == S_OK, "got error %#lx.\n", error );

    /* Completed handler is invoked synchronously for completed operations */
    handler.invoked = FALSE;
    handler.async = NULL;
    handler.status = -1;
    handler.lpVtbl = &async_completed_handler_vtbl;
    hr = IAsyncOpHttp_put_Completed( op, &handler );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( handler.invoked, "handler not invoked.\n" );
    ok( handler.async == op, "got async %p.\n", handler.async );
    ok( handler.status == Completed, "got status %d.\n", handler.status );
    inspectable = (void *)0xdeadbeef;
    hr = IAsyncOpHttp_get_Completed( op, (void **)&inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( inspectable == NULL, "got handler %p.\n", inspectable );

    /* GetResults returns the HttpResponseMessage */
    resp = (void *)0xdeadbeef;
    hr = IAsyncOpHttp_GetResults( op, &resp );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( resp != NULL, "got response %p.\n", resp );
    if (resp)
    {
        check_interface( resp, &IID_IHttpResponseMessage );
        code = 0;
        hr = IHttpResponseMessage_get_StatusCode( resp, &code );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( code == 200, "got status code %d.\n", code );
        IHttpResponseMessage_Release( resp );
    }

    hr = IAsyncInfo_Cancel( info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAsyncInfo_Close( info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IAsyncInfo_Release( info );
    ref = IAsyncOpHttp_Release( op );
    ok( ref == 0, "got ref %ld.\n", ref );

    /* PostAsync with string content */
    hr = WindowsCreateString( L"payload", 7, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpStringContentFactory_CreateFromString( str_factory, str, &content );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IHttpClient_PostAsync( client, NULL, content, (void **)&op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    resp = NULL;
    hr = IAsyncOpHttp_GetResults( op, &resp );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( resp != NULL, "got response %p.\n", resp );
    if (resp) IHttpResponseMessage_Release( resp );
    IAsyncOpHttp_Release( op );

    hr = IHttpClient_PutAsync( client, NULL, content, (void **)&op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IAsyncOpHttp_Release( op );
    hr = IHttpClient_DeleteAsync( client, NULL, (void **)&op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IAsyncOpHttp_Release( op );

    /* SendRequestAsync with an explicit request message */
    hr = IHttpRequestMessageFactory_Create( req_factory, NULL, NULL, &request );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpClient_SendRequestAsync( client, request, (void **)&op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    resp = NULL;
    hr = IAsyncOpHttp_GetResults( op, &resp );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (resp) IHttpResponseMessage_Release( resp );
    IAsyncOpHttp_Release( op );
    IHttpRequestMessage_Release( request );

    /* GetStringAsync returns an empty string operation */
    hr = IHttpClient_GetStringAsync( client, NULL, (void **)&op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    str = (void *)0xdeadbeef;
    hr = IAsyncOpHttp_GetResults( op, (void **)&str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        check_string( str, L"" );
        WindowsDeleteString( str );
    }
    IAsyncOpHttp_Release( op );

    hr = IHttpClient_GetBufferAsync( client, NULL, (void **)&op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IAsyncOpHttp_Release( op );
    hr = IHttpClient_GetInputStreamAsync( client, NULL, (void **)&op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IAsyncOpHttp_Release( op );

    /* IHttpClient2 Try* variants */
    hr = IHttpClient2_TryGetAsync( client2, NULL, (void **)&op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    resp = NULL;
    hr = IAsyncOpHttp_GetResults( op, &resp );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (resp) IHttpResponseMessage_Release( resp );
    IAsyncOpHttp_Release( op );

    hr = IHttpClient2_TrySendRequestAsync( client2, NULL, (void **)&op );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IAsyncOpHttp_Release( op );

    hr = IHttpClient_QueryInterface( client, &IID_web_IClosable, (void **)&closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IWebClosable_Close( closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IWebClosable_Release( closable );
    ok( ref == 2, "got ref %ld.\n", ref );

    IHttpContent_Release( content );
    ref = IHttpClient2_Release( client2 );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IHttpClient_Release( client );
    ok( ref == 0, "got ref %ld.\n", ref );

    ref = IActivationFactory_Release( (IActivationFactory *)req_factory );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( (IActivationFactory *)str_factory );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_HttpMethod(void)
{
    IHttpMethodFactory *method_factory = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IWebStringable *stringable = (void *)0xdeadbeef;
    IHttpMethod *method = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    get_factory( L"Windows.Web.Http.HttpMethod", &factory );
    if (!factory)
    {
        win_skip( "Windows.Web.Http.HttpMethod runtimeclass not registered, skipping tests.\n" );
        return;
    }
    hr = IActivationFactory_QueryInterface( factory, &IID_IHttpMethodStatics, (void **)&inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IInspectable_Release( inspectable );
    hr = IActivationFactory_QueryInterface( factory, &IID_IHttpMethodFactory, (void **)&method_factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_nointerface( factory, &IID_IHttpMethod );

    hr = IHttpMethodFactory_get_Get( method_factory, &method );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( method, &IID_IHttpMethod );
    check_interface( method, &IID_web_IStringable );
    check_runtimeclass_str( __LINE__, (IInspectable *)method, L"Windows.Web.Http.HttpMethod" );
    hr = IHttpMethod_get_Method( method, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_string( str, L"GET" );
    WindowsDeleteString( str );
    hr = IHttpMethod_QueryInterface( method, &IID_web_IStringable, (void **)&stringable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IWebStringable_ToString( stringable, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_string( str, L"GET" );
    WindowsDeleteString( str );
    IWebStringable_Release( stringable );
    ref = IHttpMethod_Release( method );
    ok( ref == 0, "got ref %ld.\n", ref );

    hr = IHttpMethodFactory_get_Post( method_factory, &method );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpMethod_get_Method( method, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_string( str, L"POST" );
    WindowsDeleteString( str );
    IHttpMethod_Release( method );

    hr = WindowsCreateString( L"CUSTOM", 6, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpMethodFactory_Create( method_factory, str, &method );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpMethod_get_Method( method, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_string( str, L"CUSTOM" );
    WindowsDeleteString( str );
    ref = IHttpMethod_Release( method );
    ok( ref == 0, "got ref %ld.\n", ref );

    ref = IActivationFactory_Release( (IActivationFactory *)method_factory );
    ok( ref == 1, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

START_TEST(web)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_JsonArrayStatics();
    test_JsonObjectStatics();
    test_JsonValueStatics();

    test_HttpClientFactory();
    test_HttpRequestMessage();
    test_HttpResponseMessage();
    test_HttpClient();
    test_HttpMethod();

    RoUninitialize();
}
