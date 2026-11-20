/* WinRT DataPackageView object + IDataObject clipboard adapter
 *
 * Copyright 2026 Wine Developers
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dataexchange);

/*
 * The DataPackageView the clipboard hands out and GetView produces:
 * - Over a DataPackage (impl->package != NULL): getters mirror the package's
 *   format table, and QI(IID_IDataObject) exposes an adapter that exports
 *   Text -> CF_UNICODETEXT/CF_TEXT/CF_OEMTEXT, Html -> CF_HTML ("HTML Format"),
 *   Rtf -> "Rich Text Format", Bitmap -> CF_BITMAP/CF_DIB, plus custom formats.
 * - Over an IDataObject (impl->data_obj != NULL, e.g. from OleGetClipboard()):
 *   getters read the live clipboard formats from impl->data_obj, and
 *   QI(IID_IDataObject) delegates to impl->data_obj.
 * - Empty view (both NULL): reports no formats, false Contains, empty results.
 */

struct data_package_view
{
    const void *view_vtbl;          /* IDataPackageView at offset 0 */
    const void *view2_vtbl;         /* IDataPackageView2 at offset 1 pointer */
    const void *view3_vtbl;         /* IDataPackageView3 at offset 2 pointers */
    const void *view4_vtbl;         /* IDataPackageView4 at offset 3 pointers */
    const void *data_object_vtbl;   /* IDataObject at offset 4 pointers */
    LONG ref;
    struct data_package *package;   /* owned, may be NULL */
    IDataObject *data_obj;          /* owned, may be NULL */
    IUnknown *ftm;                  /* lazy FreeThreadedMarshaler, owned */
    HSTRING accepted_format;
};

static const void *data_package_view_vtbl[];
static const void *data_package_view2_vtbl[];
static const void *data_package_view3_vtbl[];
static const void *data_package_view4_vtbl[];
static const IDataObjectVtbl data_package_view_data_object_vtbl;

static struct data_package_view *impl_from_any( const void *iface )
{
    static const void *groups[] =
    {
        data_package_view_vtbl,
        data_package_view2_vtbl,
        data_package_view3_vtbl,
        data_package_view4_vtbl,
        &data_package_view_data_object_vtbl,
    };
    static const int offsets[] = { 0, 1, 2, 3, 4 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct data_package_view *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct data_package_view *)iface;
}

/* ------------------------------------------- clipboard formats --------- */

UINT clipboard_html_format( void )
{
    static UINT cf;
    if (!cf) cf = RegisterClipboardFormatW( L"HTML Format" );
    return cf;
}

UINT clipboard_rtf_format( void )
{
    static UINT cf;
    if (!cf) cf = RegisterClipboardFormatW( L"Rich Text Format" );
    return cf;
}

UINT clipboard_preferred_drop_effect_format( void )
{
    static UINT cf;
    if (!cf) cf = RegisterClipboardFormatW( L"Preferred DropEffect" );
    return cf;
}

static BOOL is_builtin_format_name( const WCHAR *key )
{
    return !wcscmp( key, format_text ) ||
           !wcscmp( key, format_html ) ||
           !wcscmp( key, format_uri ) ||
           !wcscmp( key, format_rtf ) ||
           !wcscmp( key, format_bitmap ) ||
           !wcscmp( key, format_storage_items ) ||
           !wcscmp( key, format_web_link ) ||
           !wcscmp( key, format_application_link );
}

static BOOL is_internal_ole_format_name( const WCHAR *name )
{
    return !wcscmp( name, L"Ole Private Data" ) ||
           !wcscmp( name, L"DataObject" ) ||
           !wcscmp( name, L"Wine Marshalled DataObject" ) ||
           !wcscmp( name, L"OwnerLink" ) ||
           !wcscmp( name, L"Object Descriptor" ) ||
           !wcscmp( name, L"Link Source" ) ||
           !wcscmp( name, L"Link Source Descriptor" ) ||
           !wcscmp( name, L"Preferred DropEffect" );
}

static HRESULT alloc_hglobal_copy( const void *data, SIZE_T size, HGLOBAL *out )
{
    HGLOBAL h;
    void *dst;

    *out = NULL;
    if (!(h = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, size ))) return E_OUTOFMEMORY;
    if (!(dst = GlobalLock( h )))
    {
        GlobalFree( h );
        return E_OUTOFMEMORY;
    }
    memcpy( dst, data, size );
    GlobalUnlock( h );
    *out = h;
    return S_OK;
}

static HRESULT set_hglobal_medium( HGLOBAL h, DWORD tymed, STGMEDIUM *med )
{
    if (tymed & TYMED_HGLOBAL)
    {
        med->tymed = TYMED_HGLOBAL;
        med->hGlobal = h;
        med->pUnkForRelease = NULL;
        return S_OK;
    }
    if (tymed & TYMED_ISTREAM)
    {
        HRESULT hr = CreateStreamOnHGlobal( h, TRUE, &med->pstm );
        if (FAILED( hr ))
        {
            GlobalFree( h );
            return hr;
        }
        med->tymed = TYMED_ISTREAM;
        med->pUnkForRelease = NULL;
        return S_OK;
    }
    GlobalFree( h );
    return DV_E_TYMED;
}

static HRESULT read_medium_bytes( STGMEDIUM *med, BYTE **out_data, SIZE_T *out_size )
{
    *out_data = NULL;
    *out_size = 0;

    if (med->tymed == TYMED_HGLOBAL && med->hGlobal)
    {
        SIZE_T sz = GlobalSize( med->hGlobal );
        void *src = GlobalLock( med->hGlobal );
        BYTE *buf;

        if (!src) return E_FAIL;
        buf = malloc( sz + 4 );
        if (!buf)
        {
            GlobalUnlock( med->hGlobal );
            return E_OUTOFMEMORY;
        }
        memcpy( buf, src, sz );
        memset( buf + sz, 0, 4 );
        GlobalUnlock( med->hGlobal );
        *out_data = buf;
        *out_size = sz;
        return S_OK;
    }
    if (med->tymed == TYMED_ISTREAM && med->pstm)
    {
        LARGE_INTEGER zero = {{ 0 }};
        ULARGE_INTEGER end = {{ 0 }};
        BYTE *buf;
        ULONG read_bytes = 0;
        SIZE_T sz;
        HRESULT hr;

        IStream_Seek( med->pstm, zero, STREAM_SEEK_END, &end );
        IStream_Seek( med->pstm, zero, STREAM_SEEK_SET, NULL );
        if (end.QuadPart > 64 * 1024 * 1024) return E_OUTOFMEMORY;
        sz = (SIZE_T)end.QuadPart;
        buf = malloc( sz + 4 );
        if (!buf) return E_OUTOFMEMORY;
        hr = sz ? IStream_Read( med->pstm, buf, (ULONG)sz, &read_bytes ) : S_OK;
        if (FAILED( hr ))
        {
            free( buf );
            return hr;
        }
        memset( buf + read_bytes, 0, 4 );
        *out_data = buf;
        *out_size = read_bytes;
        return S_OK;
    }
    return DV_E_TYMED;
}

/* ------------------------------------------- CF_HTML helpers ----------- */

HRESULT build_cf_html_utf8( const WCHAR *html, UINT32 len, HGLOBAL *out )
{
    static const char hdr_fmt[] =
        "Version:0.9\r\n"
        "StartHTML:%010u\r\n"
        "EndHTML:%010u\r\n"
        "StartFragment:%010u\r\n"
        "EndFragment:%010u\r\n";
    static const char body_prefix[] = "<html><body>\r\n<!--StartFragment-->";
    static const char body_suffix[] = "<!--EndFragment-->\r\n</body></html>";
    static const char start_frag_tag[] = "<!--StartFragment-->";
    static const char end_frag_tag[] = "<!--EndFragment-->";
    char *utf8, header[256], *dst;
    int utf8_len, hdr_len;
    const char *frag_s, *frag_e, *html_tag;
    UINT32 start_html, end_html, start_frag, end_frag;
    HGLOBAL h;

    *out = NULL;
    if (!html) return E_INVALIDARG;

    utf8_len = WideCharToMultiByte( CP_UTF8, 0, html, len, NULL, 0, NULL, NULL );
    if (utf8_len < 0) return E_FAIL;
    if (!(utf8 = malloc( utf8_len + 1 ))) return E_OUTOFMEMORY;
    if (utf8_len) WideCharToMultiByte( CP_UTF8, 0, html, len, utf8, utf8_len, NULL, NULL );
    utf8[utf8_len] = '\0';

    hdr_len = snprintf( header, sizeof(header), hdr_fmt, 0u, 0u, 0u, 0u );

    frag_s = strstr( utf8, start_frag_tag );
    frag_e = frag_s ? strstr( frag_s + sizeof(start_frag_tag) - 1, end_frag_tag ) : NULL;
    html_tag = strstr( utf8, "<html" );
    if (!html_tag) html_tag = strstr( utf8, "<HTML" );

    if (frag_s && frag_e && html_tag && html_tag <= frag_s)
    {
        SIZE_T body_len = utf8_len - (html_tag - utf8);
        start_html = hdr_len;
        start_frag = hdr_len + (UINT32)((frag_s + sizeof(start_frag_tag) - 1) - html_tag);
        end_frag = hdr_len + (UINT32)(frag_e - html_tag);
        end_html = hdr_len + (UINT32)body_len;

        snprintf( header, sizeof(header), hdr_fmt, start_html, end_html, start_frag, end_frag );
        if (!(h = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, end_html + 1 )))
        {
            free( utf8 );
            return E_OUTOFMEMORY;
        }
        dst = GlobalLock( h );
        memcpy( dst, header, hdr_len );
        memcpy( dst + hdr_len, html_tag, body_len );
        dst[end_html] = '\0';
        GlobalUnlock( h );
        free( utf8 );
        *out = h;
        return S_OK;
    }

    if (frag_s && frag_e)
    {
        const char *frag_begin = frag_s + sizeof(start_frag_tag) - 1;
        SIZE_T frag_len = frag_e - frag_begin;

        start_html = hdr_len;
        start_frag = hdr_len + (UINT32)(sizeof(body_prefix) - 1);
        end_frag = start_frag + (UINT32)frag_len;
        end_html = end_frag + (UINT32)(sizeof(body_suffix) - 1);

        snprintf( header, sizeof(header), hdr_fmt, start_html, end_html, start_frag, end_frag );
        if (!(h = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, end_html + 1 )))
        {
            free( utf8 );
            return E_OUTOFMEMORY;
        }
        dst = GlobalLock( h );
        memcpy( dst, header, hdr_len );
        memcpy( dst + hdr_len, body_prefix, sizeof(body_prefix) - 1 );
        memcpy( dst + start_frag, frag_begin, frag_len );
        memcpy( dst + end_frag, body_suffix, sizeof(body_suffix) - 1 );
        dst[end_html] = '\0';
        GlobalUnlock( h );
        free( utf8 );
        *out = h;
        return S_OK;
    }

    start_html = hdr_len;
    start_frag = hdr_len + (UINT32)(sizeof(body_prefix) - 1);
    end_frag = start_frag + (UINT32)utf8_len;
    end_html = end_frag + (UINT32)(sizeof(body_suffix) - 1);

    snprintf( header, sizeof(header), hdr_fmt, start_html, end_html, start_frag, end_frag );
    if (!(h = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, end_html + 1 )))
    {
        free( utf8 );
        return E_OUTOFMEMORY;
    }
    dst = GlobalLock( h );
    memcpy( dst, header, hdr_len );
    memcpy( dst + hdr_len, body_prefix, sizeof(body_prefix) - 1 );
    memcpy( dst + start_frag, utf8, utf8_len );
    memcpy( dst + end_frag, body_suffix, sizeof(body_suffix) - 1 );
    dst[end_html] = '\0';
    GlobalUnlock( h );
    free( utf8 );
    *out = h;
    return S_OK;
}

HRESULT parse_cf_html_utf8( const char *utf8, SIZE_T utf8_len, HSTRING *out )
{
    static const WCHAR start_tag[] = L"<!--StartFragment-->";
    static const WCHAR end_tag[] = L"<!--EndFragment-->";
    static const WCHAR hdr_fmt[] =
        L"Version:0.9\r\n"
        L"StartHTML:%010u\r\n"
        L"EndHTML:%010u\r\n"
        L"StartFragment:%010u\r\n"
        L"EndFragment:%010u\r\n";
    SIZE_T actual_len = 0;
    int wlen;
    WCHAR *wbuf;
    const WCHAR *sf, *ef, *html_pos;
    HRESULT hr;

    *out = NULL;
    if (!utf8 || !utf8_len) return WindowsCreateString( L"", 0, out );

    while (actual_len < utf8_len && utf8[actual_len] != '\0') actual_len++;
    if (!actual_len) return WindowsCreateString( L"", 0, out );

    wlen = MultiByteToWideChar( CP_UTF8, 0, utf8, (int)actual_len, NULL, 0 );
    if (wlen <= 0) return WindowsCreateString( L"", 0, out );

    if (!(wbuf = malloc( (wlen + 1) * sizeof(WCHAR) ))) return E_OUTOFMEMORY;
    MultiByteToWideChar( CP_UTF8, 0, utf8, (int)actual_len, wbuf, wlen );
    wbuf[wlen] = 0;

    /* Recompute WCHAR offsets when StartFragment/EndFragment tags exist so
     * HtmlFormatHelper::GetStaticFragment matches even with multi-byte UTF-8. */
    sf = wcsstr( wbuf, start_tag );
    ef = sf ? wcsstr( sf + ARRAY_SIZE( start_tag ) - 1, end_tag ) : NULL;
    html_pos = wcsstr( wbuf, L"<html" );
    if (!html_pos) html_pos = wcsstr( wbuf, L"<HTML" );
    if (sf && ef && html_pos && html_pos <= sf)
    {
        WCHAR hdr[256];
        UINT32 hdr_len = swprintf( hdr, ARRAY_SIZE( hdr ), hdr_fmt, 0u, 0u, 0u, 0u );
        UINT32 body_len = (UINT32)(wlen - (html_pos - wbuf));
        UINT32 s_html = hdr_len;
        UINT32 s_frag = hdr_len + (UINT32)((sf + ARRAY_SIZE( start_tag ) - 1) - html_pos);
        UINT32 e_frag = hdr_len + (UINT32)(ef - html_pos);
        UINT32 e_html = hdr_len + body_len;
        WCHAR *norm = malloc( (e_html + 1) * sizeof(WCHAR) );

        if (norm)
        {
            swprintf( hdr, ARRAY_SIZE( hdr ), hdr_fmt, s_html, e_html, s_frag, e_frag );
            memcpy( norm, hdr, hdr_len * sizeof(WCHAR) );
            memcpy( norm + hdr_len, html_pos, body_len * sizeof(WCHAR) );
            norm[e_html] = 0;
            hr = WindowsCreateString( norm, e_html, out );
            free( norm );
            free( wbuf );
            return hr;
        }
    }

    hr = WindowsCreateString( wbuf, wlen, out );
    free( wbuf );
    return hr;
}

