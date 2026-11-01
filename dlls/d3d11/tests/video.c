/*
 * Copyright 2026 CodeWeavers
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

#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include "d3d11_1.h"
#include "wine/debug.h"
#include "wine/test.h"

static const D3D_FEATURE_LEVEL feature_levels[] =
{
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_1,
};

static ID3D11Device *create_device(void)
{
    ID3D11Device *device = NULL;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
            feature_levels, ARRAY_SIZE(feature_levels), D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr))
        return NULL;

    return device;
}

static void get_video_interfaces(ID3D11Device *device, ID3D11VideoDevice **video_device,
        ID3D11VideoContext **video_context)
{
    ID3D11DeviceContext *context;

    *video_device = NULL;
    if (video_context)
        *video_context = NULL;

    if (FAILED(ID3D11Device_QueryInterface(device, &IID_ID3D11VideoDevice, (void **)video_device)))
        return;

    if (!video_context)
        return;

    ID3D11Device_GetImmediateContext(device, &context);
    ID3D11DeviceContext_QueryInterface(context, &IID_ID3D11VideoContext, (void **)video_context);
    ID3D11DeviceContext_Release(context);
}

static D3D11_VIDEO_PROCESSOR_CONTENT_DESC make_content_desc(unsigned int w, unsigned int h)
{
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc;

    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputFrameRate.Numerator = 30;
    desc.InputFrameRate.Denominator = 1;
    desc.InputWidth = w;
    desc.InputHeight = h;
    desc.OutputFrameRate.Numerator = 30;
    desc.OutputFrameRate.Denominator = 1;
    desc.OutputWidth = w;
    desc.OutputHeight = h;
    desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    return desc;
}

static HRESULT create_enumerator(ID3D11VideoDevice *video_device,
        unsigned int w, unsigned int h, ID3D11VideoProcessorEnumerator **enumerator)
{
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = make_content_desc(w, h);

    return ID3D11VideoDevice_CreateVideoProcessorEnumerator(video_device, &desc, enumerator);
}

static ID3D11Texture2D *create_texture_ex(ID3D11Device *device, unsigned int w, unsigned int h,
        DXGI_FORMAT format, unsigned int bind_flags, unsigned int cpu_access, D3D11_USAGE usage)
{
    D3D11_TEXTURE2D_DESC texture_desc;
    ID3D11Texture2D *texture;
    HRESULT hr;

    texture_desc.Width = w;
    texture_desc.Height = h;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = format;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = usage;
    texture_desc.BindFlags = bind_flags;
    texture_desc.CPUAccessFlags = cpu_access;
    texture_desc.MiscFlags = 0;

    hr = ID3D11Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    if (FAILED(hr))
        return NULL;

    return texture;
}

static ID3D11Texture2D *create_texture(ID3D11Device *device, unsigned int w, unsigned int h,
        DXGI_FORMAT format, unsigned int bind_flags, unsigned int cpu_access)
{
    return create_texture_ex(device, w, h, format, bind_flags, cpu_access, D3D11_USAGE_DEFAULT);
}

static unsigned int colour_at(unsigned int x, unsigned int y)
{
    /* B8G8R8A8 in memory: a=0xff, r = y, g = x, b = (x + y) & 0xff. */
    return 0xff000000 | (y << 16) | (x << 8) | ((x + y) & 0xff);
}

static void fill_texture(ID3D11DeviceContext *context, ID3D11Texture2D *texture, unsigned int w, unsigned int h)
{
    unsigned int x, y;
    DWORD *data;

    if (!(data = malloc(w * h * sizeof(*data))))
        return;

    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            data[y * w + x] = colour_at(x, y);

    ID3D11DeviceContext_UpdateSubresource(context, (ID3D11Resource *)texture, 0, NULL, data, w * sizeof(*data), 0);
    free(data);
}

static bool readback_texture(ID3D11Device *device, ID3D11DeviceContext *context,
        ID3D11Texture2D *texture, unsigned int w, unsigned int h, DWORD **out)
{
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Texture2D *staging;
    unsigned int x, y;
    DWORD *data;

    if (!(staging = create_texture_ex(device, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D11_CPU_ACCESS_READ,
            D3D11_USAGE_STAGING)))
        return false;

    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)texture);

    if (FAILED(ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map)))
    {
        ID3D11Texture2D_Release(staging);
        return false;
    }

    data = malloc(w * h * sizeof(*data));
    for (y = 0; y < h; ++y)
    {
        const DWORD *row = (const DWORD *)((const BYTE *)map.pData + y * map.RowPitch);
        for (x = 0; x < w; ++x)
            data[y * w + x] = row[x];
    }

    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    ID3D11Texture2D_Release(staging);

    *out = data;
    return true;
}

