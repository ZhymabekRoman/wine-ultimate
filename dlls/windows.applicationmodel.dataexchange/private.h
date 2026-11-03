#ifndef __WINE_DATAEXCHANGE_PRIVATE_H
#define __WINE_DATAEXCHANGE_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "asyncinfo.h"
#include "eventtoken.h"
#include "roapi.h"

#include "wine/debug.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Security_EnterpriseData
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Storage_Streams
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_ApplicationModel_DataTransfer
#include "windows.foundation.h"
#include "windows.foundation.collections.h"
#include "windows.security.enterprisedata.h"
#include "windows.storage.h"
#include "windows.storage.streams.h"
#include "windows.ui.h"
#include "windows.applicationmodel.datatransfer.h"

/*
 * The DataPackage/Clipboard object family (data_package.c, data_package_view.c,
 * property_set.c, clipboard.c, standard_formats.c, share.c).  Every object is
 * created by one of these constructors; the factories in main.c hand them out.
 */
struct data_package;
struct data_package_view;
struct property_set;
struct winrt_map;
struct winrt_vector;

IInspectable *create_data_package( void );
IInspectable *create_data_package_view( struct data_package *package );
IInspectable *create_property_set( void );
IInspectable *create_property_set_view( struct property_set *set );
struct data_package_view *create_package_view_object( struct data_package *package );

/*
 * The DataPackage format table (data_package.c), read by the view mirror:
 * values live as an HSTRING (STRING), a COM pointer (COM) or a storage item
 * vector (ITEMS) under the canonical format key.  package_get_string returns
 * an owned duplicate (the empty string for a miss), package_get_com an owned
 * reference (NULL for a miss), package_get_items an owned vector (NULL for
 * a miss).
 */
enum fmt_kind { FMT_NONE, FMT_STRING, FMT_COM, FMT_ITEMS };

void *package_get_raw( struct data_package *pkg, const WCHAR *format, enum fmt_kind *kind );
HSTRING package_get_string( struct data_package *pkg, const WCHAR *format );
void *package_get_com( struct data_package *pkg, const WCHAR *format );
struct winrt_vector *package_get_items( struct data_package *pkg );
ULONG package_format_count( struct data_package *pkg );
const WCHAR *package_format_key( struct data_package *pkg, ULONG index );
DataPackageOperation package_get_operation( struct data_package *pkg );
struct property_set *package_get_props( struct data_package *pkg );

/*
 * Generic WinRT collection objects (collections.c):  an array-backed
 * IVector`1<T>/IVectorView`1<T>/IIterable`1<T> triple over HSTRING (STRING)
 * or COM-pointer (COM) elements, an IMap`2<HSTRING,V>/IMapView`2/IIterable`1
 * triple with the matching key/value-pair objects, and the IIterator`1
 * cursors.  The IIDs each object answers are per-object fields, so one vtable
 * set serves every instantiation.
 */
enum elem_kind { ELEM_STRING, ELEM_COM };

struct winrt_vector *create_vector( const IID *iid_vector, const IID *iid_view, const IID *iid_iterable,
                                    const IID *iid_iterator, enum elem_kind kind, const WCHAR *class_name );
void vector_add_ref( struct winrt_vector *impl );
void vector_release( struct winrt_vector *impl );
ULONG vector_size( struct winrt_vector *impl );
HRESULT vector_append( struct winrt_vector *impl, void *item );
void *vector_get_at( struct winrt_vector *impl, ULONG index );
void *vector_iface( struct winrt_vector *impl );
void *vector_view_iface( struct winrt_vector *impl );
void *vector_iterable_iface( struct winrt_vector *impl );

struct winrt_map *create_map( const IID *iid_map, const IID *iid_view, const IID *iid_iterable,
                              const IID *iid_iterator, const IID *iid_kvp, enum elem_kind kind,
                              const WCHAR *class_name );
void map_add_ref( struct winrt_map *impl );
void map_release( struct winrt_map *impl );
void map_set_outer( struct winrt_map *impl, IInspectable *outer );
HRESULT map_insert( struct winrt_map *impl, HSTRING key, void *value );
HRESULT map_lookup( struct winrt_map *impl, HSTRING key, void **value );
ULONG map_size( struct winrt_map *impl );
void *map_iface( struct winrt_map *impl );
void *map_view_iface( struct winrt_map *impl );
void *map_iterable_iface( struct winrt_map *impl );