/* ------------------------------------------- Bitmap <-> StreamRef ------ */

static HRESULT wait_async_info( void *op_iface )
{
    IAsyncInfo *info = NULL;
    enum AsyncStatus status = Started;
    DWORD deadline = GetTickCount() + 5000;
    HRESULT hr;

    if (!op_iface) return E_POINTER;
    hr = IUnknown_QueryInterface( (IUnknown *)op_iface, &IID_IAsyncInfo, (void **)&info );
    if (FAILED( hr )) return S_OK;

    while (SUCCEEDED( IAsyncInfo_get_Status( info, &status ) ) && status == Started)
    {
        if ((LONG)(GetTickCount() - deadline) >= 0)
        {
            IAsyncInfo_Release( info );
            return HRESULT_FROM_WIN32( ERROR_TIMEOUT );
        }
        Sleep( 1 );
    }

    if (status == Error)
    {
        HRESULT err = E_FAIL;
        IAsyncInfo_get_ErrorCode( info, &err );
        IAsyncInfo_Release( info );
        return FAILED( err ) ? err : E_FAIL;
    }
    IAsyncInfo_Release( info );
    return status == Completed ? S_OK : E_ABORT;
}

static HRESULT create_winrt_buffer( UINT32 capacity, __x_ABI_CWindows_CStorage_CStreams_CIBuffer **out_buf )
{
    static const WCHAR cls[] = L"Windows.Storage.Streams.Buffer";
    __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory *factory = NULL;
    HSTRING hstr = NULL;
    HRESULT hr;

    *out_buf = NULL;
    if (FAILED( (hr = WindowsCreateString( cls, ARRAY_SIZE( cls ) - 1, &hstr )) )) return hr;
    hr = RoGetActivationFactory( hstr, &IID___x_ABI_CWindows_CStorage_CStreams_CIBufferFactory, (void **)&factory );
    WindowsDeleteString( hstr );
    if (FAILED( hr )) return hr;

    hr = __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory_Create( factory, capacity, out_buf );
    __x_ABI_CWindows_CStorage_CStreams_CIBufferFactory_Release( factory );
    return hr;
}

static HRESULT decode_bytes_to_hbitmap( const BYTE *bytes, SIZE_T size, HBITMAP *out_hbm )
{
    IWICImagingFactory *wic = NULL;
    IWICStream *wic_stream = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *conv = NULL;
    UINT width = 0, height = 0;
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC hdc;
    HBITMAP hbm = NULL;
    HRESULT hr;

    *out_hbm = NULL;
    if (!bytes || !size) return E_INVALIDARG;

    /* Fast path for uncompressed BMP files ('BM'). */
    if (size >= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) &&
        bytes[0] == 'B' && bytes[1] == 'M')
    {
        const BITMAPFILEHEADER *bfh = (const BITMAPFILEHEADER *)bytes;
        const BITMAPINFO *pbmi = (const BITMAPINFO *)(bytes + sizeof(BITMAPFILEHEADER));

        if (bfh->bfOffBits >= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) &&
            bfh->bfOffBits < size &&
            pbmi->bmiHeader.biSize >= sizeof(BITMAPINFOHEADER) &&
            pbmi->bmiHeader.biCompression == BI_RGB)
        {
            hdc = GetDC( NULL );
            hbm = CreateDIBitmap( hdc, &pbmi->bmiHeader, CBM_INIT, bytes + bfh->bfOffBits, pbmi, DIB_RGB_COLORS );
            ReleaseDC( NULL, hdc );
            if (hbm)
            {
                *out_hbm = hbm;
                return S_OK;
            }
        }
    }

    hr = CoCreateInstance( &CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IWICImagingFactory, (void **)&wic );
    if (FAILED( hr )) return hr;

    hr = IWICImagingFactory_CreateStream( wic, &wic_stream );
    if (SUCCEEDED( hr ))
        hr = IWICStream_InitializeFromMemory( wic_stream, (BYTE *)bytes, (DWORD)size );
    if (SUCCEEDED( hr ))
        hr = IWICImagingFactory_CreateDecoderFromStream( wic, (IStream *)wic_stream, NULL,
                                                         WICDecodeMetadataCacheOnDemand, &decoder );
    if (SUCCEEDED( hr ))
        hr = IWICBitmapDecoder_GetFrame( decoder, 0, &frame );
    if (SUCCEEDED( hr ))
        hr = IWICImagingFactory_CreateFormatConverter( wic, &conv );
    if (SUCCEEDED( hr ))
        hr = IWICFormatConverter_Initialize( conv, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppBGRA,
                                             WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom );
    if (SUCCEEDED( hr ))
        hr = IWICBitmapSource_GetSize( (IWICBitmapSource *)conv, &width, &height );

    if (SUCCEEDED( hr ) && width && height)
    {
        memset( &bmi, 0, sizeof(bmi) );
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)width;
        bmi.bmiHeader.biHeight = -(LONG)height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        hdc = GetDC( NULL );
        hbm = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0 );
        ReleaseDC( NULL, hdc );
        if (hbm && bits)
        {
            hr = IWICBitmapSource_CopyPixels( (IWICBitmapSource *)conv, NULL, width * 4, width * height * 4, bits );
            if (FAILED( hr ))
            {
                DeleteObject( hbm );
                hbm = NULL;
            }
        }
        else hr = E_OUTOFMEMORY;
    }

    if (conv) IWICFormatConverter_Release( conv );
    if (frame) IWICBitmapFrameDecode_Release( frame );
    if (decoder) IWICBitmapDecoder_Release( decoder );
    if (wic_stream) IWICStream_Release( wic_stream );
    IWICImagingFactory_Release( wic );

    if (SUCCEEDED( hr ) && hbm)
    {
        *out_hbm = hbm;
        return S_OK;
    }
    return FAILED( hr ) ? hr : E_FAIL;
}

/* Open the reference, read the whole stream into a malloc'd byte array. */
static HRESULT stream_ref_to_bytes( __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference *ref,
                                    BYTE **out_bytes, SIZE_T *out_size )
{
    __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType *open_op = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamWithContentType *wct = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream *stream = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIInputStream *input = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIBuffer *buf = NULL;
    __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32 *read_op = NULL;
    __x_ABI_CWindows_CStorage_CStreams_CIBuffer *read_buf = NULL;
    IBufferByteAccess *access = NULL;
    UINT64 size64 = 0;
    UINT32 read_len = 0;
    BYTE *raw = NULL, *copy = NULL;
    HRESULT hr;

    *out_bytes = NULL;
    *out_size = 0;
    if (!ref) return E_INVALIDARG;

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_OpenReadAsync( ref, &open_op );
    if (FAILED( hr )) return hr;

    hr = wait_async_info( open_op );
    if (SUCCEEDED( hr ))
        hr = __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType_GetResults( open_op, &wct );
    __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType_Release( open_op );
    if (FAILED( hr ) || !wct) return FAILED( hr ) ? hr : E_FAIL;

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamWithContentType_QueryInterface(
            wct, &IID___x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream, (void **)&stream );
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamWithContentType_Release( wct );
    if (FAILED( hr )) return hr;

    hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_get_Size( stream, &size64 );
    if (SUCCEEDED( hr ))
        hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_Seek( stream, 0 );
    if (SUCCEEDED( hr ) && (!size64 || size64 > 64 * 1024 * 1024))
        hr = E_INVALIDARG;
    if (SUCCEEDED( hr ))
        hr = __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_QueryInterface(
                stream, &IID___x_ABI_CWindows_CStorage_CStreams_CIInputStream, (void **)&input );
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream_Release( stream );
    if (FAILED( hr )) return hr;

    hr = create_winrt_buffer( (UINT32)size64, &buf );
    if (SUCCEEDED( hr ))
        hr = __x_ABI_CWindows_CStorage_CStreams_CIInputStream_ReadAsync(
                input, buf, (UINT32)size64, InputStreamOptions_None, &read_op );
    __x_ABI_CWindows_CStorage_CStreams_CIInputStream_Release( input );

    if (SUCCEEDED( hr ) && read_op)
    {
        hr = wait_async_info( read_op );
        if (SUCCEEDED( hr ))
            hr = __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32_GetResults( read_op, &read_buf );
        __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32_Release( read_op );
    }
    if (SUCCEEDED( hr ) && read_buf)
    {
        hr = __x_ABI_CWindows_CStorage_CStreams_CIBuffer_get_Length( read_buf, &read_len );
        if (SUCCEEDED( hr ))
            hr = __x_ABI_CWindows_CStorage_CStreams_CIBuffer_QueryInterface( read_buf, &IID_IBufferByteAccess, (void **)&access );
        if (SUCCEEDED( hr ))
            hr = IBufferByteAccess_Buffer( access, &raw );
        if (SUCCEEDED( hr ) && raw && read_len)
        {
            if (!(copy = malloc( read_len ))) hr = E_OUTOFMEMORY;
            else memcpy( copy, raw, read_len );
        }
        if (access) IBufferByteAccess_Release( access );
        __x_ABI_CWindows_CStorage_CStreams_CIBuffer_Release( read_buf );
    }
    if (buf) __x_ABI_CWindows_CStorage_CStreams_CIBuffer_Release( buf );

    if (SUCCEEDED( hr ) && copy)
    {
        *out_bytes = copy;
        *out_size = read_len;
        copy = NULL;
    }
    free( copy );
    return hr;
}

HRESULT stream_ref_to_hbitmap( __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference *ref, HBITMAP *out_hbm )
{
    BYTE *bytes = NULL;
    SIZE_T size = 0;
    HRESULT hr;

    *out_hbm = NULL;
    hr = stream_ref_to_bytes( ref, &bytes, &size );
    if (FAILED( hr )) return hr;
    hr = decode_bytes_to_hbitmap( bytes, size, out_hbm );
    free( bytes );
    return hr;
}

static HRESULT hbitmap_to_dib_bytes( HBITMAP hbm, BOOL with_file_header, BYTE **out_buf, SIZE_T *out_size )
{
    BITMAP bm;
    BITMAPINFOHEADER bmih;
    DWORD stride, img_size, header_off;
    SIZE_T total;
    BYTE *buf;
    HDC hdc;
    int rows;

    *out_buf = NULL;
    *out_size = 0;
    if (!hbm || !GetObjectW( hbm, sizeof(bm), &bm ) || bm.bmWidth <= 0 || !bm.bmHeight)
        return E_INVALIDARG;

    memset( &bmih, 0, sizeof(bmih) );
    bmih.biSize = sizeof(BITMAPINFOHEADER);
    bmih.biWidth = bm.bmWidth;
    bmih.biHeight = abs( bm.bmHeight );
    bmih.biPlanes = 1;
    bmih.biBitCount = 32;
    bmih.biCompression = BI_RGB;

    stride = (DWORD)bm.bmWidth * 4;
    img_size = stride * (DWORD)bmih.biHeight;
    header_off = with_file_header ? sizeof(BITMAPFILEHEADER) : 0;
    total = header_off + sizeof(BITMAPINFOHEADER) + img_size;

    if (!(buf = calloc( 1, total ))) return E_OUTOFMEMORY;

    if (with_file_header)
    {
        BITMAPFILEHEADER *bfh = (BITMAPFILEHEADER *)buf;
        bfh->bfType = 0x4D42;
        bfh->bfSize = (DWORD)total;
        bfh->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    }
    memcpy( buf + header_off, &bmih, sizeof(bmih) );

    hdc = CreateCompatibleDC( NULL );
    rows = GetDIBits( hdc, hbm, 0, (UINT)bmih.biHeight,
                      buf + header_off + sizeof(BITMAPINFOHEADER),
                      (BITMAPINFO *)(buf + header_off), DIB_RGB_COLORS );
    DeleteDC( hdc );
    if (!rows)
    {
        free( buf );
        return E_FAIL;
    }

    *out_buf = buf;
    *out_size = total;
    return S_OK;
}

HRESULT hbitmap_to_dib_global( HBITMAP hbm, HGLOBAL *out_global )
{
    BYTE *buf = NULL;
    SIZE_T size = 0;
    HRESULT hr;

    *out_global = NULL;
    hr = hbitmap_to_dib_bytes( hbm, FALSE, &buf, &size );
    if (FAILED( hr )) return hr;
    hr = alloc_hglobal_copy( buf, size, out_global );
    free( buf );
    return hr;
}