static void test_video_processor_enumerator(void)
{
    D3D11_VIDEO_PROCESSOR_FILTER_RANGE filter_range;
    D3D11_VIDEO_PROCESSOR_CUSTOM_RATE custom_rate;
    D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS rate_caps;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc, out_desc;
    struct ID3D11VideoProcessorEnumerator1 *enumerator1;
    D3D11_VIDEO_PROCESSOR_CAPS caps;
    ID3D11VideoDevice *video_device;
    ID3D11VideoProcessorEnumerator *enumerator;
    ID3D11Device *device;
    ULONG refcount;
    UINT flags;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    get_video_interfaces(device, &video_device, NULL);
    if (!video_device)
    {
        skip("Failed to get ID3D11VideoDevice.\n");
        ID3D11Device_Release(device);
        return;
    }

    /* Probe parameters of the shape a GPU driver consumer uses. */
    hr = create_enumerator(video_device, 1920, 1080, &enumerator);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr))
        goto done;

    /* Content desc roundtrip. */
    ID3D11VideoProcessorEnumerator_GetVideoProcessorContentDesc(enumerator, &out_desc);
    desc = make_content_desc(1920, 1080);
    ok(out_desc.InputWidth == desc.InputWidth, "Got input width %u.\n", out_desc.InputWidth);
    ok(out_desc.InputHeight == desc.InputHeight, "Got input height %u.\n", out_desc.InputHeight);
    ok(out_desc.OutputWidth == desc.OutputWidth, "Got output width %u.\n", out_desc.OutputWidth);
    ok(out_desc.OutputHeight == desc.OutputHeight, "Got output height %u.\n", out_desc.OutputHeight);
    ok(out_desc.InputFrameFormat == desc.InputFrameFormat, "Got frame format %#x.\n", out_desc.InputFrameFormat);
    ok(out_desc.Usage == desc.Usage, "Got usage %#x.\n", out_desc.Usage);
    ok(out_desc.InputFrameRate.Numerator == 30 && out_desc.InputFrameRate.Denominator == 1,
            "Got input frame rate %u/%u.\n", out_desc.InputFrameRate.Numerator, out_desc.InputFrameRate.Denominator);

    /* Caps. */
    hr = ID3D11VideoProcessorEnumerator_GetVideoProcessorCaps(enumerator, &caps);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(caps.RateConversionCapsCount >= 1, "Got %u rate conversion caps.\n", caps.RateConversionCapsCount);
    ok(caps.MaxInputStreams >= 1, "Got %u input streams.\n", caps.MaxInputStreams);
    ok(caps.MaxStreamStates >= caps.MaxInputStreams, "Got %u stream states, %u input streams.\n",
            caps.MaxStreamStates, caps.MaxInputStreams);

    /* Format support: the RGB formats a compositor uses must be input and output
     * capable, NV12 at least input capable. */
    hr = ID3D11VideoProcessorEnumerator_CheckVideoProcessorFormat(enumerator, DXGI_FORMAT_B8G8R8A8_UNORM, &flags);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(flags == (D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT),
            "Got flags %#x for BGRA8.\n", flags);

    hr = ID3D11VideoProcessorEnumerator_CheckVideoProcessorFormat(enumerator, DXGI_FORMAT_NV12, &flags);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT, "Got flags %#x for NV12.\n", flags);

    hr = ID3D11VideoProcessorEnumerator_CheckVideoProcessorFormat(enumerator, DXGI_FORMAT_UNKNOWN, &flags);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(flags == 0, "Got flags %#x for UNKNOWN.\n", flags);

    /* Rate conversion. */
    hr = ID3D11VideoProcessorEnumerator_GetVideoProcessorRateConversionCaps(enumerator, 0, &rate_caps);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = ID3D11VideoProcessorEnumerator_GetVideoProcessorRateConversionCaps(enumerator, caps.RateConversionCapsCount,
            &rate_caps);
    ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

    hr = ID3D11VideoProcessorEnumerator_GetVideoProcessorCustomRate(enumerator, 0, 0, &custom_rate);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(custom_rate.OutputFrames >= 1, "Got %u output frames.\n", custom_rate.OutputFrames);
    hr = ID3D11VideoProcessorEnumerator_GetVideoProcessorCustomRate(enumerator, 0, rate_caps.CustomRateCount,
            &custom_rate);
    ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

    /* Filter ranges. */
    hr = ID3D11VideoProcessorEnumerator_GetVideoProcessorFilterRange(enumerator,
            D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS, &filter_range);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(filter_range.Minimum < filter_range.Maximum, "Got range %d..%d.\n", filter_range.Minimum, filter_range.Maximum);
    ok(filter_range.Default >= filter_range.Minimum && filter_range.Default <= filter_range.Maximum,
            "Got default %d.\n", filter_range.Default);
    hr = ID3D11VideoProcessorEnumerator_GetVideoProcessorFilterRange(enumerator, 0x1234, &filter_range);
    ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

    /* Devicechild plumbing. */
    {
        ID3D11Device *device2 = NULL;
        ID3D11VideoProcessorEnumerator_GetDevice(enumerator, &device2);
        ok(device2 == device, "Got device %p.\n", device2);
        if (device2)
            ID3D11Device_Release(device2);
    }

    hr = ID3D11VideoProcessorEnumerator_QueryInterface(enumerator, &IID_ID3D11VideoProcessorEnumerator1,
            (void **)&enumerator1);
    if (SUCCEEDED(hr))
    {
        BOOL supported = false;

        hr = ID3D11VideoProcessorEnumerator1_CheckVideoProcessorFormatConversion(enumerator1,
                DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
                DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &supported);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        ok(supported, "BGRA -> BGRA conversion not supported.\n");

        supported = true;
        hr = ID3D11VideoProcessorEnumerator1_CheckVideoProcessorFormatConversion(enumerator1,
                DXGI_FORMAT_NV12, DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601,
                DXGI_FORMAT_R8G8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &supported);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        ok(!supported, "Got unsupported NV12 -> R8G8 conversion reported as supported.\n");

        ID3D11VideoProcessorEnumerator1_Release(enumerator1);
    }

    refcount = ID3D11VideoProcessorEnumerator_Release(enumerator);
    ok(!refcount, "Enumerator has %lu references left.\n", refcount);

    /* Invalid desc validation. */
    {
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC invalid = desc;

        hr = ID3D11VideoDevice_CreateVideoProcessorEnumerator(video_device, NULL, &enumerator);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

        invalid = desc;
        invalid.InputWidth = 0;
        hr = ID3D11VideoDevice_CreateVideoProcessorEnumerator(video_device, &invalid, &enumerator);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

        invalid = desc;
        invalid.OutputHeight = 0;
        hr = ID3D11VideoDevice_CreateVideoProcessorEnumerator(video_device, &invalid, &enumerator);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

        invalid = desc;
        invalid.Usage = (D3D11_VIDEO_USAGE)0x1234;
        hr = ID3D11VideoDevice_CreateVideoProcessorEnumerator(video_device, &invalid, &enumerator);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

        invalid = desc;
        invalid.InputFrameFormat = (D3D11_VIDEO_FRAME_FORMAT)0x1234;
        hr = ID3D11VideoDevice_CreateVideoProcessorEnumerator(video_device, &invalid, &enumerator);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

        hr = ID3D11VideoDevice_CreateVideoProcessorEnumerator(video_device, &desc, NULL);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
    }

    return;