/*
 * Born-completed IAsyncOperation`1<T> / IAsyncAction implementation (async.c),
 * the same two-vtable object the pushnotifications module hands out: the
 * operation vtable first, the IAsyncInfo vtable second, QueryInterface
 * switching between them; the parameterized IID and the runtime class name
 * are per-object fields.  The result is created with the operation and comes
 * in three flavours: a COM pointer, an HSTRING, or a 4-byte value (value
 * type T), which decides how GetResults publishes it and how Release frees it.
 */
enum op_result_kind { OP_RESULT_COM, OP_RESULT_STRING, OP_RESULT_VALUE32 };

struct async_op;

extern const void *async_op_vtbl[];
extern const void *async_info_vtbl[];
extern const void *async_action_vtbl[];

struct async_op *create_async_op( const IID *iid, const WCHAR *class_name, void *result, enum op_result_kind kind );
struct async_op *create_async_action( void );
void *async_op_iface( struct async_op *impl );


/*
 * IDataTransferManagerInterop: the shell interop interface of the
 * DataTransferManager class (GetForWindow / ShowShareUIForWindow).  The
 * interface lives in include/shobjidl_core.idl:42 and the IID is the
 * generated DEFINE_GUID of include/shobjidl_core.h
 * (IID_IDataTransferManagerInterop 0x3a3dcd6c-3eab-43dc-bcde-45671ce800c8),
 * the value twinapi.appcore.dll's DataTransferManager stub answers in this
 * tree.  Declared locally, shobjidl.h not included: pulling the shell GUID
 * set into a module that imports the uuid library produces multiple
 * definitions at link time.
 */
typedef struct IDataTransferManagerInterop IDataTransferManagerInterop;
EXTERN_C const IID IID_IDataTransferManagerInterop;
typedef struct IDataTransferManagerInteropVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IDataTransferManagerInterop *This, REFIID riid, void **ppv );
    ULONG (STDMETHODCALLTYPE *AddRef)( IDataTransferManagerInterop *This );
    ULONG (STDMETHODCALLTYPE *Release)( IDataTransferManagerInterop *This );
    HRESULT (STDMETHODCALLTYPE *GetForWindow)( IDataTransferManagerInterop *This, HWND hwnd, REFIID riid, void **manager );
    HRESULT (STDMETHODCALLTYPE *ShowShareUIForWindow)( IDataTransferManagerInterop *This, HWND hwnd );
} IDataTransferManagerInteropVtbl;

struct IDataTransferManagerInterop
{
    const IDataTransferManagerInteropVtbl *lpVtbl;
};

#define IDataTransferManagerInterop_QueryInterface(This,riid,ppv)   (This)->lpVtbl->QueryInterface(This,riid,ppv)
#define IDataTransferManagerInterop_AddRef(This)                    (This)->lpVtbl->AddRef(This)
#define IDataTransferManagerInterop_Release(This)                   (This)->lpVtbl->Release(This)
#define IDataTransferManagerInterop_GetForWindow(This,h,riid,ppm)   (This)->lpVtbl->GetForWindow(This,h,riid,ppm)
#define IDataTransferManagerInterop_ShowShareUIForWindow(This,h)    (This)->lpVtbl->ShowShareUIForWindow(This,h)

/* StandardDataFormats canonical format-name strings (standard_formats.c). */
extern const WCHAR format_text[];
extern const WCHAR format_html[];
extern const WCHAR format_uri[];
extern const WCHAR format_rtf[];
extern const WCHAR format_bitmap[];
extern const WCHAR format_storage_items[];
extern const WCHAR format_web_link[];
extern const WCHAR format_application_link[];
extern const WCHAR format_user_activity_json[];
extern const WCHAR format_jpeg[];
extern const WCHAR format_gif[];
extern const WCHAR format_png[];
extern const WCHAR format_json[];
extern const WCHAR format_xaml_document[];
extern const WCHAR format_custom[];

extern IActivationFactory *clipboard_statics_factory;
extern IActivationFactory *standard_data_formats_factory;
extern IActivationFactory *core_drag_drop_manager_factory;
extern IActivationFactory *data_transfer_manager_factory;
extern IActivationFactory *html_format_helper_factory;
extern IActivationFactory *share_provider_factory;

#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *impl_from( iface_type *iface )                                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )

#endif /* __WINE_DATAEXCHANGE_PRIVATE_H */