/*
 * In-memory bitmap stream reference: the IRandomAccessStreamReference the view
 * hands out for the Bitmap format (GetBitmapAsync, GetDataAsync("Bitmap")).  One
 * object carries four faces, resolved from the face's vtable pointer the way
 * every object in this module does it:
 *
 *   IRandomAccessStreamReference  OpenReadAsync -> born-completed operation
 *   IRandomAccessStreamWithContentType   the stream opened for reading, slot 15
 *                                        answers get_ContentType "image/bmp"
 *   IRandomAccessStream           the same face without the content type
 *   IInputStream                  ReadAsync(buffer, count, options, &op)
 *
 * QueryInterface is a strict IsEqualGUID allowlist.  The ReadAsync points copy
 * out of the owned byte buffer, set the destination IBuffer's Length and return
 * a born-completed IAsyncOperationWithProgress`2<IBuffer,UINT32> over it.  The
 * stream faces carry the Win11 C projection slots (30 ReadAsync(buffer),
 * 31 ReadAsyncAt(op, buffer, count)) like the windows.storage in-memory stream;
 * the position is shared by every face, as the consumers seek to 0 before read.
 */

struct bmp_stream_ref
{
    const void *ref_vtbl;      /* IRandomAccessStreamReference at offset 0 */
    const void *wct_vtbl;      /* IRandomAccessStreamWithContentType at offset 1 pointer */
    const void *stream_vtbl;   /* IRandomAccessStream at offset 2 pointers */
    const void *input_vtbl;    /* IInputStream at offset 3 pointers */
    LONG ref;
    BYTE *data;
    SIZE_T size;
    UINT64 pos;
};

static const void *bmp_ref_vtbl[];
static const void *bmp_wct_vtbl[];
static const void *bmp_stream_vtbl[];
static const void *bmp_input_vtbl[];

static struct bmp_stream_ref *bmp_impl_from( const void *iface )
{
    static const void *groups[] = { bmp_ref_vtbl, bmp_wct_vtbl, bmp_stream_vtbl, bmp_input_vtbl };
    static const int offsets[] = { 0, 1, 2, 3 };
    const void *vtbl = *(const void *const *)iface;
    int i;

    for (i = 0; i < (int)ARRAY_SIZE( groups ); i++)
        if (vtbl == groups[i])
            return (struct bmp_stream_ref *)((const BYTE *)iface - offsets[i] * sizeof(void *));
    return (struct bmp_stream_ref *)iface;
}

static HRESULT WINAPI bmp_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IRandomAccessStreamReference ))
    {
        *out = (void *)&impl->ref_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IRandomAccessStreamWithContentType ))
    {
        *out = (void *)&impl->wct_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IRandomAccessStream ))
    {
        *out = (void *)&impl->stream_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IInputStream ))
    {
        *out = (void *)&impl->input_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI bmp_AddRef( const void *iface )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI bmp_Release( const void *iface )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        free( impl->data );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI bmp_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI bmp_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    const void *vtbl = *(const void *const *)iface;
    const WCHAR *cls;

    if (!class_name) return E_POINTER;
    if (vtbl == bmp_ref_vtbl) cls = L"Windows.Storage.Streams.RandomAccessStreamReference";
    else if (vtbl == bmp_wct_vtbl) cls = L"Windows.Storage.Streams.RandomAccessStreamWithContentType";
    else if (vtbl == bmp_input_vtbl) cls = L"Windows.Storage.Streams.InputStream";
    else cls = L"Windows.Storage.Streams.RandomAccessStream";
    return WindowsCreateString( cls, lstrlenW( cls ), class_name );
}

static HRESULT WINAPI bmp_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI bmp_get_Size( const void *iface, UINT64 *value )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    if (!value) return E_POINTER;
    *value = impl->size;
    return S_OK;
}

static HRESULT WINAPI bmp_put_Size( const void *iface, UINT64 value )
{
    FIXME( "iface %p, value %I64u stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI bmp_GetInputStreamAt( const void *iface, UINT64 position, IInputStream **stream )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    if (!stream) return E_POINTER;
    impl->pos = position;
    *stream = (IInputStream *)&impl->input_vtbl;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI bmp_GetOutputStreamAt( const void *iface, UINT64 position, IOutputStream **stream )
{
    FIXME( "iface %p, position %I64u, stream %p stub!\n", iface, position, stream );
    return E_NOTIMPL;
}

static HRESULT WINAPI bmp_get_Position( const void *iface, UINT64 *value )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    if (!value) return E_POINTER;
    *value = impl->pos;
    return S_OK;
}

static HRESULT WINAPI bmp_Seek( const void *iface, UINT64 position )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    impl->pos = position;
    return S_OK;
}

static HRESULT WINAPI bmp_CloneStream( const void *iface, IRandomAccessStream **stream )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    if (!stream) return E_POINTER;
    *stream = (IRandomAccessStream *)&impl->stream_vtbl;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI bmp_get_CanRead( const void *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}

static HRESULT WINAPI bmp_get_CanWrite( const void *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI bmp_get_ContentType( const void *iface, HSTRING *value )
{
    static const WCHAR type[] = L"image/bmp";

    if (!value) return E_POINTER;
    return WindowsCreateString( type, ARRAY_SIZE( type ) - 1, value );
}

static HRESULT WINAPI bmp_StreamGetResults( const void *iface, void **result )
{
    FIXME( "iface %p, result %p stub!\n", iface, result );
    return E_NOTIMPL;
}

/* Copy out of the owned buffer, publish the length on the destination IBuffer
 * and hand back the born-completed progress operation over the same buffer. */
static HRESULT bmp_read_at( struct bmp_stream_ref *impl, IBuffer *buffer, UINT64 pos, UINT32 count,
                            __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32 **operation )
{
    __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32 *op;
    struct async_op *async;
    IBufferByteAccess *access = NULL;
    BYTE *dest = NULL;
    UINT32 cap = 0, to_read = 0;
    HRESULT hr;

    if (!buffer || !operation) return E_POINTER;
    *operation = NULL;
    IBuffer_get_Capacity( buffer, &cap );

    if (pos < impl->size)
    {
        UINT64 avail = impl->size - pos;
        to_read = (avail < count) ? (UINT32)avail : count;
        if (to_read > cap) to_read = cap;
        if (to_read && SUCCEEDED( hr = IBuffer_QueryInterface( buffer, &IID_IBufferByteAccess, (void **)&access ) ))
        {
            if (SUCCEEDED( IBufferByteAccess_Buffer( access, &dest ) ) && dest)
                memcpy( dest, impl->data + pos, to_read );
            IBufferByteAccess_Release( access );
        }
        impl->pos = pos + to_read;
    }
    IBuffer_put_Length( buffer, to_read );

    async = create_async_op_progress( &IID___FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32,
                                      L"Windows.Foundation.IAsyncOperationWithProgress`2<Windows.Storage.Streams.IBuffer, UInt32>",
                                      buffer );
    if (!async) return E_OUTOFMEMORY;
    op = (void *)async_op_iface( async );
    *operation = op;
    return S_OK;
}

static HRESULT WINAPI bmp_ReadAsync( const void *iface, IBuffer *buffer,
                                     __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32 **operation )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    UINT32 cap = 0;

    if (buffer) IBuffer_get_Capacity( buffer, &cap );
    return bmp_read_at( impl, buffer, impl->pos, cap, operation );
}

static HRESULT WINAPI bmp_ReadAsyncAt( const void *iface,
                                       __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32 **operation,
                                       IBuffer *buffer, UINT32 count )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    return bmp_read_at( impl, buffer, impl->pos, count, operation );
}

static HRESULT WINAPI bmp_input_ReadAsync( const void *iface, IBuffer *buffer, UINT32 count,
                                           InputStreamOptions options,
                                           __FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT32 **operation )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    return bmp_read_at( impl, buffer, impl->pos, count, operation );
}

static HRESULT WINAPI bmp_ref_OpenReadAsync( const void *iface,
                                             __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType **operation )
{
    struct bmp_stream_ref *impl = bmp_impl_from( iface );
    struct async_op *async;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    async = create_async_op( &IID___FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamWithContentType,
                             L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.RandomAccessStreamWithContentType>",
                             (void *)&impl->wct_vtbl, OP_RESULT_COM );
    if (!async) return E_OUTOFMEMORY;
    *operation = (void *)async_op_iface( async );
    return S_OK;
}

/* Stream faces in the Win11 C projection layout: slots 0-14 the classic
 * IRandomAccessStream, 15-29/37-39 projection fillers (QueryInterface-shaped),
 * 30 ReadAsync(buffer), 31 ReadAsyncAt(op, buffer, count), 32-36 GetResults
 * fillers; the WithContentType face answers get_ContentType at slot 15. */
static const void *bmp_stream_vtbl[] =
{
    bmp_QueryInterface,          /* 0 */
    bmp_AddRef,
    bmp_Release,
    bmp_GetIids,
    bmp_GetRuntimeClassName,
    bmp_GetTrustLevel,
    bmp_get_Size,                /* 6 */
    bmp_put_Size,
    bmp_GetInputStreamAt,
    bmp_GetOutputStreamAt,
    bmp_get_Position,            /* 10 */
    bmp_Seek,
    bmp_CloneStream,
    bmp_get_CanRead,
    bmp_get_CanWrite,            /* 14 */
    bmp_QueryInterface,          /* 15-29 projection fillers */
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_ReadAsync,               /* 30 ReadAsync(buffer) */
    bmp_ReadAsyncAt,             /* 31 ReadAsyncAt(op, buffer, count) */
    bmp_StreamGetResults,        /* 32-36 GetResults fillers */
    bmp_StreamGetResults,
    bmp_StreamGetResults,
    bmp_StreamGetResults,
    bmp_StreamGetResults,
    bmp_QueryInterface,          /* 37-39 projection fillers */
    bmp_QueryInterface,
    bmp_QueryInterface,
};

static const void *bmp_wct_vtbl[] =
{
    bmp_QueryInterface,          /* 0 */
    bmp_AddRef,
    bmp_Release,
    bmp_GetIids,
    bmp_GetRuntimeClassName,
    bmp_GetTrustLevel,
    bmp_get_Size,                /* 6 */
    bmp_put_Size,
    bmp_GetInputStreamAt,
    bmp_GetOutputStreamAt,
    bmp_get_Position,            /* 10 */
    bmp_Seek,
    bmp_CloneStream,
    bmp_get_CanRead,
    bmp_get_CanWrite,            /* 14 */
    bmp_get_ContentType,         /* 15 */
    bmp_QueryInterface,          /* 16-29 projection fillers */
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_QueryInterface,
    bmp_ReadAsync,               /* 30 ReadAsync(buffer) */
    bmp_ReadAsyncAt,             /* 31 ReadAsyncAt(op, buffer, count) */
    bmp_StreamGetResults,        /* 32-36 GetResults fillers */
    bmp_StreamGetResults,
    bmp_StreamGetResults,
    bmp_StreamGetResults,
    bmp_StreamGetResults,
    bmp_QueryInterface,          /* 37-39 projection fillers */
    bmp_QueryInterface,
    bmp_QueryInterface,
};

static const void *bmp_ref_vtbl[] =
{
    bmp_QueryInterface,
    bmp_AddRef,
    bmp_Release,
    bmp_GetIids,
    bmp_GetRuntimeClassName,
    bmp_GetTrustLevel,
    bmp_ref_OpenReadAsync,
};

static const void *bmp_input_vtbl[] =
{
    bmp_QueryInterface,
    bmp_AddRef,
    bmp_Release,
    bmp_GetIids,
    bmp_GetRuntimeClassName,
    bmp_GetTrustLevel,
    bmp_input_ReadAsync,
};

static HRESULT bmp_bytes_to_stream_ref( const BYTE *bytes, SIZE_T size,
                                        __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference **out_ref )
{
    struct bmp_stream_ref *impl;

    *out_ref = NULL;
    if (!bytes || !size) return E_INVALIDARG;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    if (!(impl->data = malloc( size )))
    {
        free( impl );
        return E_OUTOFMEMORY;
    }
    memcpy( impl->data, bytes, size );
    impl->ref_vtbl = bmp_ref_vtbl;
    impl->wct_vtbl = bmp_wct_vtbl;
    impl->stream_vtbl = bmp_stream_vtbl;
    impl->input_vtbl = bmp_input_vtbl;
    impl->ref = 1;
    impl->size = size;

    *out_ref = (void *)&impl->ref_vtbl;
    return S_OK;
}

HRESULT hbitmap_to_stream_ref( HBITMAP hbm, __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference **out_ref )
{
    BYTE *buf = NULL;
    SIZE_T size = 0;
    HRESULT hr;

    *out_ref = NULL;
    hr = hbitmap_to_dib_bytes( hbm, TRUE, &buf, &size );
    if (FAILED( hr )) return hr;
    hr = bmp_bytes_to_stream_ref( buf, size, out_ref );
    free( buf );
    return hr;
}