done:
    ID3D11VideoDevice_Release(video_device);
    refcount = ID3D11Device_Release(device);
    ok(!refcount, "Device has %lu references left.\n", refcount);
}

static void test_video_processor(void)
{
    D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS rate_caps;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc, out_desc;
    ID3D11VideoProcessorEnumerator *enumerator = NULL;
    ID3D11VideoProcessor *processor = NULL;
    ID3D11VideoDevice *video_device;
    ID3D11Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    get_video_interfaces(device, &video_device, NULL);
    if (!video_device)
    {
        skip("Failed to get ID3D11VideoDevice.\n");
        ID3D11Device_Release(device);
        return;
    }

    hr = create_enumerator(video_device, 1280, 720, &enumerator);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr))
        goto done;

    hr = ID3D11VideoDevice_CreateVideoProcessor(video_device, enumerator, 0, &processor);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr))
        goto done;

    /* The content desc of the processor must be the one the enumerator was made with. */
    ID3D11VideoProcessor_GetContentDesc(processor, &out_desc);
    desc = make_content_desc(1280, 720);
    ok(out_desc.InputWidth == desc.InputWidth && out_desc.InputHeight == desc.InputHeight,
            "Got input size %ux%u.\n", out_desc.InputWidth, out_desc.InputHeight);
    ok(out_desc.OutputWidth == desc.OutputWidth && out_desc.OutputHeight == desc.OutputHeight,
            "Got output size %ux%u.\n", out_desc.OutputWidth, out_desc.OutputHeight);

    ID3D11VideoProcessor_GetRateConversionCaps(processor, &rate_caps);
    ok(rate_caps.CustomRateCount >= 1, "Got %u custom rates.\n", rate_caps.CustomRateCount);

    {
        ID3D11Device *device2 = NULL;
        ID3D11VideoProcessor_GetDevice(processor, &device2);
        ok(device2 == device, "Got device %p.\n", device2);
        if (device2)
            ID3D11Device_Release(device2);
    }

    {
        ID3D11VideoProcessor *p2 = NULL;

        hr = ID3D11VideoProcessor_QueryInterface(processor, &IID_ID3D11VideoProcessor, (void **)&p2);
        ok(hr == S_OK && p2 == processor, "Got hr %#lx, iface %p.\n", hr, p2);
        if (p2)
            ID3D11VideoProcessor_Release(p2);

        hr = ID3D11VideoProcessor_QueryInterface(processor, &IID_ID3D11VideoDecoder, (void **)&p2);
        ok(hr == E_NOINTERFACE, "Got hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
            ID3D11VideoDecoder_Release((ID3D11VideoDecoder *)p2);
    }

    /* Invalid parameters. A failed create must not produce an object, so probe
     * them with a separate pointer that is never released. */
    {
        ID3D11VideoProcessor *bad = (ID3D11VideoProcessor *)0xdeadbeef;

        hr = ID3D11VideoDevice_CreateVideoProcessor(video_device, NULL, 0, &bad);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
        ok(!bad, "Got processor %p.\n", bad);
        hr = ID3D11VideoDevice_CreateVideoProcessor(video_device, enumerator, 0, NULL);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
        bad = (ID3D11VideoProcessor *)0xdeadbeef;
        hr = ID3D11VideoDevice_CreateVideoProcessor(video_device, enumerator, 0x1234, &bad);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
        ok(!bad, "Got processor %p.\n", bad);
    }

    refcount = ID3D11VideoProcessor_Release(processor);
    ok(!refcount, "Processor has %lu references left.\n", refcount);

    refcount = ID3D11VideoProcessorEnumerator_Release(enumerator);
    ok(!refcount, "Enumerator has %lu references left.\n", refcount);
    ID3D11VideoDevice_Release(video_device);
    refcount = ID3D11Device_Release(device);
    ok(!refcount, "Device has %lu references left.\n", refcount);
    return;

done:
    if (processor)
        ID3D11VideoProcessor_Release(processor);
    if (enumerator)
        ID3D11VideoProcessorEnumerator_Release(enumerator);
    ID3D11VideoDevice_Release(video_device);
    refcount = ID3D11Device_Release(device);
    ok(!refcount, "Device has %lu references left.\n", refcount);
}

