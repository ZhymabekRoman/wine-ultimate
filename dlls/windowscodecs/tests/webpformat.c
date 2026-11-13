/*
 * WebP decoder test
 *
 * Copyright 2026 Wine Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS

#include "objbase.h"
#include "wincodec.h"
#include "wine/test.h"

static const unsigned char webp_test_data[] = {
    0x52, 0x49, 0x46, 0x46, 0x3c, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50,
    0x56, 0x50, 0x38, 0x20, 0x30, 0x00, 0x00, 0x00, 0xd0, 0x01, 0x00, 0x9d,
    0x01, 0x2a, 0x02, 0x00, 0x02, 0x00, 0x01, 0x40, 0x26, 0x25, 0xa0, 0x02,
    0x74, 0xba, 0x01, 0xf8, 0x00, 0x03, 0xb0, 0x00, 0xfe, 0xf2, 0xeb, 0x7f,
    0xfc, 0xd8, 0x15, 0xcd, 0x73, 0xef, 0xf7, 0xff, 0xd2, 0xe0, 0xfd, 0x2e,
    0x0f, 0xd2, 0xe0, 0xff, 0xd2, 0x90, 0x00, 0x00
};

static void test_webp_decoder(void)
{
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICImagingFactory *factory = NULL;
    IStream *stream = NULL;
    HGLOBAL hdata;
    void *ptr;
    GUID format;
    UINT count = 0, width = 0, height = 0;
    BYTE buffer[2 * 2 * 4] = {0};
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_WICWebpDecoder, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICBitmapDecoder, (void **)&decoder);
    trace("CoCreateInstance(CLSID_WICWebpDecoder) returned hr %#lx\n", hr);
    if (FAILED(hr))
    {
        win_skip("WebpDecoder is not available, skipping test\n");
        return;
    }
    ok(hr == S_OK, "CoCreateInstance(CLSID_WICWebpDecoder) failed, hr %#lx\n", hr);

    hdata = GlobalAlloc(GMEM_MOVEABLE, sizeof(webp_test_data));
    ok(hdata != NULL, "GlobalAlloc failed\n");
    ptr = GlobalLock(hdata);
    memcpy(ptr, webp_test_data, sizeof(webp_test_data));
    GlobalUnlock(hdata);

    hr = CreateStreamOnHGlobal(hdata, TRUE, &stream);
    ok(hr == S_OK, "CreateStreamOnHGlobal failed, hr %#lx\n", hr);

    hr = IWICBitmapDecoder_Initialize(decoder, stream, WICDecodeMetadataCacheOnLoad);
    ok(hr == S_OK, "Initialize failed, hr %#lx\n", hr);

    hr = IWICBitmapDecoder_GetContainerFormat(decoder, &format);
    ok(hr == S_OK, "GetContainerFormat failed, hr %#lx\n", hr);
    ok(IsEqualGUID(&format, &GUID_ContainerFormatWebp), "got unexpected container format %s\n",
       wine_dbgstr_guid(&format));

    hr = IWICBitmapDecoder_GetFrameCount(decoder, &count);
    ok(hr == S_OK, "GetFrameCount failed, hr %#lx\n", hr);
    ok(count == 1, "got frame count %u\n", count);

    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    ok(hr == S_OK, "GetFrame failed, hr %#lx\n", hr);

    hr = IWICBitmapFrameDecode_GetSize(frame, &width, &height);
    ok(hr == S_OK, "GetSize failed, hr %#lx\n", hr);
    ok(width == 2 && height == 2, "got width %u, height %u\n", width, height);

    hr = IWICBitmapFrameDecode_GetPixelFormat(frame, &format);
    ok(hr == S_OK, "GetPixelFormat failed, hr %#lx\n", hr);
    ok(IsEqualGUID(&format, &GUID_WICPixelFormat32bppBGRA), "got unexpected pixel format %s\n",
       wine_dbgstr_guid(&format));

    hr = IWICBitmapFrameDecode_CopyPixels(frame, NULL, 2 * 4, sizeof(buffer), buffer);
    ok(hr == S_OK, "CopyPixels failed, hr %#lx\n", hr);
    /* red pixel: B=0 G=~0-1 R=255 A=255 */
    ok(buffer[2] >= 240 && buffer[3] == 255, "unexpected pixel colors: B=%u G=%u R=%u A=%u\n",
       buffer[0], buffer[1], buffer[2], buffer[3]);

    IWICBitmapFrameDecode_Release(frame);
    IWICBitmapDecoder_Release(decoder);
    IStream_Release(stream);

    /* Test via ImagingFactory sniffing */
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&factory);
    ok(hr == S_OK, "CoCreateInstance(CLSID_WICImagingFactory) failed, hr %#lx\n", hr);

    hdata = GlobalAlloc(GMEM_MOVEABLE, sizeof(webp_test_data));
    ptr = GlobalLock(hdata);
    memcpy(ptr, webp_test_data, sizeof(webp_test_data));
    GlobalUnlock(hdata);
    CreateStreamOnHGlobal(hdata, TRUE, &stream);

    hr = IWICImagingFactory_CreateDecoderFromStream(factory, stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder);
    ok(hr == S_OK, "CreateDecoderFromStream failed, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IWICBitmapDecoder_GetContainerFormat(decoder, &format);
        ok(hr == S_OK, "GetContainerFormat failed, hr %#lx\n", hr);
        ok(IsEqualGUID(&format, &GUID_ContainerFormatWebp), "sniffed format mismatch %s\n",
           wine_dbgstr_guid(&format));
        IWICBitmapDecoder_Release(decoder);
    }

    IStream_Release(stream);
    IWICImagingFactory_Release(factory);
}

START_TEST(webpformat)
{
    CoInitialize(NULL);
    test_webp_decoder();
    CoUninitialize();
}