HRESULT dib_to_stream_ref( const BITMAPINFO *bmi, SIZE_T dib_size,
                           __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference **out_ref )
{
    BYTE *buf;
    BITMAPFILEHEADER bfh;
    DWORD colors = 0, info_hdr_size;
    SIZE_T total;
    HRESULT hr;

    *out_ref = NULL;
    if (!bmi || dib_size < sizeof(BITMAPINFOHEADER)) return E_INVALIDARG;

    info_hdr_size = bmi->bmiHeader.biSize;
    if (bmi->bmiHeader.biClrUsed)
        colors = bmi->bmiHeader.biClrUsed;
    else if (bmi->bmiHeader.biBitCount <= 8)
        colors = 1u << bmi->bmiHeader.biBitCount;
    if (bmi->bmiHeader.biCompression == BI_BITFIELDS && info_hdr_size == sizeof(BITMAPINFOHEADER))
        colors += 3;

    total = sizeof(BITMAPFILEHEADER) + dib_size;
    if (!(buf = malloc( total ))) return E_OUTOFMEMORY;

    bfh.bfType = 0x4D42;
    bfh.bfSize = (DWORD)total;
    bfh.bfReserved1 = 0;
    bfh.bfReserved2 = 0;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + info_hdr_size + colors * sizeof(RGBQUAD);
    memcpy( buf, &bfh, sizeof(bfh) );
    memcpy( buf + sizeof(bfh), bmi, dib_size );

    hr = bmp_bytes_to_stream_ref( buf, total, out_ref );
    free( buf );
    return hr;
}

/* ------------------------------------------- IDataObject helpers ------- */

static HRESULT data_obj_read_text( IDataObject *data_obj, HSTRING *out )
{
    FORMATETC fmt = { CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL | TYMED_ISTREAM };
    STGMEDIUM med;
    BYTE *bytes = NULL;
    SIZE_T size = 0;
    HRESULT hr;

    *out = NULL;
    if (!data_obj) return E_FAIL;

    memset( &med, 0, sizeof(med) );
    if (SUCCEEDED( IDataObject_GetData( data_obj, &fmt, &med ) ))
    {
        hr = read_medium_bytes( &med, &bytes, &size );
        ReleaseStgMedium( &med );
        if (SUCCEEDED( hr ) && bytes)
        {
            const WCHAR *w = (const WCHAR *)bytes;
            SIZE_T max_chars = size / sizeof(WCHAR), wlen = 0;
            while (wlen < max_chars && w[wlen]) wlen++;
            hr = WindowsCreateString( w, (UINT32)wlen, out );
            free( bytes );
            return hr;
        }
        free( bytes );
    }

    fmt.cfFormat = CF_TEXT;
    memset( &med, 0, sizeof(med) );
    if (SUCCEEDED( IDataObject_GetData( data_obj, &fmt, &med ) ))
    {
        hr = read_medium_bytes( &med, &bytes, &size );
        ReleaseStgMedium( &med );
        if (SUCCEEDED( hr ) && bytes)
        {
            SIZE_T alen = 0;
            int wlen;
            WCHAR *wbuf;
            while (alen < size && bytes[alen]) alen++;
            wlen = alen ? MultiByteToWideChar( CP_ACP, 0, (const char *)bytes, (int)alen, NULL, 0 ) : 0;
            if (wlen > 0 && (wbuf = malloc( wlen * sizeof(WCHAR) )))
            {
                MultiByteToWideChar( CP_ACP, 0, (const char *)bytes, (int)alen, wbuf, wlen );
                hr = WindowsCreateString( wbuf, wlen, out );
                free( wbuf );
            }
            else hr = WindowsCreateString( L"", 0, out );
            free( bytes );
            return hr;
        }
        free( bytes );
    }

    return E_FAIL;
}

static HRESULT data_obj_read_cf_bytes( IDataObject *data_obj, UINT cf, BYTE **out_bytes, SIZE_T *out_size )
{
    FORMATETC fmt = { (CLIPFORMAT)cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL | TYMED_ISTREAM };
    STGMEDIUM med;
    HRESULT hr;

    *out_bytes = NULL;
    *out_size = 0;
    if (!data_obj || !cf) return E_FAIL;

    memset( &med, 0, sizeof(med) );
    hr = IDataObject_GetData( data_obj, &fmt, &med );
    if (FAILED( hr )) return hr;
    hr = read_medium_bytes( &med, out_bytes, out_size );
    ReleaseStgMedium( &med );
    return hr;
}

static HRESULT data_obj_read_html( IDataObject *data_obj, HSTRING *out )
{
    BYTE *bytes = NULL;
    SIZE_T size = 0;
    HRESULT hr;

    *out = NULL;
    hr = data_obj_read_cf_bytes( data_obj, clipboard_html_format(), &bytes, &size );
    if (FAILED( hr )) return hr;
    hr = parse_cf_html_utf8( (const char *)bytes, size, out );
    free( bytes );
    return hr;
}

static HRESULT data_obj_read_rtf( IDataObject *data_obj, HSTRING *out )
{
    BYTE *bytes = NULL;
    SIZE_T size = 0;
    HRESULT hr;

    *out = NULL;
    hr = data_obj_read_cf_bytes( data_obj, clipboard_rtf_format(), &bytes, &size );
    if (FAILED( hr )) return hr;

    if (size && bytes)
    {
        SIZE_T alen = 0;
        int wlen;
        WCHAR *wbuf;
        while (alen < size && bytes[alen]) alen++;
        wlen = alen ? MultiByteToWideChar( CP_ACP, 0, (const char *)bytes, (int)alen, NULL, 0 ) : 0;
        if (wlen > 0 && (wbuf = malloc( wlen * sizeof(WCHAR) )))
        {
            MultiByteToWideChar( CP_ACP, 0, (const char *)bytes, (int)alen, wbuf, wlen );
            hr = WindowsCreateString( wbuf, wlen, out );
            free( wbuf );
        }
        else hr = WindowsCreateString( L"", 0, out );
    }
    else hr = WindowsCreateString( L"", 0, out );
    free( bytes );
    return hr;
}

static HRESULT data_obj_read_bitmap( IDataObject *data_obj,
                                     __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference **out_ref )
{
    FORMATETC fmt = { CF_BITMAP, NULL, DVASPECT_CONTENT, -1, TYMED_GDI };
    STGMEDIUM med;
    BYTE *dib = NULL;
    SIZE_T dib_size = 0;
    HRESULT hr;

    *out_ref = NULL;
    if (!data_obj) return E_FAIL;

    memset( &med, 0, sizeof(med) );
    if (SUCCEEDED( IDataObject_GetData( data_obj, &fmt, &med ) ))
    {
        if (med.tymed == TYMED_GDI && med.hBitmap)
        {
            hr = hbitmap_to_stream_ref( med.hBitmap, out_ref );
            ReleaseStgMedium( &med );
            if (SUCCEEDED( hr )) return S_OK;
        }
        else ReleaseStgMedium( &med );
    }

    if (SUCCEEDED( data_obj_read_cf_bytes( data_obj, CF_DIB, &dib, &dib_size ) ) && dib)
    {
        hr = dib_to_stream_ref( (const BITMAPINFO *)dib, dib_size, out_ref );
        free( dib );
        return hr;
    }
    free( dib );

    /* BITMAPV5HEADER carries the classic BITMAPINFO as its prefix. */
    if (SUCCEEDED( data_obj_read_cf_bytes( data_obj, CF_DIBV5, &dib, &dib_size ) ) && dib)
    {
        hr = dib_to_stream_ref( (const BITMAPINFO *)dib, dib_size, out_ref );
        free( dib );
        return hr;
    }
    free( dib );
    return E_FAIL;
}

static BOOL data_obj_has_cf( IDataObject *data_obj, UINT cf, DWORD tymed )
{
    FORMATETC fmt = { (CLIPFORMAT)cf, NULL, DVASPECT_CONTENT, -1, tymed };
    return data_obj && cf && IDataObject_QueryGetData( data_obj, &fmt ) == S_OK;
}

static BOOL data_obj_has_text( IDataObject *data_obj )
{
    return data_obj_has_cf( data_obj, CF_UNICODETEXT, TYMED_HGLOBAL | TYMED_ISTREAM ) ||
           data_obj_has_cf( data_obj, CF_TEXT, TYMED_HGLOBAL | TYMED_ISTREAM ) ||
           data_obj_has_cf( data_obj, CF_OEMTEXT, TYMED_HGLOBAL | TYMED_ISTREAM );
}

static BOOL data_obj_has_bitmap( IDataObject *data_obj )
{
    return data_obj_has_cf( data_obj, CF_BITMAP, TYMED_GDI ) ||
           data_obj_has_cf( data_obj, CF_DIB, TYMED_HGLOBAL | TYMED_ISTREAM ) ||
           data_obj_has_cf( data_obj, CF_DIBV5, TYMED_HGLOBAL | TYMED_ISTREAM );
}

static BOOL data_obj_has_storage_items( IDataObject *data_obj )
{
    return data_obj_has_cf( data_obj, CF_HDROP, TYMED_HGLOBAL | TYMED_ISTREAM );
}

/* The CF_HDROP payload header (include/shlobj.h:1750); declared locally, the
 * module imports only the small header set above (see the
 * IDataTransferManagerInterop note in private.h for the same reason). */
typedef struct
{
    DWORD pFiles;
    POINT pt;
    BOOL  fNC;
    BOOL  fWide;
} dropfiles_hdr;

/* ------------------------------------------- storage items ------------- */

/*
 * The IStorageItem the StorageItems format hands out for each CF_HDROP path
 * (and for each item a SetStorageItems iterable carried).  The object is a
 * path wrapper: Name is the last component of Path, Attributes and the
 * file/folder type come from one GetFileAttributesW snapshot taken at
 * creation, the mutation and property points answer E_NOTIMPL, and
 * QueryInterface answers the strict IUnknown/IInspectable/IAgileObject/
 * IStorageItem set only.
 */

struct storage_item
{
    const void *item_vtbl;      /* IStorageItem at offset 0 */
    LONG ref;
    HSTRING path;
    HSTRING name;
    DWORD attrs;
    BOOL is_dir;
};

static const void *storage_item_vtbl[];

static inline struct storage_item *storage_item_from( const void *iface )
{
    return (struct storage_item *)iface;
}