static void test_video_processor_state(void)
{
    ID3D11VideoProcessorEnumerator *enumerator = NULL;
    ID3D11VideoProcessor *processor = NULL;
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE colour_space, out_colour_space;
    ID3D11VideoDevice *video_device;
    ID3D11VideoContext *video_context;
    ID3D11Device *device;
    ULONG refcount;
    RECT rect, out_rect;
    D3D11_VIDEO_COLOR colour;
    SIZE size, out_size;
    BOOL enabled;
    float alpha;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    get_video_interfaces(device, &video_device, &video_context);
    if (!video_device || !video_context)
    {
        skip("Failed to get video interfaces.\n");
        if (video_device)
            ID3D11VideoDevice_Release(video_device);
        if (video_context)
            ID3D11VideoContext_Release(video_context);
        ID3D11Device_Release(device);
        return;
    }

    hr = create_enumerator(video_device, 640, 480, &enumerator);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr))
        goto done;

    hr = ID3D11VideoDevice_CreateVideoProcessor(video_device, enumerator, 0, &processor);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr))
        goto done;

    /* Output target rect. */
    SetRect(&rect, 4, 8, 604, 468);
    ID3D11VideoContext_VideoProcessorSetOutputTargetRect(video_context, processor, TRUE, &rect);
    SetRect(&out_rect, 0, 0, 0, 0);
    ID3D11VideoContext_VideoProcessorGetOutputTargetRect(video_context, processor, &enabled, &out_rect);
    ok(enabled, "Target rect not enabled.\n");
    ok(EqualRect(&rect, &out_rect), "Got rect %s.\n", wine_dbgstr_rect(&out_rect));
    ID3D11VideoContext_VideoProcessorSetOutputTargetRect(video_context, processor, FALSE, NULL);
    ID3D11VideoContext_VideoProcessorGetOutputTargetRect(video_context, processor, &enabled, &out_rect);
    ok(!enabled, "Target rect still enabled.\n");

    /* Output background colour. */
    colour.RGBA.R = 0.25f, colour.RGBA.G = 0.5f, colour.RGBA.B = 0.75f, colour.RGBA.A = 1.0f;
    ID3D11VideoContext_VideoProcessorSetOutputBackgroundColor(video_context, processor, FALSE, &colour);
    memset(&colour, 0, sizeof(colour));
    ID3D11VideoContext_VideoProcessorGetOutputBackgroundColor(video_context, processor, &enabled, &colour);
    ok(!enabled, "Background colour reported as YCbCr.\n");
    ok(colour.RGBA.R == 0.25f && colour.RGBA.G == 0.5f && colour.RGBA.B == 0.75f,
            "Got colour {%.8e, %.8e, %.8e}.\n", colour.RGBA.R, colour.RGBA.G, colour.RGBA.B);

    /* Output colour space. */
    memset(&colour_space, 0, sizeof(colour_space));
    colour_space.Nominal_Range = 1;
    colour_space.YCbCr_Matrix = 1;
    ID3D11VideoContext_VideoProcessorSetOutputColorSpace(video_context, processor, &colour_space);
    memset(&out_colour_space, 0, sizeof(out_colour_space));
    ID3D11VideoContext_VideoProcessorGetOutputColorSpace(video_context, processor, &out_colour_space);
    ok(out_colour_space.Nominal_Range == 1 && out_colour_space.YCbCr_Matrix == 1,
            "Got colour space %u/%u.\n", out_colour_space.Nominal_Range, out_colour_space.YCbCr_Matrix);

    /* Alpha fill mode. */
    ID3D11VideoContext_VideoProcessorSetOutputAlphaFillMode(video_context, processor,
            D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_SOURCE_STREAM, 3);
    {
        D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE mode;
        UINT stream_idx = 0;

        ID3D11VideoContext_VideoProcessorGetOutputAlphaFillMode(video_context, processor, &mode, &stream_idx);
        ok(mode == D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_SOURCE_STREAM, "Got mode %#x.\n", mode);
        ok(stream_idx == 3, "Got stream index %u.\n", stream_idx);
    }

    /* Constriction. */
    size.cx = 320, size.cy = 200;
    ID3D11VideoContext_VideoProcessorSetOutputConstriction(video_context, processor, TRUE, size);
    ID3D11VideoContext_VideoProcessorGetOutputConstriction(video_context, processor, &enabled, &out_size);
    ok(enabled, "Constriction not enabled.\n");
    ok(out_size.cx == 320 && out_size.cy == 200, "Got size %ldx%ld.\n", out_size.cx, out_size.cy);

    /* Stereo mode. */
    ID3D11VideoContext_VideoProcessorSetOutputStereoMode(video_context, processor, TRUE);
    ID3D11VideoContext_VideoProcessorGetOutputStereoMode(video_context, processor, &enabled);
    ok(enabled, "Stereo mode not enabled.\n");

    /* Stream source and dest rects. */
    SetRect(&rect, 16, 16, 116, 116);
    ID3D11VideoContext_VideoProcessorSetStreamSourceRect(video_context, processor, 1, TRUE, &rect);
    SetRect(&rect, 2, 4, 102, 104);
    ID3D11VideoContext_VideoProcessorSetStreamDestRect(video_context, processor, 1, TRUE, &rect);
    SetRect(&out_rect, 0, 0, 0, 0);
    ID3D11VideoContext_VideoProcessorGetStreamSourceRect(video_context, processor, 1, &enabled, &out_rect);
    ok(enabled && out_rect.left == 16 && out_rect.top == 16 && out_rect.right == 116 && out_rect.bottom == 116,
            "Got source rect %s.\n", wine_dbgstr_rect(&out_rect));
    SetRect(&out_rect, 0, 0, 0, 0);
    ID3D11VideoContext_VideoProcessorGetStreamDestRect(video_context, processor, 1, &enabled, &out_rect);
    ok(enabled && out_rect.left == 2 && out_rect.top == 4 && out_rect.right == 102 && out_rect.bottom == 104,
            "Got dest rect %s.\n", wine_dbgstr_rect(&out_rect));
    /* Stream 0 must be untouched by the writes to stream 1. */
    ID3D11VideoContext_VideoProcessorGetStreamSourceRect(video_context, processor, 0, &enabled, &out_rect);
    ok(!enabled, "Stream 0 source rect enabled.\n");

    /* Stream alpha. */
    ID3D11VideoContext_VideoProcessorSetStreamAlpha(video_context, processor, 2, TRUE, 0.5f);
    ID3D11VideoContext_VideoProcessorGetStreamAlpha(video_context, processor, 2, &enabled, &alpha);
    ok(enabled && alpha == 0.5f, "Got enabled %d, alpha %.8e.\n", enabled, alpha);

    /* Stream palette. */
    {
        static const UINT entries[4] = {0, 64, 128, 255};
        UINT out_entries[6] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u};

        ID3D11VideoContext_VideoProcessorSetStreamPalette(video_context, processor, 0, 4, entries);
        ID3D11VideoContext_VideoProcessorGetStreamPalette(video_context, processor, 0, 6, out_entries);
        ok(out_entries[0] == 0 && out_entries[1] == 64 && out_entries[2] == 128 && out_entries[3] == 255,
                "Got palette {%u, %u, %u, %u}.\n", out_entries[0], out_entries[1], out_entries[2], out_entries[3]);
        ok(out_entries[4] == 0 && out_entries[5] == 0, "Got palette {%u, %u}.\n", out_entries[4], out_entries[5]);
    }

    /* Stream filters. */
    ID3D11VideoContext_VideoProcessorSetStreamFilter(video_context, processor, 0,
            D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST, TRUE, 42);
    {
        int level = 0;

        ID3D11VideoContext_VideoProcessorGetStreamFilter(video_context, processor, 0,
                D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST, &enabled, &level);
        ok(enabled && level == 42, "Got enabled %d, level %d.\n", enabled, level);
        ID3D11VideoContext_VideoProcessorGetStreamFilter(video_context, processor, 0,
                D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS, &enabled, &level);
        ok(!enabled && level == 0, "Got enabled %d, level %d.\n", enabled, level);
    }

    /* Stream rotation. */
    ID3D11VideoContext_VideoProcessorSetStreamRotation(video_context, processor, 1, TRUE,
            D3D11_VIDEO_PROCESSOR_ROTATION_90);
    {
        D3D11_VIDEO_PROCESSOR_ROTATION rotation = D3D11_VIDEO_PROCESSOR_ROTATION_IDENTITY;

        ID3D11VideoContext_VideoProcessorGetStreamRotation(video_context, processor, 1, &enabled, &rotation);
        ok(enabled && rotation == D3D11_VIDEO_PROCESSOR_ROTATION_90, "Got enabled %d, rotation %#x.\n", enabled, rotation);
    }

    /* Stream frame format, colour space, output rate, auto processing, luma key, stereo. */
    ID3D11VideoContext_VideoProcessorSetStreamFrameFormat(video_context, processor, 0,
            D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST);
    {
        D3D11_VIDEO_FRAME_FORMAT format;

        ID3D11VideoContext_VideoProcessorGetStreamFrameFormat(video_context, processor, 0, &format);
        ok(format == D3D11_VIDEO_FRAME_FORMAT_INTERLACED_TOP_FIELD_FIRST, "Got format %#x.\n", format);
    }

    ID3D11VideoContext_VideoProcessorSetStreamColorSpace(video_context, processor, 0, &colour_space);
    ID3D11VideoContext_VideoProcessorGetStreamColorSpace(video_context, processor, 0, &out_colour_space);
    ok(out_colour_space.Nominal_Range == 1, "Got stream colour space %u.\n", out_colour_space.Nominal_Range);

    ID3D11VideoContext_VideoProcessorSetStreamOutputRate(video_context, processor, 0,
            D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_HALF, TRUE, NULL);
    {
        D3D11_VIDEO_PROCESSOR_OUTPUT_RATE rate;

        ID3D11VideoContext_VideoProcessorGetStreamOutputRate(video_context, processor, 0, &rate, &enabled, NULL);
        ok(rate == D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_HALF && enabled, "Got rate %#x, repeat %d.\n", rate, enabled);
    }

    ID3D11VideoContext_VideoProcessorSetStreamAutoProcessingMode(video_context, processor, 0, FALSE);
    ID3D11VideoContext_VideoProcessorGetStreamAutoProcessingMode(video_context, processor, 0, &enabled);
    ok(!enabled, "Auto processing mode enabled.\n");

    ID3D11VideoContext_VideoProcessorSetStreamLumaKey(video_context, processor, 0, TRUE, 0.1f, 0.9f);
    {
        float lower = 0.0f, upper = 0.0f;

        ID3D11VideoContext_VideoProcessorGetStreamLumaKey(video_context, processor, 0, &enabled, &lower, &upper);
        ok(enabled && lower == 0.1f && upper == 0.9f, "Got enabled %d, %.8e..%.8e.\n", enabled, lower, upper);
    }

    ID3D11VideoContext_VideoProcessorSetStreamPixelAspectRatio(video_context, processor, 0, TRUE, NULL, NULL);
    ID3D11VideoContext_VideoProcessorSetStreamStereoFormat(video_context, processor, 0, TRUE,
            D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_HORIZONTAL, TRUE, FALSE, 0, 3);
    {
        D3D11_VIDEO_PROCESSOR_STEREO_FORMAT format;
        int mono_offset = 0;
        BOOL left = FALSE, base = FALSE;

        ID3D11VideoContext_VideoProcessorGetStreamStereoFormat(video_context, processor, 0, &enabled, &format,
                &left, &base, NULL, &mono_offset);
        ok(enabled && format == D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_HORIZONTAL, "Got enabled %d, format %#x.\n",
                enabled, format);
        ok(left && !base && mono_offset == 3, "Got left %d, base %d, offset %d.\n", left, base, mono_offset);
    }

    /* Vendor extensions are not implemented, so they must be rejected, not stubbed. */
    {
        static const GUID extension_guid = {0x1b3b6c1d, 0x1e3f, 0x4d1e, {0x9c, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f, 0x70}};
        UINT value = 0;

        hr = ID3D11VideoContext_VideoProcessorSetOutputExtension(video_context, processor, &extension_guid,
                sizeof(value), &value);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
        hr = ID3D11VideoContext_VideoProcessorGetOutputExtension(video_context, processor, &extension_guid,
                sizeof(value), &value);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
        hr = ID3D11VideoContext_VideoProcessorSetStreamExtension(video_context, processor, 0, &extension_guid,
                sizeof(value), &value);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
        hr = ID3D11VideoContext_VideoProcessorGetStreamExtension(video_context, processor, 0, &extension_guid,
                sizeof(value), &value);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
    }

    /* Views. */
    {
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc;
        ID3D11VideoProcessorOutputView *output_view;
        ID3D11VideoProcessorInputView *input_view;
        ID3D11Texture2D *texture;

        texture = create_texture(device, 64, 32, DXGI_FORMAT_B8G8R8A8_UNORM,
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0);
        ok(!!texture, "Failed to create texture.\n");
        if (texture)
        {
            input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            input_desc.Texture2D.MipSlice = 0;
            input_desc.Texture2D.ArraySlice = 0;
            hr = ID3D11VideoDevice_CreateVideoProcessorInputView(video_device, (ID3D11Resource *)texture,
                    enumerator, &input_desc, &input_view);
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            if (SUCCEEDED(hr))
            {
                D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC out_input_desc;

                ID3D11VideoProcessorInputView_GetDesc(input_view, &out_input_desc);
                ok(out_input_desc.ViewDimension == D3D11_VPIV_DIMENSION_TEXTURE2D, "Got dimension %#x.\n",
                        out_input_desc.ViewDimension);
                ok(out_input_desc.Texture2D.MipSlice == 0, "Got mip slice %u.\n", out_input_desc.Texture2D.MipSlice);

                {
                    ID3D11Resource *resource = NULL;

                    ID3D11VideoProcessorInputView_GetResource(input_view, &resource);
                    ok(resource == (ID3D11Resource *)texture, "Got resource %p.\n", resource);
                    if (resource)
                        ID3D11Resource_Release(resource);
                }

                {
                    ID3D11View *view2 = NULL;

                    hr = ID3D11VideoProcessorInputView_QueryInterface(input_view, &IID_ID3D11View, (void **)&view2);
                    ok(hr == S_OK && view2, "Got hr %#lx, iface %p.\n", hr, view2);
                    if (view2)
                        ID3D11View_Release(view2);
                }
                ID3D11VideoProcessorInputView_Release(input_view);
                input_view = NULL;
            }

            output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            output_desc.Texture2D.MipSlice = 0;
            hr = ID3D11VideoDevice_CreateVideoProcessorOutputView(video_device, (ID3D11Resource *)texture,
                    enumerator, &output_desc, &output_view);
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            if (SUCCEEDED(hr))
            {
                D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out_output_desc;

                ID3D11VideoProcessorOutputView_GetDesc(output_view, &out_output_desc);
                ok(out_output_desc.ViewDimension == D3D11_VPOV_DIMENSION_TEXTURE2D, "Got dimension %#x.\n",
                        out_output_desc.ViewDimension);
                ID3D11VideoProcessorOutputView_Release(output_view);
                output_view = NULL;
            }

            /* Invalid view parameters. */
            input_desc.ViewDimension = D3D11_VPIV_DIMENSION_UNKNOWN;
            hr = ID3D11VideoDevice_CreateVideoProcessorInputView(video_device, (ID3D11Resource *)texture,
                    enumerator, &input_desc, &input_view);
            ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

            output_desc.ViewDimension = D3D11_VPOV_DIMENSION_UNKNOWN;
            hr = ID3D11VideoDevice_CreateVideoProcessorOutputView(video_device, (ID3D11Resource *)texture,
                    enumerator, &output_desc, &output_view);
            ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

            input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            input_desc.Texture2D.MipSlice = 7;
            hr = ID3D11VideoDevice_CreateVideoProcessorInputView(video_device, (ID3D11Resource *)texture,
                    enumerator, &input_desc, &input_view);
            ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

            hr = ID3D11VideoDevice_CreateVideoProcessorInputView(video_device, NULL, enumerator, &input_desc,
                    &input_view);
            ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);

            ID3D11Texture2D_Release(texture);
        }
    }

    refcount = ID3D11VideoProcessor_Release(processor);
    ok(!refcount, "Processor has %lu references left.\n", refcount);
    processor = NULL;
    refcount = ID3D11VideoProcessorEnumerator_Release(enumerator);
    ok(!refcount, "Enumerator has %lu references left.\n", refcount);
    enumerator = NULL;