static HRESULT WINAPI item_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct storage_item *impl = storage_item_from( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IStorageItem ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI item_AddRef( const void *iface )
{
    return InterlockedIncrement( &storage_item_from( iface )->ref );
}

static ULONG WINAPI item_Release( const void *iface )
{
    struct storage_item *impl = storage_item_from( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->path );
        WindowsDeleteString( impl->name );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI item_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI item_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    struct storage_item *impl = storage_item_from( iface );
    const WCHAR *cls = impl->is_dir ? RuntimeClass_Windows_Storage_StorageFolder
                                    : RuntimeClass_Windows_Storage_StorageFile;

    if (!class_name) return E_POINTER;
    return WindowsCreateString( cls, lstrlenW( cls ), class_name );
}

static HRESULT WINAPI item_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI item_RenameAsyncOverload( const void *iface, HSTRING name, void **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring( name ), operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI item_RenameAsync( const void *iface, HSTRING name,
                                        enum __x_ABI_CWindows_CStorage_CNameCollisionOption option,
                                        void **operation )
{
    FIXME( "iface %p, name %s, option %u, operation %p stub!\n", iface, debugstr_hstring( name ), option, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI item_DeleteAsyncOverload( const void *iface, void **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI item_DeleteAsync( const void *iface,
                                        enum __x_ABI_CWindows_CStorage_CStorageDeleteOption option,
                                        void **operation )
{
    FIXME( "iface %p, option %u, operation %p stub!\n", iface, option, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI item_GetBasicPropertiesAsync( const void *iface, void **operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI item_get_Name( const void *iface, HSTRING *value )
{
    struct storage_item *impl = storage_item_from( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->name, value );
}

static HRESULT WINAPI item_get_Path( const void *iface, HSTRING *value )
{
    struct storage_item *impl = storage_item_from( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->path, value );
}

static HRESULT WINAPI item_get_Attributes( const void *iface,
                                           enum __x_ABI_CWindows_CStorage_CFileAttributes *value )
{
    struct storage_item *impl = storage_item_from( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = (enum __x_ABI_CWindows_CStorage_CFileAttributes)impl->attrs;
    return S_OK;
}

static HRESULT WINAPI item_get_DateCreated( const void *iface,
                                            struct __x_ABI_CWindows_CFoundation_CDateTime *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI item_IsOfType( const void *iface,
                                     enum __x_ABI_CWindows_CStorage_CStorageItemTypes type,
                                     boolean *value )
{
    struct storage_item *impl = storage_item_from( iface );

    TRACE( "iface %p, type %u, value %p.\n", iface, type, value );

    if (!value) return E_POINTER;
    switch (type)
    {
    case StorageItemTypes_File:   *value = !impl->is_dir; break;
    case StorageItemTypes_Folder: *value = impl->is_dir; break;
    default:                      *value = FALSE; break;
    }
    return S_OK;
}

static const void *storage_item_vtbl[] =
{
    item_QueryInterface,
    item_AddRef,
    item_Release,
    item_GetIids,
    item_GetRuntimeClassName,
    item_GetTrustLevel,
    item_RenameAsyncOverload,
    item_RenameAsync,
    item_DeleteAsyncOverload,
    item_DeleteAsync,
    item_GetBasicPropertiesAsync,
    item_get_Name,
    item_get_Path,
    item_get_Attributes,
    item_get_DateCreated,
    item_IsOfType,
};

/* Wrap one path (len characters, not NUL-terminated) in an IStorageItem;
 * Name is the last component, trailing directory separators excluded. */
static HRESULT create_storage_item( const WCHAR *path, SIZE_T len, struct storage_item **out )
{
    struct storage_item *impl;
    const WCHAR *name;
    SIZE_T i, nlen;
    DWORD attrs;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->item_vtbl = storage_item_vtbl;
    impl->ref = 1;
    if (FAILED( WindowsCreateString( path, (UINT32)len, &impl->path ) ))
    {
        free( impl );
        return E_OUTOFMEMORY;
    }

    name = path;
    for (i = 0; i < len; i++)
        if (path[i] == L'\\' || path[i] == L'/') name = path + i + 1;
    nlen = len - (name - path);
    while (nlen && (name[nlen - 1] == L'\\' || name[nlen - 1] == L'/')) nlen--;
    if (!nlen)
    {
        name = path;
        nlen = len;
        while (nlen && (name[nlen - 1] == L'\\' || name[nlen - 1] == L'/')) nlen--;
    }
    if (FAILED( WindowsCreateString( name, (UINT32)nlen, &impl->name ) ))
    {
        WindowsDeleteString( impl->path );
        free( impl );
        return E_OUTOFMEMORY;
    }

    attrs = GetFileAttributesW( path );
    impl->is_dir = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    impl->attrs = attrs == INVALID_FILE_ATTRIBUTES ? FILE_ATTRIBUTE_NORMAL : attrs;
    *out = impl;
    return S_OK;
}

/* Serialize the storage item vector into GMEM_MOVEABLE CF_HDROP memory:
 * dropfiles_hdr with pFiles = sizeof(dropfiles_hdr), fWide = TRUE, and the
 * double-NUL-terminated wide path list. */
static HRESULT items_to_hdrop_global( struct winrt_vector *items, HGLOBAL *out )
{
    __x_ABI_CWindows_CStorage_CIStorageItem *item;
    HSTRING *paths = NULL;
    dropfiles_hdr *df;
    WCHAR *dst;
    HGLOBAL h;
    SIZE_T size, total = 0;
    ULONG count, i;
    HRESULT hr = S_OK;

    *out = NULL;
    count = items ? vector_size( items ) : 0;
    if (count)
    {
        if (!(paths = calloc( count, sizeof(*paths) ))) return E_OUTOFMEMORY;
        for (i = 0; i < count; i++)
        {
            item = (__x_ABI_CWindows_CStorage_CIStorageItem *)vector_get_at( items, i );
            if (!item) continue;
            if (SUCCEEDED( __x_ABI_CWindows_CStorage_CIStorageItem_get_Path( item, &paths[i] ) ) && paths[i])
                total += WindowsGetStringLen( paths[i] ) + 1;
            else paths[i] = NULL;
        }
    }

    size = sizeof(dropfiles_hdr) + (total + 1) * sizeof(WCHAR);
    if (!(h = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, size )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    df = GlobalLock( h );
    df->pFiles = sizeof(dropfiles_hdr);
    df->pt.x = df->pt.y = 0;
    df->fNC = FALSE;
    df->fWide = TRUE;
    dst = (WCHAR *)((BYTE *)df + sizeof(dropfiles_hdr));
    for (i = 0; i < count; i++)
    {
        const WCHAR *raw;
        UINT32 len = 0;

        if (!paths[i]) continue;
        raw = WindowsGetStringRawBuffer( paths[i], &len );
        if (len) memcpy( dst, raw, len * sizeof(WCHAR) );
        dst += len;
        *dst++ = 0;
    }
    *dst = 0;
    GlobalUnlock( h );
    *out = h;

done:
    if (paths) for (i = 0; i < count; i++) WindowsDeleteString( paths[i] );
    free( paths );
    return hr;
}

/* Read CF_HDROP (dropfiles_hdr, fWide TRUE and FALSE) and append one
 * IStorageItem per path to the vector, in clipboard order. */
static HRESULT data_obj_read_storage_items( IDataObject *data_obj, struct winrt_vector *items )
{
    const dropfiles_hdr *df;
    struct storage_item *item = NULL;
    BYTE *bytes = NULL;
    SIZE_T size = 0, off;
    HRESULT hr;

    hr = data_obj_read_cf_bytes( data_obj, CF_HDROP, &bytes, &size );
    if (FAILED( hr )) return hr;
    if (!bytes || size < sizeof(dropfiles_hdr))
    {
        free( bytes );
        return E_FAIL;
    }
    df = (const dropfiles_hdr *)bytes;
    off = df->pFiles;
    if (off < sizeof(dropfiles_hdr) || off > size)
    {
        free( bytes );
        return E_FAIL;
    }

    if (df->fWide)
    {
        const WCHAR *p = (const WCHAR *)(bytes + off), *end = (const WCHAR *)(bytes + size);

        while (p < end && *p)
        {
            SIZE_T len = 0;

            while (p + len < end && p[len]) len++;
            if (len && SUCCEEDED( create_storage_item( p, len, &item ) ))
            {
                hr = vector_append( items, item );
                __x_ABI_CWindows_CStorage_CIStorageItem_Release( (void *)item );
                item = NULL;
                if (FAILED( hr )) break;
            }
            p += len + 1;
        }
    }
    else
    {
        const char *p = (const char *)(bytes + off), *end = (const char *)(bytes + size);

        while (p < end && *p)
        {
            int wlen = MultiByteToWideChar( CP_ACP, 0, p, -1, NULL, 0 );
            WCHAR *wbuf;

            if (wlen > 0 && (wbuf = malloc( wlen * sizeof(WCHAR) )))
            {
                MultiByteToWideChar( CP_ACP, 0, p, -1, wbuf, wlen );
                if (SUCCEEDED( create_storage_item( wbuf, lstrlenW( wbuf ), &item ) ))
                {
                    hr = vector_append( items, item );
                    __x_ABI_CWindows_CStorage_CIStorageItem_Release( (void *)item );
                    item = NULL;
                }
                free( wbuf );
                if (FAILED( hr )) break;
            }
            p += strlen( p ) + 1;
        }
    }

    free( bytes );
    return S_OK;
}

static BOOL vector_contains_string( struct winrt_vector *vec, const WCHAR *str )
{
    ULONG i, count = vector_size( vec );

    for (i = 0; i < count; i++)
    {
        HSTRING item = (HSTRING)vector_get_at( vec, i );
        const WCHAR *raw = item ? WindowsGetStringRawBuffer( item, NULL ) : NULL;
        BOOL eq = raw && !wcscmp( raw, str );
        WindowsDeleteString( item );
        if (eq) return TRUE;
    }
    return FALSE;
}

static void vector_append_unique_str( struct winrt_vector *vec, const WCHAR *str )
{
    HSTRING hstr = NULL;

    if (!str || !str[0] || vector_contains_string( vec, str )) return;
    if (SUCCEEDED( WindowsCreateString( str, lstrlenW( str ), &hstr ) ))
    {
        vector_append( vec, hstr );
        WindowsDeleteString( hstr );
    }
}

/* -------------------------------------------------- op helpers --------- */

static HRESULT view_string_op( struct data_package_view *impl, const WCHAR *format,
                               IAsyncOperation_HSTRING **operation )
{
    struct async_op *op;
    HSTRING value = NULL;

    if (impl->data_obj)
    {
        if (!wcscmp( format, format_text )) data_obj_read_text( impl->data_obj, &value );
        else if (!wcscmp( format, format_html )) data_obj_read_html( impl->data_obj, &value );
        else if (!wcscmp( format, format_rtf )) data_obj_read_rtf( impl->data_obj, &value );
    }
    else if (impl->package)
    {
        value = package_get_string( impl->package, format );
    }
    if (!value && FAILED( WindowsCreateString( L"", 0, &value ) )) return E_OUTOFMEMORY;

    op = create_async_op( &IID___FIAsyncOperation_1_HSTRING, L"Windows.Foundation.IAsyncOperation`1<HSTRING>", value, OP_RESULT_STRING );
    WindowsDeleteString( value );
    if (!op) return E_OUTOFMEMORY;
    *operation = (IAsyncOperation_HSTRING *)async_op_iface( op );
    return S_OK;
}

static HRESULT view_com_op( const IID *iid, const WCHAR *class_name, void *result, void *operation )
{
    struct async_op *op;

    op = create_async_op( iid, class_name, result, OP_RESULT_COM );
    if (result) IUnknown_Release( (IUnknown *)result );
    if (!op) return E_OUTOFMEMORY;
    *(void **)operation = async_op_iface( op );
    return S_OK;
}

/* ------------------------------------------------ IEnumFORMATETC ------- */

struct pkg_enum_fmtetc
{
    IEnumFORMATETC IEnumFORMATETC_iface;
    LONG ref;
    ULONG pos;
    ULONG count;
    FORMATETC fmts[1];
};

static inline struct pkg_enum_fmtetc *impl_from_IEnumFORMATETC( IEnumFORMATETC *iface )
{
    return CONTAINING_RECORD( iface, struct pkg_enum_fmtetc, IEnumFORMATETC_iface );
}

static HRESULT WINAPI pkg_enum_QueryInterface( IEnumFORMATETC *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IEnumFORMATETC ))
    {
        *out = iface;
        IEnumFORMATETC_AddRef( iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI pkg_enum_AddRef( IEnumFORMATETC *iface )
{
    struct pkg_enum_fmtetc *impl = impl_from_IEnumFORMATETC( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI pkg_enum_Release( IEnumFORMATETC *iface )
{
    struct pkg_enum_fmtetc *impl = impl_from_IEnumFORMATETC( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI pkg_enum_Next( IEnumFORMATETC *iface, ULONG celt, FORMATETC *rgelt, ULONG *fetched )
{
    struct pkg_enum_fmtetc *impl = impl_from_IEnumFORMATETC( iface );
    ULONG avail, n, i;

    if (!rgelt) return E_POINTER;
    if (celt > 1 && !fetched) return E_INVALIDARG;

    avail = impl->pos < impl->count ? impl->count - impl->pos : 0;
    n = min( celt, avail );
    for (i = 0; i < n; i++) rgelt[i] = impl->fmts[impl->pos++];
    if (fetched) *fetched = n;
    return n == celt ? S_OK : S_FALSE;
}

static HRESULT WINAPI pkg_enum_Skip( IEnumFORMATETC *iface, ULONG celt )
{
    struct pkg_enum_fmtetc *impl = impl_from_IEnumFORMATETC( iface );
    if (impl->pos + celt > impl->count)
    {
        impl->pos = impl->count;
        return S_FALSE;
    }
    impl->pos += celt;
    return S_OK;
}

static HRESULT WINAPI pkg_enum_Reset( IEnumFORMATETC *iface )
{
    struct pkg_enum_fmtetc *impl = impl_from_IEnumFORMATETC( iface );
    impl->pos = 0;
    return S_OK;
}

static const IEnumFORMATETCVtbl pkg_enum_vtbl;

static HRESULT WINAPI pkg_enum_Clone( IEnumFORMATETC *iface, IEnumFORMATETC **out )
{
    struct pkg_enum_fmtetc *impl = impl_from_IEnumFORMATETC( iface );
    struct pkg_enum_fmtetc *copy;
    SIZE_T sz = offsetof( struct pkg_enum_fmtetc, fmts[impl->count] );

    if (!out) return E_POINTER;
    if (!(copy = malloc( sz ))) return E_OUTOFMEMORY;
    memcpy( copy, impl, sz );
    copy->ref = 1;
    *out = &copy->IEnumFORMATETC_iface;
    return S_OK;
}

static const IEnumFORMATETCVtbl pkg_enum_vtbl =
{
    pkg_enum_QueryInterface,
    pkg_enum_AddRef,
    pkg_enum_Release,
    pkg_enum_Next,
    pkg_enum_Skip,
    pkg_enum_Reset,
    pkg_enum_Clone,
};

static void add_fmt( struct pkg_enum_fmtetc *e, CLIPFORMAT cf, DWORD tymed )
{
    FORMATETC *f = &e->fmts[e->count++];
    f->cfFormat = cf;
    f->ptd = NULL;
    f->dwAspect = DVASPECT_CONTENT;
    f->lindex = -1;
    f->tymed = tymed;
}

/* ------------------------------------------------ IDataObject ---------- */

static HRESULT WINAPI view_do_QueryInterface( IDataObject *iface, REFIID iid, void **out )
{
    struct data_package_view *impl = impl_from_any( iface );
    return IInspectable_QueryInterface( (IInspectable *)impl, iid, out );
}

static ULONG WINAPI view_do_AddRef( IDataObject *iface )
{
    struct data_package_view *impl = impl_from_any( iface );
    return IInspectable_AddRef( (IInspectable *)impl );
}

static ULONG WINAPI view_do_Release( IDataObject *iface )
{
    struct data_package_view *impl = impl_from_any( iface );
    return IInspectable_Release( (IInspectable *)impl );
}

static const WCHAR *package_find_custom_key_for_cf( struct data_package *pkg, CLIPFORMAT cf )
{
    WCHAR name[256];
    ULONG count, i;

    if (cf < 0xC000 || !GetClipboardFormatNameW( cf, name, ARRAY_SIZE( name ) )) return NULL;
    count = package_format_count( pkg );
    package_lock( pkg );
    for (i = 0; i < count; i++)
    {
        const WCHAR *key = package_format_key( pkg, i );
        if (key && !is_builtin_format_name( key ) && !wcscmp( key, name ))
        {
            package_unlock( pkg );
            return key;
        }
    }
    package_unlock( pkg );
    return NULL;
}

static HRESULT WINAPI view_do_GetData( IDataObject *iface, FORMATETC *fmt, STGMEDIUM *med )
{
    struct data_package_view *impl = impl_from_any( iface );
    enum fmt_kind kind;

    TRACE( "iface %p, fmt %p, med %p.\n", iface, fmt, med );

    if (!fmt || !med) return E_INVALIDARG;
    memset( med, 0, sizeof(*med) );

    if (impl->data_obj) return IDataObject_GetData( impl->data_obj, fmt, med );
    if (!impl->package) return DV_E_FORMATETC;
    if (fmt->dwAspect != DVASPECT_CONTENT || fmt->lindex != -1) return DV_E_FORMATETC;

    if (fmt->cfFormat == CF_UNICODETEXT)
    {
        HSTRING hstr;
        const WCHAR *raw;
        UINT32 len = 0;
        HGLOBAL h;
        HRESULT hr;

        package_lock( impl->package );
        package_get_raw( impl->package, format_text, &kind );
        package_unlock( impl->package );
        if (kind != FMT_STRING) return DV_E_FORMATETC;
        if (!(fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM))) return DV_E_TYMED;

        hstr = package_get_string( impl->package, format_text );
        raw = WindowsGetStringRawBuffer( hstr, &len );
        hr = alloc_hglobal_copy( raw ? raw : L"", (len + 1) * sizeof(WCHAR), &h );
        WindowsDeleteString( hstr );
        if (FAILED( hr )) return hr;
        return set_hglobal_medium( h, fmt->tymed, med );
    }

    if (fmt->cfFormat == CF_TEXT || fmt->cfFormat == CF_OEMTEXT)
    {
        UINT cp = fmt->cfFormat == CF_OEMTEXT ? CP_OEMCP : CP_ACP;
        HSTRING hstr;
        const WCHAR *raw;
        UINT32 len = 0;
        int mblen;
        HGLOBAL h;
        char *dst;

        package_lock( impl->package );
        package_get_raw( impl->package, format_text, &kind );
        package_unlock( impl->package );
        if (kind != FMT_STRING) return DV_E_FORMATETC;
        if (!(fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM))) return DV_E_TYMED;

        hstr = package_get_string( impl->package, format_text );
        raw = WindowsGetStringRawBuffer( hstr, &len );
        mblen = len ? WideCharToMultiByte( cp, 0, raw, len, NULL, 0, NULL, NULL ) : 0;
        if (!(h = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, mblen + 1 )))
        {
            WindowsDeleteString( hstr );
            return E_OUTOFMEMORY;
        }
        dst = GlobalLock( h );
        if (mblen) WideCharToMultiByte( cp, 0, raw, len, dst, mblen, NULL, NULL );
        dst[mblen] = '\0';
        GlobalUnlock( h );
        WindowsDeleteString( hstr );
        return set_hglobal_medium( h, fmt->tymed, med );
    }

    if (fmt->cfFormat == clipboard_html_format())
    {
        HSTRING hstr;
        const WCHAR *raw;
        UINT32 len = 0;
        HGLOBAL h = NULL;
        HRESULT hr;

        package_lock( impl->package );
        package_get_raw( impl->package, format_html, &kind );
        package_unlock( impl->package );
        if (kind != FMT_STRING) return DV_E_FORMATETC;
        if (!(fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM))) return DV_E_TYMED;

        hstr = package_get_string( impl->package, format_html );
        raw = WindowsGetStringRawBuffer( hstr, &len );
        hr = build_cf_html_utf8( raw ? raw : L"", len, &h );
        WindowsDeleteString( hstr );
        if (FAILED( hr )) return hr;
        return set_hglobal_medium( h, fmt->tymed, med );
    }

    if (fmt->cfFormat == clipboard_rtf_format())
    {
        HSTRING hstr;
        const WCHAR *raw;
        UINT32 len = 0;
        int mblen;
        HGLOBAL h;
        char *dst;

        package_lock( impl->package );
        package_get_raw( impl->package, format_rtf, &kind );
        package_unlock( impl->package );
        if (kind != FMT_STRING) return DV_E_FORMATETC;
        if (!(fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM))) return DV_E_TYMED;

        hstr = package_get_string( impl->package, format_rtf );
        raw = WindowsGetStringRawBuffer( hstr, &len );
        mblen = len ? WideCharToMultiByte( CP_ACP, 0, raw, len, NULL, 0, NULL, NULL ) : 0;
        if (!(h = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, mblen + 1 )))
        {
            WindowsDeleteString( hstr );
            return E_OUTOFMEMORY;
        }
        dst = GlobalLock( h );
        if (mblen) WideCharToMultiByte( CP_ACP, 0, raw, len, dst, mblen, NULL, NULL );
        dst[mblen] = '\0';
        GlobalUnlock( h );
        WindowsDeleteString( hstr );
        return set_hglobal_medium( h, fmt->tymed, med );
    }

    if (fmt->cfFormat == CF_BITMAP || fmt->cfFormat == CF_DIB || fmt->cfFormat == CF_DIBV5)
    {
        __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference *ref;
        HBITMAP hbm = NULL;
        HRESULT hr;

        ref = package_get_com( impl->package, format_bitmap );
        if (!ref) return DV_E_FORMATETC;

        if (fmt->cfFormat == CF_BITMAP)
        {
            if (!(fmt->tymed & TYMED_GDI))
            {
                __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_Release( ref );
                return DV_E_TYMED;
            }
            hr = stream_ref_to_hbitmap( ref, &hbm );
            __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_Release( ref );
            if (FAILED( hr )) return hr;
            med->tymed = TYMED_GDI;
            med->hBitmap = hbm;
            med->pUnkForRelease = NULL;
            return S_OK;
        }
        else
        {
            HGLOBAL h = NULL;
            BYTE *bytes = NULL;
            SIZE_T size = 0;

            if (!(fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)))
            {
                __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_Release( ref );
                return DV_E_TYMED;
            }

            /* The Bitmap value is a BMP stream; CF_DIB is its byte stream
             * without the 14-byte file header, so hand the pixel payload out
             * directly.  Other encodings (PNG, JPEG) round-trip via a DIB. */
            hr = stream_ref_to_bytes( ref, &bytes, &size );
            if (SUCCEEDED( hr ) && bytes && size >= sizeof(BITMAPFILEHEADER) &&
                (bytes[0] == 'B' && bytes[1] == 'M'))
            {
                hr = alloc_hglobal_copy( bytes + sizeof(BITMAPFILEHEADER),
                                         size - sizeof(BITMAPFILEHEADER), &h );
                free( bytes );
                __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_Release( ref );
                if (FAILED( hr )) return hr;
                return set_hglobal_medium( h, fmt->tymed, med );
            }
            free( bytes );

            hr = stream_ref_to_hbitmap( ref, &hbm );
            __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference_Release( ref );
            if (FAILED( hr )) return hr;
            hr = hbitmap_to_dib_global( hbm, &h );
            DeleteObject( hbm );
            if (FAILED( hr )) return hr;
            return set_hglobal_medium( h, fmt->tymed, med );
        }
    }

    if (fmt->cfFormat == CF_HDROP)
    {
        struct winrt_vector *items = package_get_items( impl->package );
        HGLOBAL h;
        HRESULT hr;

        if (!items) return DV_E_FORMATETC;
        if (!(fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)))
        {
            vector_release( items );
            return DV_E_TYMED;
        }
        hr = items_to_hdrop_global( items, &h );
        vector_release( items );
        if (FAILED( hr )) return hr;
        return set_hglobal_medium( h, fmt->tymed, med );
    }

    if (fmt->cfFormat == clipboard_preferred_drop_effect_format())
    {
        DataPackageOperation op = package_get_operation( impl->package );
        DWORD eff = DROPEFFECT_NONE;
        HGLOBAL h;
        HRESULT hr;

        if (!(fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM))) return DV_E_TYMED;
        if (op & DataPackageOperation_Copy) eff |= DROPEFFECT_COPY;
        if (op & DataPackageOperation_Move) eff |= DROPEFFECT_MOVE;
        if (op & DataPackageOperation_Link) eff |= DROPEFFECT_LINK;
        hr = alloc_hglobal_copy( &eff, sizeof(eff), &h );
        if (FAILED( hr )) return hr;
        return set_hglobal_medium( h, fmt->tymed, med );
    }

    {
        WCHAR name[256];
        if (fmt->cfFormat >= 0xC000 && GetClipboardFormatNameW( fmt->cfFormat, name, ARRAY_SIZE( name ) ))
        {
            if (!(fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM))) return DV_E_TYMED;
            package_lock( impl->package );
            package_get_raw( impl->package, name, &kind );
            package_unlock( impl->package );
            if (kind == FMT_STRING)
            {
                HSTRING hstr = package_get_string( impl->package, name );
                const WCHAR *raw = WindowsGetStringRawBuffer( hstr, NULL );
                UINT32 len = WindowsGetStringLen( hstr );
                HGLOBAL h = NULL;
                HRESULT hr = alloc_hglobal_copy( raw ? raw : L"", (len + 1) * sizeof(WCHAR), &h );
                WindowsDeleteString( hstr );
                if (FAILED( hr )) return hr;
                return set_hglobal_medium( h, fmt->tymed, med );
            }
        }
    }

    return DV_E_FORMATETC;
}

static HRESULT WINAPI view_do_GetDataHere( IDataObject *iface, FORMATETC *fmt, STGMEDIUM *med )
{
    struct data_package_view *impl = impl_from_any( iface );
    if (impl->data_obj) return IDataObject_GetDataHere( impl->data_obj, fmt, med );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_do_QueryGetData( IDataObject *iface, FORMATETC *fmt )
{
    struct data_package_view *impl = impl_from_any( iface );
    enum fmt_kind kind;

    if (!fmt) return E_INVALIDARG;
    if (impl->data_obj) return IDataObject_QueryGetData( impl->data_obj, fmt );
    if (!impl->package) return DV_E_CLIPFORMAT;
    if (fmt->dwAspect != DVASPECT_CONTENT || fmt->lindex != -1) return DV_E_FORMATETC;

    if (fmt->cfFormat == CF_UNICODETEXT || fmt->cfFormat == CF_TEXT || fmt->cfFormat == CF_OEMTEXT)
    {
        package_lock( impl->package );
        package_get_raw( impl->package, format_text, &kind );
        package_unlock( impl->package );
        if (kind != FMT_STRING) return DV_E_CLIPFORMAT;
        return (fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)) ? S_OK : DV_E_TYMED;
    }
    if (fmt->cfFormat == clipboard_html_format())
    {
        package_lock( impl->package );
        package_get_raw( impl->package, format_html, &kind );
        package_unlock( impl->package );
        if (kind != FMT_STRING) return DV_E_CLIPFORMAT;
        return (fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)) ? S_OK : DV_E_TYMED;
    }
    if (fmt->cfFormat == clipboard_rtf_format())
    {
        package_lock( impl->package );
        package_get_raw( impl->package, format_rtf, &kind );
        package_unlock( impl->package );
        if (kind != FMT_STRING) return DV_E_CLIPFORMAT;
        return (fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)) ? S_OK : DV_E_TYMED;
    }
    if (fmt->cfFormat == CF_BITMAP)
    {
        package_lock( impl->package );
        package_get_raw( impl->package, format_bitmap, &kind );
        package_unlock( impl->package );
        if (kind != FMT_COM) return DV_E_CLIPFORMAT;
        return (fmt->tymed & TYMED_GDI) ? S_OK : DV_E_TYMED;
    }
    if (fmt->cfFormat == CF_DIB || fmt->cfFormat == CF_DIBV5)
    {
        package_lock( impl->package );
        package_get_raw( impl->package, format_bitmap, &kind );
        package_unlock( impl->package );
        if (kind != FMT_COM) return DV_E_CLIPFORMAT;
        return (fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)) ? S_OK : DV_E_TYMED;
    }
    if (fmt->cfFormat == CF_HDROP)
    {
        package_lock( impl->package );
        package_get_raw( impl->package, format_storage_items, &kind );
        package_unlock( impl->package );
        if (kind != FMT_ITEMS) return DV_E_CLIPFORMAT;
        return (fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)) ? S_OK : DV_E_TYMED;
    }
    if (fmt->cfFormat == clipboard_preferred_drop_effect_format())
    {
        return (fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)) ? S_OK : DV_E_TYMED;
    }
    if (package_find_custom_key_for_cf( impl->package, fmt->cfFormat ))
    {
        return (fmt->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM)) ? S_OK : DV_E_TYMED;
    }
    return DV_E_CLIPFORMAT;
}