done:
    if (processor)
        ID3D11VideoProcessor_Release(processor);
    if (enumerator)
        ID3D11VideoProcessorEnumerator_Release(enumerator);
    ID3D11VideoContext_Release(video_context);
    ID3D11VideoDevice_Release(video_device);
    refcount = ID3D11Device_Release(device);
    ok(!refcount, "Device has %lu references left.\n", refcount);
}

static void test_video_processor_blt(void)
{
    const unsigned int w = 64, h = 32;
    ID3D11VideoProcessorEnumerator *enumerator = NULL;
    ID3D11VideoProcessorOutputView *output_view = NULL;
    ID3D11VideoProcessorInputView *input_view = NULL;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc;
    ID3D11VideoProcessor *processor = NULL;
    ID3D11VideoDevice *video_device;
    ID3D11VideoContext *video_context;
    ID3D11DeviceContext *context = NULL;
    D3D11_VIDEO_PROCESSOR_STREAM stream;
    ID3D11Texture2D *src = NULL, *dst = NULL;
    ID3D11Device *device;
    ID3D11RenderTargetView *rtv;
    ULONG refcount;
    static const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static const float sentinel[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    DWORD sentinel_value = 0;
    DWORD *data;
    RECT rect;
    unsigned int x, y;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    get_video_interfaces(device, &video_device, &video_context);
    if (!video_device || !video_context)
    {
        skip("Failed to get video interfaces.\n");
        if (video_device)
            ID3D11VideoDevice_Release(video_device);
        if (video_context)
            ID3D11VideoContext_Release(video_context);
        ID3D11Device_Release(device);
        return;
    }

    ID3D11Device_GetImmediateContext(device, &context);

    hr = create_enumerator(video_device, w, h, &enumerator);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr))
        goto done;

    hr = ID3D11VideoDevice_CreateVideoProcessor(video_device, enumerator, 0, &processor);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr))
        goto done;

    src = create_texture(device, w, h, DXGI_FORMAT_B8G8R8A8_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0);
    dst = create_texture(device, w, h, DXGI_FORMAT_B8G8R8A8_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0);
    ok(!!src && !!dst, "Failed to create textures.\n");
    if (!src || !dst)
        goto done;

    input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    input_desc.Texture2D.MipSlice = 0;
    input_desc.Texture2D.ArraySlice = 0;
    hr = ID3D11VideoDevice_CreateVideoProcessorInputView(video_device, (ID3D11Resource *)src, enumerator,
            &input_desc, &input_view);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr))
        goto done;

    output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    output_desc.Texture2D.MipSlice = 0;
    hr = ID3D11VideoDevice_CreateVideoProcessorOutputView(video_device, (ID3D11Resource *)dst, enumerator,
            &output_desc, &output_view);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr))
        goto done;

    fill_texture(context, src, w, h);
    if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)dst, NULL, &rtv)))
    {
        ID3D11DeviceContext_ClearRenderTargetView(context, rtv, sentinel);
        ID3D11RenderTargetView_Release(rtv);
    }

    /* A full surface 1:1 blit must copy the pixels. */
    memset(&stream, 0, sizeof(stream));
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.pInputSurface = input_view;
    hr = ID3D11VideoContext_VideoProcessorBlt(video_context, processor, output_view, 0, 1, &stream);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    if (SUCCEEDED(hr) && readback_texture(device, context, dst, w, h, &data))
    {
        unsigned int mismatch = 0, unchanged = 0;

        for (y = 0; y < h; ++y)
        {
            for (x = 0; x < w; ++x)
            {
                if (data[y * w + x] != colour_at(x, y))
                    ++mismatch;
            }
        }
        ok(!mismatch, "Got %u mismatching pixels out of %u.\n", mismatch, w * h);
        (void)unchanged;
        free(data);
    }

    /* Source and dest rects: a 1:1 copy of a sub-rect. */
    if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)dst, NULL, &rtv)))
    {
        ID3D11DeviceContext_ClearRenderTargetView(context, rtv, black);
        ID3D11RenderTargetView_Release(rtv);
    }

    SetRect(&rect, 8, 4, 40, 20);
    ID3D11VideoContext_VideoProcessorSetStreamSourceRect(video_context, processor, 0, TRUE, &rect);
    SetRect(&rect, 0, 0, 32, 16);
    ID3D11VideoContext_VideoProcessorSetStreamDestRect(video_context, processor, 0, TRUE, &rect);

    hr = ID3D11VideoContext_VideoProcessorBlt(video_context, processor, output_view, 0, 1, &stream);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    if (SUCCEEDED(hr) && readback_texture(device, context, dst, w, h, &data))
    {
        unsigned int mismatch = 0;

        for (y = 0; y < 16; ++y)
            for (x = 0; x < 32; ++x)
            {
                if (data[y * w + x] != colour_at(x + 8, y + 4))
                    ++mismatch;
            }
        ok(!mismatch, "Got %u mismatching pixels in the blitted rect.\n", mismatch);
        /* Outside the dest rect the clear must survive. */
        ok(data[20 * w + 40] == 0xff000000, "Got %#lx outside the dest rect.\n", data[20 * w + 40]);
        free(data);
    }

    /* A stretched blit must succeed and touch the destination. */
    SetRect(&rect, 0, 0, 32, 16);
    ID3D11VideoContext_VideoProcessorSetStreamSourceRect(video_context, processor, 0, FALSE, NULL);
    ID3D11VideoContext_VideoProcessorSetStreamDestRect(video_context, processor, 0, TRUE, &rect);
    if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)dst, NULL, &rtv)))
    {
        ID3D11DeviceContext_ClearRenderTargetView(context, rtv, sentinel);
        ID3D11RenderTargetView_Release(rtv);
    }
    if (readback_texture(device, context, dst, w, h, &data))
    {
        sentinel_value = data[0];
        free(data);
    }
    hr = ID3D11VideoContext_VideoProcessorBlt(video_context, processor, output_view, 0, 1, &stream);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (SUCCEEDED(hr) && readback_texture(device, context, dst, w, h, &data))
    {
        unsigned int changed = 0;

        for (y = 0; y < 16; ++y)
            for (x = 0; x < 32; ++x)
                if (data[y * w + x] != sentinel_value)
                    ++changed;
        ok(changed > 32 * 16 / 2, "Got %u changed pixels of %u.\n", changed, 32 * 16);
        free(data);
    }

    /* Two streams, each with its own dest rect. */
    {
        D3D11_VIDEO_PROCESSOR_STREAM streams[2];

        ID3D11VideoContext_VideoProcessorSetStreamDestRect(video_context, processor, 0, FALSE, NULL);
        ID3D11VideoContext_VideoProcessorSetStreamDestRect(video_context, processor, 1, FALSE, NULL);
        SetRect(&rect, 0, 0, 32, 32);
        ID3D11VideoContext_VideoProcessorSetOutputTargetRect(video_context, processor, TRUE, &rect);

        memset(streams, 0, sizeof(streams));
        streams[0].Enable = TRUE;
        streams[0].OutputIndex = 0;
        streams[0].pInputSurface = input_view;
        streams[1].Enable = TRUE;
        streams[1].OutputIndex = 1;
        streams[1].pInputSurface = input_view;
        hr = ID3D11VideoContext_VideoProcessorBlt(video_context, processor, output_view, 0, 2, streams);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        ID3D11VideoContext_VideoProcessorSetOutputTargetRect(video_context, processor, FALSE, NULL);
    }

    /* Invalid parameters. */
    hr = ID3D11VideoContext_VideoProcessorBlt(video_context, processor, output_view, 0, 1, NULL);
    ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
    hr = ID3D11VideoContext_VideoProcessorBlt(video_context, processor, NULL, 0, 1, &stream);
    ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
    stream.OutputIndex = 0x1234;
    hr = ID3D11VideoContext_VideoProcessorBlt(video_context, processor, output_view, 0, 1, &stream);
    ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
    stream.OutputIndex = 0;
    stream.Enable = FALSE;
    hr = ID3D11VideoContext_VideoProcessorBlt(video_context, processor, output_view, 0, 1, &stream);
    ok(hr == S_OK, "Got hr %#lx for a disabled stream.\n", hr);

    /* NV12 -> RGB conversion is not supported by the blit path; document that here. */
    {
        ID3D11Texture2D *nv12;

        nv12 = create_texture(device, w, h, DXGI_FORMAT_NV12, D3D11_BIND_SHADER_RESOURCE, 0);
        if (nv12)
        {
            input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            input_desc.Texture2D.MipSlice = 0;
            input_desc.Texture2D.ArraySlice = 0;
            hr = ID3D11VideoDevice_CreateVideoProcessorInputView(video_device, (ID3D11Resource *)nv12, enumerator,
                    &input_desc, &input_view);
            ok(hr == S_OK, "Got hr %#lx for NV12 input view.\n", hr);
            if (SUCCEEDED(hr))
            {
                ID3D11VideoContext_VideoProcessorSetStreamDestRect(video_context, processor, 0, FALSE, NULL);
                stream.Enable = TRUE;
                stream.OutputIndex = 0;
                stream.pInputSurface = input_view;
                hr = ID3D11VideoContext_VideoProcessorBlt(video_context, processor, output_view, 0, 1, &stream);
                ok(FAILED(hr), "Got hr %#lx for NV12 -> BGRA blit.\n", hr);
                ID3D11VideoProcessorInputView_Release(input_view);
            }
            ID3D11Texture2D_Release(nv12);
        }
    }

    if (input_view)
        ID3D11VideoProcessorInputView_Release(input_view);
    if (output_view)
        ID3D11VideoProcessorOutputView_Release(output_view);
    if (src)
        ID3D11Texture2D_Release(src);
    if (dst)
        ID3D11Texture2D_Release(dst);
    if (processor)
    {
        refcount = ID3D11VideoProcessor_Release(processor);
        ok(!refcount, "Processor has %lu references left.\n", refcount);
    }
    if (enumerator)
    {
        refcount = ID3D11VideoProcessorEnumerator_Release(enumerator);
        ok(!refcount, "Enumerator has %lu references left.\n", refcount);
    }

    return;