static HRESULT WINAPI view_do_GetCanonicalFormatEtc( IDataObject *iface, FORMATETC *fmt_in, FORMATETC *fmt_out )
{
    struct data_package_view *impl = impl_from_any( iface );
    if (impl->data_obj) return IDataObject_GetCanonicalFormatEtc( impl->data_obj, fmt_in, fmt_out );
    if (!fmt_in || !fmt_out) return E_INVALIDARG;
    *fmt_out = *fmt_in;
    fmt_out->ptd = NULL;
    return DATA_S_SAMEFORMATETC;
}

static HRESULT WINAPI view_do_SetData( IDataObject *iface, FORMATETC *fmt, STGMEDIUM *med, BOOL release )
{
    struct data_package_view *impl = impl_from_any( iface );
    if (impl->data_obj) return IDataObject_SetData( impl->data_obj, fmt, med, release );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_do_EnumFormatEtc( IDataObject *iface, DWORD dir, IEnumFORMATETC **enum_fmt )
{
    struct data_package_view *impl = impl_from_any( iface );
    struct pkg_enum_fmtetc *e;
    ULONG max_fmts = 8, count, i;

    if (!enum_fmt) return E_POINTER;
    *enum_fmt = NULL;
    if (dir != DATADIR_GET) return E_NOTIMPL;
    if (impl->data_obj) return IDataObject_EnumFormatEtc( impl->data_obj, dir, enum_fmt );

    if (!impl->package)
    {
        if (!(e = calloc( 1, sizeof(*e) ))) return E_OUTOFMEMORY;
        e->IEnumFORMATETC_iface.lpVtbl = &pkg_enum_vtbl;
        e->ref = 1;
        *enum_fmt = &e->IEnumFORMATETC_iface;
        return S_OK;
    }

    count = package_format_count( impl->package );
    max_fmts += count * 3;
    if (!(e = calloc( 1, offsetof( struct pkg_enum_fmtetc, fmts[max_fmts] ) ))) return E_OUTOFMEMORY;
    e->IEnumFORMATETC_iface.lpVtbl = &pkg_enum_vtbl;
    e->ref = 1;

    package_lock( impl->package );
    count = package_format_count( impl->package );
    for (i = 0; i < count; i++)
    {
        const WCHAR *key = package_format_key( impl->package, i );
        enum fmt_kind kind = FMT_NONE;

        if (!key) continue;
        package_get_raw( impl->package, key, &kind );
        if (!wcscmp( key, format_text ) && kind == FMT_STRING)
        {
            add_fmt( e, CF_UNICODETEXT, TYMED_HGLOBAL | TYMED_ISTREAM );
            add_fmt( e, CF_TEXT, TYMED_HGLOBAL | TYMED_ISTREAM );
            add_fmt( e, CF_OEMTEXT, TYMED_HGLOBAL | TYMED_ISTREAM );
        }
        else if (!wcscmp( key, format_html ) && kind == FMT_STRING)
        {
            add_fmt( e, (CLIPFORMAT)clipboard_html_format(), TYMED_HGLOBAL | TYMED_ISTREAM );
        }
        else if (!wcscmp( key, format_rtf ) && kind == FMT_STRING)
        {
            add_fmt( e, (CLIPFORMAT)clipboard_rtf_format(), TYMED_HGLOBAL | TYMED_ISTREAM );
        }
        else if (!wcscmp( key, format_bitmap ) && kind == FMT_COM)
        {
            add_fmt( e, CF_BITMAP, TYMED_GDI );
            add_fmt( e, CF_DIB, TYMED_HGLOBAL | TYMED_ISTREAM );
            add_fmt( e, CF_DIBV5, TYMED_HGLOBAL | TYMED_ISTREAM );
        }
        else if (!wcscmp( key, format_storage_items ) && kind == FMT_ITEMS)
        {
            add_fmt( e, CF_HDROP, TYMED_HGLOBAL | TYMED_ISTREAM );
        }
        else if (!is_builtin_format_name( key ) && kind == FMT_STRING)
        {
            UINT cf = RegisterClipboardFormatW( key );
            if (cf) add_fmt( e, (CLIPFORMAT)cf, TYMED_HGLOBAL | TYMED_ISTREAM );
        }
    }
    package_unlock( impl->package );

    if (package_get_operation( impl->package ) != DataPackageOperation_None && e->count > 0)
        add_fmt( e, (CLIPFORMAT)clipboard_preferred_drop_effect_format(), TYMED_HGLOBAL | TYMED_ISTREAM );

    *enum_fmt = &e->IEnumFORMATETC_iface;
    return S_OK;
}

static HRESULT WINAPI view_do_DAdvise( IDataObject *iface, FORMATETC *fmt, DWORD flags, IAdviseSink *sink, DWORD *conn )
{
    struct data_package_view *impl = impl_from_any( iface );
    if (impl->data_obj) return IDataObject_DAdvise( impl->data_obj, fmt, flags, sink, conn );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_do_DUnadvise( IDataObject *iface, DWORD conn )
{
    struct data_package_view *impl = impl_from_any( iface );
    if (impl->data_obj) return IDataObject_DUnadvise( impl->data_obj, conn );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_do_EnumDAdvise( IDataObject *iface, IEnumSTATDATA **enum_advise )
{
    struct data_package_view *impl = impl_from_any( iface );
    if (impl->data_obj) return IDataObject_EnumDAdvise( impl->data_obj, enum_advise );
    return E_NOTIMPL;
}

static const IDataObjectVtbl data_package_view_data_object_vtbl =
{
    view_do_QueryInterface,
    view_do_AddRef,
    view_do_Release,
    view_do_GetData,
    view_do_GetDataHere,
    view_do_QueryGetData,
    view_do_GetCanonicalFormatEtc,
    view_do_SetData,
    view_do_EnumFormatEtc,
    view_do_DAdvise,
    view_do_DUnadvise,
    view_do_EnumDAdvise,
};

/* ------------------------------------------------ IDataPackageView ----- */

static HRESULT WINAPI view_QueryInterface( const void *iface, REFIID iid, void **out )
{
    struct data_package_view *impl = impl_from_any( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDataPackageView ))
    {
        *out = (void *)impl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataPackageView2 ))
    {
        *out = (void *)&impl->view2_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataPackageView3 ))
    {
        *out = (void *)&impl->view3_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataPackageView4 ))
    {
        *out = (void *)&impl->view4_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDataObject ))
    {
        *out = (void *)&impl->data_object_vtbl;
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IMarshal ))
    {
        if (!impl->ftm)
        {
            IUnknown *ftm = NULL;
            if (SUCCEEDED( CoCreateFreeThreadedMarshaler( (IUnknown *)impl, &ftm ) ) && ftm)
            {
                if (InterlockedCompareExchangePointer( (void **)&impl->ftm, ftm, NULL ) != NULL)
                    IUnknown_Release( ftm );
            }
        }
        if (impl->ftm) return IUnknown_QueryInterface( impl->ftm, iid, out );
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI view_AddRef( const void *iface )
{
    struct data_package_view *impl = impl_from_any( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI view_Release( const void *iface )
{
    struct data_package_view *impl = impl_from_any( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->ftm) IUnknown_Release( impl->ftm );
        if (impl->data_obj) IDataObject_Release( impl->data_obj );
        if (impl->package) IInspectable_Release( (IInspectable *)impl->package );
        WindowsDeleteString( impl->accepted_format );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI view_GetIids( const void *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_GetRuntimeClassName( const void *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackageView,
                                lstrlenW( RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackageView ),
                                class_name );
}

static HRESULT WINAPI view_GetTrustLevel( const void *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI view_get_Properties( const void *iface, void **value )
{
    struct data_package_view *impl = impl_from_any( iface );
    IInspectable *props;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (!(props = create_property_set_view( package_get_props( impl->package ) ))) return E_OUTOFMEMORY;
    *value = props;
    return S_OK;
}

static HRESULT WINAPI view_get_RequestedOperation( const void *iface, DataPackageOperation *value )
{
    struct data_package_view *impl = impl_from_any( iface );

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    if (impl->data_obj)
    {
        BYTE *bytes = NULL;
        SIZE_T sz = 0;

        if (SUCCEEDED( data_obj_read_cf_bytes( impl->data_obj, clipboard_preferred_drop_effect_format(), &bytes, &sz ) ) &&
            bytes && sz >= sizeof(DWORD))
        {
            DWORD eff = *(const DWORD *)bytes;
            DataPackageOperation op = DataPackageOperation_None;
            if (eff & DROPEFFECT_COPY) op |= DataPackageOperation_Copy;
            if (eff & DROPEFFECT_MOVE) op |= DataPackageOperation_Move;
            if (eff & DROPEFFECT_LINK) op |= DataPackageOperation_Link;
            *value = op;
            free( bytes );
            return S_OK;
        }
        free( bytes );
        *value = (data_obj_has_text( impl->data_obj ) || data_obj_has_bitmap( impl->data_obj ) ||
                  data_obj_has_cf( impl->data_obj, clipboard_html_format(), TYMED_HGLOBAL | TYMED_ISTREAM ))
                 ? DataPackageOperation_Copy : DataPackageOperation_None;
        return S_OK;
    }

    *value = package_get_operation( impl->package );
    return S_OK;
}

static HRESULT WINAPI view_ReportOperationCompleted( const void *iface, DataPackageOperation value )
{
    TRACE( "iface %p, value %#x.\n", iface, value );
    return S_OK;
}

static HRESULT WINAPI view_get_AvailableFormats( const void *iface, void **formats )
{
    struct data_package_view *impl = impl_from_any( iface );
    struct winrt_vector *vector;
    ULONG i, count;

    TRACE( "iface %p, formats %p.\n", iface, formats );

    if (!formats) return E_POINTER;

    vector = create_vector( NULL, &IID___FIVectorView_1_HSTRING, &IID___FIIterable_1_HSTRING,
                            &IID___FIIterator_1_HSTRING, ELEM_STRING,
                            L"Windows.Foundation.Collections.IVectorView`1<HSTRING>" );
    if (!vector) return E_OUTOFMEMORY;

    if (impl->data_obj)
    {
        IEnumFORMATETC *enum_fmt = NULL;
        if (SUCCEEDED( IDataObject_EnumFormatEtc( impl->data_obj, DATADIR_GET, &enum_fmt ) ) && enum_fmt)
        {
            FORMATETC fmt;
            while (IEnumFORMATETC_Next( enum_fmt, 1, &fmt, NULL ) == S_OK)
            {
                if (fmt.ptd) CoTaskMemFree( fmt.ptd );
                if (fmt.cfFormat == CF_UNICODETEXT || fmt.cfFormat == CF_TEXT || fmt.cfFormat == CF_OEMTEXT)
                    vector_append_unique_str( vector, format_text );
                else if (fmt.cfFormat == clipboard_html_format())
                    vector_append_unique_str( vector, format_html );
                else if (fmt.cfFormat == clipboard_rtf_format())
                    vector_append_unique_str( vector, format_rtf );
                else if (fmt.cfFormat == CF_BITMAP || fmt.cfFormat == CF_DIB || fmt.cfFormat == CF_DIBV5)
                    vector_append_unique_str( vector, format_bitmap );
                else if (fmt.cfFormat == CF_HDROP)
                    vector_append_unique_str( vector, format_storage_items );
                else if (fmt.cfFormat >= 0xC000)
                {
                    WCHAR name[256];
                    if (GetClipboardFormatNameW( fmt.cfFormat, name, ARRAY_SIZE( name ) ) &&
                        !is_internal_ole_format_name( name ))
                    {
                        vector_append_unique_str( vector, name );
                    }
                }
            }
            IEnumFORMATETC_Release( enum_fmt );
        }
        else
        {
            if (data_obj_has_text( impl->data_obj )) vector_append_unique_str( vector, format_text );
            if (data_obj_has_cf( impl->data_obj, clipboard_html_format(), TYMED_HGLOBAL | TYMED_ISTREAM ))
                vector_append_unique_str( vector, format_html );
            if (data_obj_has_cf( impl->data_obj, clipboard_rtf_format(), TYMED_HGLOBAL | TYMED_ISTREAM ))
                vector_append_unique_str( vector, format_rtf );
            if (data_obj_has_bitmap( impl->data_obj )) vector_append_unique_str( vector, format_bitmap );
            if (data_obj_has_storage_items( impl->data_obj ))
                vector_append_unique_str( vector, format_storage_items );
        }

        *formats = vector_view_iface( vector );
        return S_OK;
    }

    if (impl->package)
    {
        package_lock( impl->package );
        count = package_format_count( impl->package );
        for (i = 0; i < count; i++)
        {
            const WCHAR *key = package_format_key( impl->package, i );
            if (key) vector_append_unique_str( vector, key );
        }
        package_unlock( impl->package );
    }

    *formats = vector_view_iface( vector );
    return S_OK;
}

static HRESULT WINAPI view_Contains( const void *iface, HSTRING format_id, BOOLEAN *value )
{
    struct data_package_view *impl = impl_from_any( iface );
    const WCHAR *key = WindowsGetStringRawBuffer( format_id, NULL );
    enum fmt_kind kind;

    TRACE( "iface %p, format_id %s, value %p.\n", iface, debugstr_hstring( format_id ), value );

    if (!value) return E_POINTER;
    *value = FALSE;
    if (!key) return S_OK;

    if (impl->data_obj)
    {
        if (!wcscmp( key, format_text )) *value = data_obj_has_text( impl->data_obj );
        else if (!wcscmp( key, format_html ))
            *value = data_obj_has_cf( impl->data_obj, clipboard_html_format(), TYMED_HGLOBAL | TYMED_ISTREAM );
        else if (!wcscmp( key, format_rtf ))
            *value = data_obj_has_cf( impl->data_obj, clipboard_rtf_format(), TYMED_HGLOBAL | TYMED_ISTREAM );
        else if (!wcscmp( key, format_bitmap )) *value = data_obj_has_bitmap( impl->data_obj );
        else if (!wcscmp( key, format_storage_items ))
            *value = data_obj_has_cf( impl->data_obj, CF_HDROP, TYMED_HGLOBAL | TYMED_ISTREAM );
        else
        {
            UINT cf = RegisterClipboardFormatW( key );
            *value = data_obj_has_cf( impl->data_obj, cf, TYMED_HGLOBAL | TYMED_ISTREAM );
        }
        return S_OK;
    }

    if (!impl->package) return S_OK;
    package_lock( impl->package );
    package_get_raw( impl->package, key, &kind );
    package_unlock( impl->package );
    *value = kind != FMT_NONE;
    return S_OK;
}

static HRESULT WINAPI view_GetDataAsync( const void *iface, HSTRING format_id, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    const WCHAR *key = WindowsGetStringRawBuffer( format_id, NULL );
    void *value = NULL;

    TRACE( "iface %p, format_id %s, operation %p.\n", iface, debugstr_hstring( format_id ), operation );

    if (!operation) return E_POINTER;

    if (impl->data_obj && key)
    {
        if (!wcscmp( key, format_bitmap ))
        {
            __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference *ref = NULL;
            data_obj_read_bitmap( impl->data_obj, &ref );
            return view_com_op( &IID___FIAsyncOperation_1_IInspectable,
                                L"Windows.Foundation.IAsyncOperation`1<IInspectable>", ref, operation );
        }
        if (!wcscmp( key, format_text ) || !wcscmp( key, format_html ) || !wcscmp( key, format_rtf ))
        {
            HSTRING hstr = NULL;
            if (!wcscmp( key, format_text )) data_obj_read_text( impl->data_obj, &hstr );
            else if (!wcscmp( key, format_html )) data_obj_read_html( impl->data_obj, &hstr );
            else data_obj_read_rtf( impl->data_obj, &hstr );

            if (hstr)
            {
                static const WCHAR pv_cls[] = L"Windows.Foundation.PropertyValue";
                IPropertyValueStatics *pvs = NULL;
                HSTRING cls_str = NULL;
                if (SUCCEEDED( WindowsCreateString( pv_cls, ARRAY_SIZE( pv_cls ) - 1, &cls_str ) ))
                {
                    if (SUCCEEDED( RoGetActivationFactory( cls_str, &IID_IPropertyValueStatics, (void **)&pvs ) ))
                    {
                        IPropertyValueStatics_CreateString( pvs, hstr, (IInspectable **)&value );
                        IPropertyValueStatics_Release( pvs );
                    }
                    WindowsDeleteString( cls_str );
                }
                WindowsDeleteString( hstr );
            }
            return view_com_op( &IID___FIAsyncOperation_1_IInspectable,
                                L"Windows.Foundation.IAsyncOperation`1<IInspectable>", value, operation );
        }
    }

    value = key && impl->package ? package_get_com( impl->package, key ) : NULL;
    return view_com_op( &IID___FIAsyncOperation_1_IInspectable,
                        L"Windows.Foundation.IAsyncOperation`1<IInspectable>", value, operation );
}

static HRESULT WINAPI view_GetTextAsync( const void *iface, IAsyncOperation_HSTRING **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return view_string_op( impl_from_any( iface ), format_text, operation );
}

static HRESULT WINAPI view_GetCustomTextAsync( const void *iface, HSTRING format_id,
                                               IAsyncOperation_HSTRING **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    const WCHAR *key = WindowsGetStringRawBuffer( format_id, NULL );
    struct async_op *op;
    HSTRING value = NULL;

    TRACE( "iface %p, format_id %s, operation %p.\n", iface, debugstr_hstring( format_id ), operation );

    if (!operation) return E_POINTER;

    if (impl->data_obj && key)
    {
        if (!wcscmp( key, format_text )) return view_string_op( impl, format_text, operation );
        if (!wcscmp( key, format_html )) return view_string_op( impl, format_html, operation );
        if (!wcscmp( key, format_rtf )) return view_string_op( impl, format_rtf, operation );

        {
            BYTE *bytes = NULL;
            SIZE_T size = 0;
            UINT cf = RegisterClipboardFormatW( key );
            if (SUCCEEDED( data_obj_read_cf_bytes( impl->data_obj, cf, &bytes, &size ) ) && bytes && size)
            {
                const WCHAR *w = (const WCHAR *)bytes;
                SIZE_T max_w = size / sizeof(WCHAR), wlen = 0;
                while (wlen < max_w && w[wlen]) wlen++;
                WindowsCreateString( w, (UINT32)wlen, &value );
            }
            free( bytes );
        }
    }
    else if (key && impl->package)
    {
        value = package_get_string( impl->package, key );
    }

    if (!value && FAILED( WindowsCreateString( L"", 0, &value ) )) return E_OUTOFMEMORY;

    op = create_async_op( &IID___FIAsyncOperation_1_HSTRING, L"Windows.Foundation.IAsyncOperation`1<HSTRING>", value, OP_RESULT_STRING );
    WindowsDeleteString( value );
    if (!op) return E_OUTOFMEMORY;
    *operation = (IAsyncOperation_HSTRING *)async_op_iface( op );
    return S_OK;
}

static HRESULT WINAPI view_GetUriAsync( const void *iface, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    void *uri;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    uri = impl->package ? package_get_com( impl->package, format_uri ) : NULL;
    return view_com_op( &IID___FIAsyncOperation_1_Windows__CFoundation__CUri,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Uri>", uri, operation );
}

static HRESULT WINAPI view_GetHtmlFormatAsync( const void *iface, IAsyncOperation_HSTRING **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return view_string_op( impl_from_any( iface ), format_html, operation );
}

static HRESULT WINAPI view_GetResourceMapAsync( const void *iface, void **operation )
{
    struct winrt_map *map;
    void *result;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    map = create_map( NULL,
                      &IID___FIMapView_2_HSTRING_Windows__CStorage__CStreams__CRandomAccessStreamReference,
                      NULL, NULL, NULL, ELEM_COM,
                      L"Windows.Foundation.Collections.IMapView`2<HSTRING, Windows.Storage.Streams.RandomAccessStreamReference>" );
    if (!map) return E_OUTOFMEMORY;
    result = map_view_iface( map );
    return view_com_op( &IID___FIAsyncOperation_1___FIMapView_2_HSTRING_Windows__CStorage__CStreams__CRandomAccessStreamReference,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IMapView`2<HSTRING, Windows.Storage.Streams.RandomAccessStreamReference>>",
                        result, operation );
}

static HRESULT WINAPI view_GetRtfAsync( const void *iface, IAsyncOperation_HSTRING **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return view_string_op( impl_from_any( iface ), format_rtf, operation );
}

static HRESULT WINAPI view_GetBitmapAsync( const void *iface, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    void *bitmap = NULL;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    if (impl->data_obj)
    {
        __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference *ref = NULL;
        if (SUCCEEDED( data_obj_read_bitmap( impl->data_obj, &ref ) ))
            bitmap = ref;
    }
    else if (impl->package)
    {
        bitmap = package_get_com( impl->package, format_bitmap );
    }

    return view_com_op( &IID___FIAsyncOperation_1_Windows__CStorage__CStreams__CRandomAccessStreamReference,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.RandomAccessStreamReference>",
                        bitmap, operation );
}

static HRESULT WINAPI view_GetStorageItemsAsync( const void *iface, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    struct winrt_vector *items;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    if (impl->data_obj)
    {
        items = create_vector( NULL, &IID___FIVectorView_1_Windows__CStorage__CIStorageItem,
                               &IID___FIIterable_1_Windows__CStorage__CIStorageItem,
                               &IID___FIIterator_1_Windows__CStorage__CIStorageItem, ELEM_COM,
                               L"Windows.Foundation.Collections.IVectorView`1<Windows.Storage.IStorageItem>" );
        if (!items) return E_OUTOFMEMORY;
        data_obj_read_storage_items( impl->data_obj, items );
        return view_com_op( &IID___FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CIStorageItem,
                            L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Storage.IStorageItem>>",
                            vector_view_iface( items ), operation );
    }

    items = package_get_items( impl->package );
    if (!items)
    {
        items = create_vector( NULL, &IID___FIVectorView_1_Windows__CStorage__CIStorageItem,
                               &IID___FIIterable_1_Windows__CStorage__CIStorageItem,
                               &IID___FIIterator_1_Windows__CStorage__CIStorageItem, ELEM_COM,
                               L"Windows.Foundation.Collections.IVectorView`1<Windows.Storage.IStorageItem>" );
        if (!items) return E_OUTOFMEMORY;
    }

    return view_com_op( &IID___FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CIStorageItem,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Storage.IStorageItem>>",
                        vector_view_iface( items ), operation );
}

static const void *data_package_view_vtbl[] =
{
    view_QueryInterface,
    view_AddRef,
    view_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view_get_Properties,
    view_get_RequestedOperation,
    view_ReportOperationCompleted,
    view_get_AvailableFormats,
    view_Contains,
    view_GetDataAsync,
    view_GetTextAsync,
    view_GetCustomTextAsync,
    view_GetUriAsync,
    view_GetHtmlFormatAsync,
    view_GetResourceMapAsync,
    view_GetRtfAsync,
    view_GetBitmapAsync,
    view_GetStorageItemsAsync,
};

/* ------------------------------------------------ IDataPackageView2 ---- */

static HRESULT WINAPI view2_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return view_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI view2_AddRef( const void *iface )
{
    return view_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI view2_Release( const void *iface )
{
    return view_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI view2_GetApplicationLinkAsync( const void *iface, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    void *uri;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    uri = impl->package ? package_get_com( impl->package, format_application_link ) : NULL;
    return view_com_op( &IID___FIAsyncOperation_1_Windows__CFoundation__CUri,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Uri>", uri, operation );
}

static HRESULT WINAPI view2_GetWebLinkAsync( const void *iface, void **operation )
{
    struct data_package_view *impl = impl_from_any( iface );
    void *uri;

    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;

    uri = impl->package ? package_get_com( impl->package, format_web_link ) : NULL;
    return view_com_op( &IID___FIAsyncOperation_1_Windows__CFoundation__CUri,
                        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Uri>", uri, operation );
}

static const void *data_package_view2_vtbl[] =
{
    view2_QueryInterface,
    view2_AddRef,
    view2_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view2_GetApplicationLinkAsync,
    view2_GetWebLinkAsync,
};

/* ------------------------------------------------ IDataPackageView3 ---- */

static HRESULT WINAPI view3_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return view_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI view3_AddRef( const void *iface )
{
    return view_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI view3_Release( const void *iface )
{
    return view_Release( (void *)impl_from_any( iface ) );
}

static HRESULT view_access_op( void **operation )
{
    struct async_op *op;

    op = create_async_op( NULL,
                          L"Windows.Foundation.IAsyncOperation`1<Windows.Security.EnterpriseData.ProtectionPolicyEvaluationResult>",
                          (void *)(UINT_PTR)ProtectionPolicyEvaluationResult_Allowed, OP_RESULT_VALUE32 );
    if (!op) return E_OUTOFMEMORY;
    *operation = async_op_iface( op );
    return S_OK;
}

static HRESULT WINAPI view3_RequestAccessAsync( const void *iface, void **operation )
{
    TRACE( "iface %p, operation %p.\n", iface, operation );

    if (!operation) return E_POINTER;
    return view_access_op( operation );
}

static HRESULT WINAPI view3_RequestAccessWithEnterpriseIdAsync( const void *iface, HSTRING enterprise_id,
                                                                void **operation )
{
    TRACE( "iface %p, enterprise_id %s, operation %p.\n", iface, debugstr_hstring( enterprise_id ), operation );

    if (!operation) return E_POINTER;
    return view_access_op( operation );
}

static HRESULT WINAPI view3_UnlockAndAssumeEnterpriseIdentity( const void *iface, void *result )
{
    TRACE( "iface %p, result %p.\n", iface, result );

    if (!result) return E_POINTER;
    *(int *)result = ProtectionPolicyEvaluationResult_Allowed;
    return S_OK;
}

static const void *data_package_view3_vtbl[] =
{
    view3_QueryInterface,
    view3_AddRef,
    view3_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view3_RequestAccessAsync,
    view3_RequestAccessWithEnterpriseIdAsync,
    view3_UnlockAndAssumeEnterpriseIdentity,
};

/* ------------------------------------------------ IDataPackageView4 ---- */

static HRESULT WINAPI view4_QueryInterface( const void *iface, REFIID iid, void **out )
{
    return view_QueryInterface( (void *)impl_from_any( iface ), iid, out );
}

static ULONG WINAPI view4_AddRef( const void *iface )
{
    return view_AddRef( (void *)impl_from_any( iface ) );
}

static ULONG WINAPI view4_Release( const void *iface )
{
    return view_Release( (void *)impl_from_any( iface ) );
}

static HRESULT WINAPI view4_SetAcceptedFormatId( const void *iface, HSTRING format_id )
{
    struct data_package_view *impl = impl_from_any( iface );
    HSTRING dup = NULL;

    TRACE( "iface %p, format_id %s.\n", iface, debugstr_hstring( format_id ) );

    if (format_id) WindowsDuplicateString( format_id, &dup );
    WindowsDeleteString( impl->accepted_format );
    impl->accepted_format = dup;
    return S_OK;
}

static const void *data_package_view4_vtbl[] =
{
    view4_QueryInterface,
    view4_AddRef,
    view4_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view4_SetAcceptedFormatId,
};

/* ------------------------------------------------------------ create -- */

static struct data_package_view *alloc_view( void )
{
    struct data_package_view *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->view_vtbl = data_package_view_vtbl;
    impl->view2_vtbl = data_package_view2_vtbl;
    impl->view3_vtbl = data_package_view3_vtbl;
    impl->view4_vtbl = data_package_view4_vtbl;
    impl->data_object_vtbl = &data_package_view_data_object_vtbl;
    impl->ref = 1;
    return impl;
}

struct data_package_view *create_package_view_object( struct data_package *package )
{
    struct data_package_view *impl = alloc_view();

    if (!impl) return NULL;
    impl->package = package;
    if (package) IInspectable_AddRef( (IInspectable *)package );

    TRACE( "created package view %p over package %p.\n", impl, package );
    return impl;
}

struct data_package_view *create_package_view_from_data_object( IDataObject *data_obj )
{
    struct data_package_view *impl = alloc_view();

    if (!impl) return NULL;
    impl->data_obj = data_obj;
    if (data_obj) IDataObject_AddRef( data_obj );

    TRACE( "created package view %p over IDataObject %p.\n", impl, data_obj );
    return impl;
}

IInspectable *create_data_package_view( struct data_package *package )
{
    return (IInspectable *)create_package_view_object( package );
}