done:
    if (context)
        ID3D11DeviceContext_Release(context);
    ID3D11VideoContext_Release(video_context);
    ID3D11VideoDevice_Release(video_device);
    refcount = ID3D11Device_Release(device);
    ok(!refcount, "Device has %lu references left.\n", refcount);
}

static void test_video_decoder_profile_support(void)
{
    ID3D11VideoDevice1 *video_device1 = NULL;
    ID3D11VideoDevice *video_device;
    ID3D11Device *device;
    ULONG refcount;
    unsigned int i, count;
    GUID profile;
    BOOL supported;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D11Device_QueryInterface(device, &IID_ID3D11VideoDevice, (void **)&video_device);
    if (FAILED(hr))
    {
        skip("Failed to get ID3D11VideoDevice.\n");
        ID3D11Device_Release(device);
        return;
    }

    hr = ID3D11VideoDevice_QueryInterface(video_device, &IID_ID3D11VideoDevice1, (void **)&video_device1);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);

    count = ID3D11VideoDevice_GetVideoDecoderProfileCount(video_device);
    trace("Got %u decoder profiles.\n", count);

    for (i = 0; i < count; ++i)
    {
        hr = ID3D11VideoDevice_GetVideoDecoderProfile(video_device, i, &profile);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);

        /* A profile the device advertises must be reported as supported, and NV12
         * output must be reported as available for it. */
        supported = FALSE;
        hr = ID3D11VideoDevice1_GetVideoProfileSupported(video_device1, &profile, &supported);
        ok(hr == S_OK, "Got hr %#lx for profile %s.\n", hr, debugstr_guid(&profile));
        ok(supported, "Profile %s not supported.\n", debugstr_guid(&profile));

        supported = FALSE;
        hr = ID3D11VideoDevice_CheckVideoDecoderFormat(video_device, &profile, DXGI_FORMAT_NV12, &supported);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        ok(supported, "NV12 not supported for profile %s.\n", debugstr_guid(&profile));

        supported = TRUE;
        hr = ID3D11VideoDevice_CheckVideoDecoderFormat(video_device, &profile, DXGI_FORMAT_R8G8B8A8_UNORM,
                &supported);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        ok(!supported, "R8G8B8A8 reported supported for profile %s.\n", debugstr_guid(&profile));
    }

    {
        static const GUID unknown_profile = {0xdeadbeef, 0x1234, 0x5678, {0x9a, 0xbc, 0xde, 0xef, 0x01, 0x23, 0x45, 0x67}};

        supported = TRUE;
        hr = ID3D11VideoDevice1_GetVideoProfileSupported(video_device1, &unknown_profile, &supported);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        ok(!supported, "Unknown profile reported supported.\n");

        hr = ID3D11VideoDevice1_GetVideoProfileSupported(video_device1, NULL, &supported);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
        hr = ID3D11VideoDevice1_GetVideoProfileSupported(video_device1, &unknown_profile, NULL);
        ok(hr == E_INVALIDARG, "Got hr %#lx.\n", hr);
    }

    if (video_device1)
        ID3D11VideoDevice1_Release(video_device1);
    ID3D11VideoDevice_Release(video_device);
    refcount = ID3D11Device_Release(device);
    ok(!refcount, "Device has %lu references left.\n", refcount);
}

static void test_video_processor_format_support(void)
{
    ID3D11VideoDevice *video_device;
    ID3D11Device *device;
    ULONG refcount;
    UINT support;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    get_video_interfaces(device, &video_device, NULL);
    if (!video_device)
    {
        skip("Failed to get ID3D11VideoDevice.\n");
        ID3D11Device_Release(device);
        return;
    }

    /* The device must report video processor support for the formats the video
     * processor reports, so that CheckFormatSupport consumers agree with
     * CheckVideoProcessorFormat. */
    hr = ID3D11Device_CheckFormatSupport(device, DXGI_FORMAT_B8G8R8A8_UNORM, &support);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(support & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT, "Got support %#x.\n", support);
    ok(support & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT, "Got support %#x.\n", support);

    hr = ID3D11Device_CheckFormatSupport(device, DXGI_FORMAT_NV12, &support);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(support & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT, "Got support %#x.\n", support);
    ok(support & D3D11_FORMAT_SUPPORT_DECODER_OUTPUT, "Got support %#x.\n", support);

    hr = ID3D11Device_CheckFormatSupport(device, DXGI_FORMAT_R32G32_FLOAT, &support);
    ok(hr == S_OK || hr == E_FAIL, "Got hr %#lx.\n", hr);
    if (hr == S_OK)
        ok(!(support & (D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT
                | D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT)), "Got support %#x.\n", support);

    ID3D11VideoDevice_Release(video_device);
    refcount = ID3D11Device_Release(device);
    ok(!refcount, "Device has %lu references left.\n", refcount);
}

START_TEST(video)
{
    test_video_processor_enumerator();
    test_video_processor();
    test_video_processor_state();
    test_video_processor_blt();
    test_video_decoder_profile_support();
    test_video_processor_format_support();
}
