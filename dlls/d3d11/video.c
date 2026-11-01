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

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

/* The rate conversion and custom rate tables are deliberately minimal: index 0 is the
 * pass-through rate conversion, which is the only mode our blit path implements. */
#define D3D11_VP_RATE_CONVERSION_CAPS_COUNT 1
#define D3D11_VP_CUSTOM_RATE_COUNT 1

/* Rate conversion capability bits, from the D3D11 SDK's D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS. */
#define D3D11_VP_RATE_CONVERSION_PASSTHROUGH 0x00000001

UINT d3d11_video_processor_format_support(DXGI_FORMAT format)
{
    static const struct
    {
        DXGI_FORMAT format;
        UINT flags;
    }
    supported_formats[] =
    {
        {DXGI_FORMAT_B8G8R8A8_UNORM,      D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT},
        {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT},
        {DXGI_FORMAT_B8G8R8X8_UNORM,      D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT},
        {DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT},
        {DXGI_FORMAT_R8G8B8A8_UNORM,      D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT},
        {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT},
        {DXGI_FORMAT_R16G16B16A16_FLOAT,  D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT},
        {DXGI_FORMAT_R8G8_UNORM,          D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT},
        {DXGI_FORMAT_NV12,                D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT},
        {DXGI_FORMAT_YUY2,                D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT},
    };

    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(supported_formats); ++i)
    {
        if (supported_formats[i].format == format)
            return supported_formats[i].flags;
    }

    return 0;
}

static bool d3d11_video_processor_format_is_yuv(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_NV12 || format == DXGI_FORMAT_YUY2;
}

/* ID3D11VideoProcessorEnumerator methods */

static struct d3d_video_processor_enumerator *impl_from_ID3D11VideoProcessorEnumerator(
        ID3D11VideoProcessorEnumerator *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_video_processor_enumerator, ID3D11VideoProcessorEnumerator_iface);
}

static struct d3d_video_processor_enumerator *impl_from_ID3D11VideoProcessorEnumerator1(
        ID3D11VideoProcessorEnumerator1 *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_video_processor_enumerator, ID3D11VideoProcessorEnumerator1_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator_QueryInterface(
        ID3D11VideoProcessorEnumerator *iface, REFIID iid, void **out)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID3D11VideoProcessorEnumerator)
            || IsEqualGUID(iid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D11VideoProcessorEnumerator_AddRef(iface);
        *out = enumerator;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ID3D11VideoProcessorEnumerator1))
    {
        ID3D11VideoProcessorEnumerator_AddRef(iface);
        *out = &enumerator->ID3D11VideoProcessorEnumerator1_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d11_video_processor_enumerator_AddRef(ID3D11VideoProcessorEnumerator *iface)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator(iface);
    ULONG refcount = InterlockedIncrement(&enumerator->refcount);

    TRACE("%p increasing refcount to %lu.\n", enumerator, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d11_video_processor_enumerator_Release(ID3D11VideoProcessorEnumerator *iface)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator(iface);
    ULONG refcount = InterlockedDecrement(&enumerator->refcount);

    TRACE("%p decreasing refcount to %lu.\n", enumerator, refcount);

    if (!refcount)
    {
        ID3D11Device5_Release(&enumerator->device->ID3D11Device5_iface);
        wined3d_private_store_cleanup(&enumerator->private_store);
        free(enumerator);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d3d11_video_processor_enumerator_GetDevice(ID3D11VideoProcessorEnumerator *iface,
        ID3D11Device **device)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    *device = (ID3D11Device *)&enumerator->device->ID3D11Device5_iface;
    ID3D11Device5_AddRef(&enumerator->device->ID3D11Device5_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator_GetPrivateData(
        ID3D11VideoProcessorEnumerator *iface, REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d_get_private_data(&enumerator->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator_SetPrivateData(
        ID3D11VideoProcessorEnumerator *iface, REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d_set_private_data(&enumerator->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator_SetPrivateDataInterface(
        ID3D11VideoProcessorEnumerator *iface, REFGUID guid, const IUnknown *data)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return d3d_set_private_data_interface(&enumerator->private_store, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator_GetVideoProcessorContentDesc(
        ID3D11VideoProcessorEnumerator *iface, D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
        return E_INVALIDARG;

    *desc = enumerator->desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator_CheckVideoProcessorFormat(
        ID3D11VideoProcessorEnumerator *iface, DXGI_FORMAT format, UINT *flags)
{
    TRACE("iface %p, format %s, flags %p.\n", iface, debug_dxgi_format(format), flags);

    if (!flags)
        return E_INVALIDARG;

    *flags = d3d11_video_processor_format_support(format);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator_GetVideoProcessorCaps(
        ID3D11VideoProcessorEnumerator *iface, D3D11_VIDEO_PROCESSOR_CAPS *caps)
{
    TRACE("iface %p, caps %p.\n", iface, caps);

    if (!caps)
        return E_INVALIDARG;

    /* Only the features our blit path actually implements are reported: stream
     * constriction, and the pass-through rate conversion. Colour space conversion,
     * filters, rotation, luma key, alpha palette and stereo are not implemented, so
     * the corresponding capability bits are cleared. */
    caps->DeviceCaps = 0;
    caps->FeatureCaps = D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_CONSTRICTION;
    caps->FilterCaps = 0;
    caps->InputFormatCaps = 0;
    caps->AutoStreamCaps = 0;
    caps->StereoCaps = 0;
    caps->RateConversionCapsCount = D3D11_VP_RATE_CONVERSION_CAPS_COUNT;
    caps->MaxInputStreams = D3D11_VP_MAX_STREAMS;
    caps->MaxStreamStates = D3D11_VP_MAX_STREAMS;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator_GetVideoProcessorRateConversionCaps(
        ID3D11VideoProcessorEnumerator *iface, UINT type_index, D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *caps)
{
    TRACE("iface %p, type_index %u, caps %p.\n", iface, type_index, caps);

    if (!caps || type_index >= D3D11_VP_RATE_CONVERSION_CAPS_COUNT)
        return E_INVALIDARG;

    caps->PastFrames = 0;
    caps->FutureFrames = 0;
    caps->ProcessorCaps = D3D11_VP_RATE_CONVERSION_PASSTHROUGH;
    caps->ITelecineCaps = 0;
    caps->CustomRateCount = D3D11_VP_CUSTOM_RATE_COUNT;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator_GetVideoProcessorCustomRate(
        ID3D11VideoProcessorEnumerator *iface, UINT type_index, UINT custom_rate_index,
        D3D11_VIDEO_PROCESSOR_CUSTOM_RATE *rate)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator(iface);

    TRACE("iface %p, type_index %u, custom_rate_index %u, rate %p.\n", iface, type_index, custom_rate_index, rate);

    if (!rate || type_index >= D3D11_VP_RATE_CONVERSION_CAPS_COUNT
            || custom_rate_index >= D3D11_VP_CUSTOM_RATE_COUNT)
        return E_INVALIDARG;

    rate->CustomRate = enumerator->desc.OutputFrameRate;
    if (!rate->CustomRate.Denominator)
    {
        rate->CustomRate.Numerator = 1;
        rate->CustomRate.Denominator = 1;
    }
    rate->OutputFrames = 1;
    rate->InputInterlaced = false;
    rate->InputFramesOrFields = 1;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator_GetVideoProcessorFilterRange(
        ID3D11VideoProcessorEnumerator *iface, D3D11_VIDEO_PROCESSOR_FILTER filter,
        D3D11_VIDEO_PROCESSOR_FILTER_RANGE *range)
{
    static const struct
    {
        int minimum;
        int maximum;
        int default_value;
        float multiplier;
    }
    filter_ranges[] =
    {
        {0, 255, 128, 1.0f},  /* D3D11_VIDEO_PROCESSOR_FILTER_BRIGHTNESS */
        {0, 255, 128, 1.0f},  /* D3D11_VIDEO_PROCESSOR_FILTER_CONTRAST */
        {-180, 180, 0, 1.0f}, /* D3D11_VIDEO_PROCESSOR_FILTER_HUE */
        {0, 255, 128, 1.0f},  /* D3D11_VIDEO_PROCESSOR_FILTER_SATURATION */
        {0, 100, 0, 1.0f},    /* D3D11_VIDEO_PROCESSOR_FILTER_NOISE_REDUCTION */
        {0, 100, 0, 1.0f},    /* D3D11_VIDEO_PROCESSOR_FILTER_EDGE_ENHANCEMENT */
        {0, 100, 0, 1.0f},    /* D3D11_VIDEO_PROCESSOR_FILTER_ANAMORPHIC_SCALING */
        {0, 100, 0, 1.0f},    /* D3D11_VIDEO_PROCESSOR_FILTER_STEREO_ADJUSTMENT */
    };

    TRACE("iface %p, filter %#x, range %p.\n", iface, filter, range);

    if (!range || filter >= ARRAY_SIZE(filter_ranges))
        return E_INVALIDARG;

    range->Minimum = filter_ranges[filter].minimum;
    range->Maximum = filter_ranges[filter].maximum;
    range->Default = filter_ranges[filter].default_value;
    range->Multiplier = filter_ranges[filter].multiplier;

    return S_OK;
}

static const struct ID3D11VideoProcessorEnumeratorVtbl d3d11_video_processor_enumerator_vtbl =
{
    d3d11_video_processor_enumerator_QueryInterface,
    d3d11_video_processor_enumerator_AddRef,
    d3d11_video_processor_enumerator_Release,
    d3d11_video_processor_enumerator_GetDevice,
    d3d11_video_processor_enumerator_GetPrivateData,
    d3d11_video_processor_enumerator_SetPrivateData,
    d3d11_video_processor_enumerator_SetPrivateDataInterface,
    d3d11_video_processor_enumerator_GetVideoProcessorContentDesc,
    d3d11_video_processor_enumerator_CheckVideoProcessorFormat,
    d3d11_video_processor_enumerator_GetVideoProcessorCaps,
    d3d11_video_processor_enumerator_GetVideoProcessorRateConversionCaps,
    d3d11_video_processor_enumerator_GetVideoProcessorCustomRate,
    d3d11_video_processor_enumerator_GetVideoProcessorFilterRange,
};

/* ID3D11VideoProcessorEnumerator1 methods */

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_QueryInterface(
        ID3D11VideoProcessorEnumerator1 *iface, REFIID iid, void **out)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return IUnknown_QueryInterface(&enumerator->ID3D11VideoProcessorEnumerator_iface, iid, out);
}

static ULONG STDMETHODCALLTYPE d3d11_video_processor_enumerator1_AddRef(ID3D11VideoProcessorEnumerator1 *iface)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_AddRef(&enumerator->ID3D11VideoProcessorEnumerator_iface);
}

static ULONG STDMETHODCALLTYPE d3d11_video_processor_enumerator1_Release(ID3D11VideoProcessorEnumerator1 *iface)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_Release(&enumerator->ID3D11VideoProcessorEnumerator_iface);
}

static void STDMETHODCALLTYPE d3d11_video_processor_enumerator1_GetDevice(
        ID3D11VideoProcessorEnumerator1 *iface, ID3D11Device **device)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    d3d11_video_processor_enumerator_GetDevice(&enumerator->ID3D11VideoProcessorEnumerator_iface, device);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_GetPrivateData(
        ID3D11VideoProcessorEnumerator1 *iface, REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_GetPrivateData(&enumerator->ID3D11VideoProcessorEnumerator_iface,
            guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_SetPrivateData(
        ID3D11VideoProcessorEnumerator1 *iface, REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_SetPrivateData(&enumerator->ID3D11VideoProcessorEnumerator_iface,
            guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_SetPrivateDataInterface(
        ID3D11VideoProcessorEnumerator1 *iface, REFGUID guid, const IUnknown *data)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_SetPrivateDataInterface(
            &enumerator->ID3D11VideoProcessorEnumerator_iface, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_GetVideoProcessorContentDesc(
        ID3D11VideoProcessorEnumerator1 *iface, D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_GetVideoProcessorContentDesc(
            &enumerator->ID3D11VideoProcessorEnumerator_iface, desc);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_CheckVideoProcessorFormat(
        ID3D11VideoProcessorEnumerator1 *iface, DXGI_FORMAT format, UINT *flags)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_CheckVideoProcessorFormat(
            &enumerator->ID3D11VideoProcessorEnumerator_iface, format, flags);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_GetVideoProcessorCaps(
        ID3D11VideoProcessorEnumerator1 *iface, D3D11_VIDEO_PROCESSOR_CAPS *caps)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_GetVideoProcessorCaps(
            &enumerator->ID3D11VideoProcessorEnumerator_iface, caps);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_GetVideoProcessorRateConversionCaps(
        ID3D11VideoProcessorEnumerator1 *iface, UINT type_index,
        D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *caps)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_GetVideoProcessorRateConversionCaps(
            &enumerator->ID3D11VideoProcessorEnumerator_iface, type_index, caps);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_GetVideoProcessorCustomRate(
        ID3D11VideoProcessorEnumerator1 *iface, UINT type_index, UINT custom_rate_index,
        D3D11_VIDEO_PROCESSOR_CUSTOM_RATE *rate)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_GetVideoProcessorCustomRate(
            &enumerator->ID3D11VideoProcessorEnumerator_iface, type_index, custom_rate_index, rate);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_GetVideoProcessorFilterRange(
        ID3D11VideoProcessorEnumerator1 *iface, D3D11_VIDEO_PROCESSOR_FILTER filter,
        D3D11_VIDEO_PROCESSOR_FILTER_RANGE *range)
{
    struct d3d_video_processor_enumerator *enumerator = impl_from_ID3D11VideoProcessorEnumerator1(iface);

    return d3d11_video_processor_enumerator_GetVideoProcessorFilterRange(
            &enumerator->ID3D11VideoProcessorEnumerator_iface, filter, range);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_enumerator1_CheckVideoProcessorFormatConversion(
        ID3D11VideoProcessorEnumerator1 *iface, DXGI_FORMAT input_format,
        DXGI_COLOR_SPACE_TYPE input_colour_space, DXGI_FORMAT output_format,
        DXGI_COLOR_SPACE_TYPE output_colour_space, BOOL *supported)
{
    UINT input_flags, output_flags;

    TRACE("iface %p, input_format %s, input_colour_space %#x, output_format %s, output_colour_space %#x,"
            " supported %p.\n", iface, debug_dxgi_format(input_format), input_colour_space,
            debug_dxgi_format(output_format), output_colour_space, supported);

    if (!supported)
        return E_INVALIDARG;

    input_flags = d3d11_video_processor_format_support(input_format);
    output_flags = d3d11_video_processor_format_support(output_format);

    /* Our blit path is a wined3d surface blit. It copies within a format family, and
     * converts YUY2 to RGB, which is all wined3d's conversion blitter implements; NV12
     * is only readable through sampling, so NV12 -> RGB conversion is not supported. */
    *supported = false;
    if (!(input_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)
            || !(output_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))
        return S_OK;

    if (!d3d11_video_processor_format_is_yuv(input_format)
            && !d3d11_video_processor_format_is_yuv(output_format))
        *supported = true;
    else if (input_format == DXGI_FORMAT_YUY2 && output_format != DXGI_FORMAT_NV12)
        *supported = true;
    else if (input_format == output_format)
        *supported = true;

    return S_OK;
}

static const struct ID3D11VideoProcessorEnumerator1Vtbl d3d11_video_processor_enumerator1_vtbl =
{
    d3d11_video_processor_enumerator1_QueryInterface,
    d3d11_video_processor_enumerator1_AddRef,
    d3d11_video_processor_enumerator1_Release,
    d3d11_video_processor_enumerator1_GetDevice,
    d3d11_video_processor_enumerator1_GetPrivateData,
    d3d11_video_processor_enumerator1_SetPrivateData,
    d3d11_video_processor_enumerator1_SetPrivateDataInterface,
    d3d11_video_processor_enumerator1_GetVideoProcessorContentDesc,
    d3d11_video_processor_enumerator1_CheckVideoProcessorFormat,
    d3d11_video_processor_enumerator1_GetVideoProcessorCaps,
    d3d11_video_processor_enumerator1_GetVideoProcessorRateConversionCaps,
    d3d11_video_processor_enumerator1_GetVideoProcessorCustomRate,
    d3d11_video_processor_enumerator1_GetVideoProcessorFilterRange,
    d3d11_video_processor_enumerator1_CheckVideoProcessorFormatConversion,
};

HRESULT d3d_video_processor_enumerator_create(struct d3d_device *device,
        const D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc, struct d3d_video_processor_enumerator **enumerator)
{
    struct d3d_video_processor_enumerator *object;

    TRACE("device %p, desc %p, enumerator %p.\n", device, desc, enumerator);

    if (!desc || !enumerator)
        return E_INVALIDARG;

    if (!desc->InputWidth || !desc->InputHeight || !desc->OutputWidth || !desc->OutputHeight)
    {
        WARN("Invalid size %ux%u -> %ux%u.\n", desc->InputWidth, desc->InputHeight,
                desc->OutputWidth, desc->OutputHeight);
        return E_INVALIDARG;
    }

    if (desc->InputFrameFormat > D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST
            || desc->Usage > D3D11_VIDEO_USAGE_OPTIMAL_QUALITY)
    {
        WARN("Invalid frame format %#x, usage %#x.\n", desc->InputFrameFormat, desc->Usage);
        return E_INVALIDARG;
    }

    if (desc->InputWidth > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || desc->InputHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || desc->OutputWidth > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || desc->OutputHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
    {
        WARN("Size %ux%u -> %ux%u exceeds the texture limit.\n", desc->InputWidth, desc->InputHeight,
                desc->OutputWidth, desc->OutputHeight);
        return E_INVALIDARG;
    }

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID3D11VideoProcessorEnumerator_iface.lpVtbl = &d3d11_video_processor_enumerator_vtbl;
    object->ID3D11VideoProcessorEnumerator1_iface.lpVtbl = &d3d11_video_processor_enumerator1_vtbl;
    object->refcount = 1;
    object->desc = *desc;

    wined3d_private_store_init(&object->private_store);
    object->device = device;
    ID3D11Device5_AddRef(&device->ID3D11Device5_iface);

    TRACE("Created video processor enumerator %p.\n", object);
    *enumerator = object;
    return S_OK;
}

struct d3d_video_processor_enumerator *unsafe_impl_from_ID3D11VideoProcessorEnumerator(
        ID3D11VideoProcessorEnumerator *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d11_video_processor_enumerator_vtbl);

    return impl_from_ID3D11VideoProcessorEnumerator(iface);
}

/* ID3D11VideoProcessor methods */

static struct d3d_video_processor *impl_from_ID3D11VideoProcessor(ID3D11VideoProcessor *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_video_processor, ID3D11VideoProcessor_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_QueryInterface(ID3D11VideoProcessor *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID3D11VideoProcessor)
            || IsEqualGUID(iid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D11VideoProcessor_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d11_video_processor_AddRef(ID3D11VideoProcessor *iface)
{
    struct d3d_video_processor *processor = impl_from_ID3D11VideoProcessor(iface);
    ULONG refcount = InterlockedIncrement(&processor->refcount);

    TRACE("%p increasing refcount to %lu.\n", processor, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d11_video_processor_Release(ID3D11VideoProcessor *iface)
{
    struct d3d_video_processor *processor = impl_from_ID3D11VideoProcessor(iface);
    ULONG refcount = InterlockedDecrement(&processor->refcount);

    TRACE("%p decreasing refcount to %lu.\n", processor, refcount);

    if (!refcount)
    {
        ID3D11VideoProcessorEnumerator_Release(&processor->enumerator->ID3D11VideoProcessorEnumerator_iface);
        ID3D11Device5_Release(&processor->device->ID3D11Device5_iface);
        wined3d_private_store_cleanup(&processor->private_store);
        free(processor);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d3d11_video_processor_GetDevice(ID3D11VideoProcessor *iface, ID3D11Device **device)
{
    struct d3d_video_processor *processor = impl_from_ID3D11VideoProcessor(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    *device = (ID3D11Device *)&processor->device->ID3D11Device5_iface;
    ID3D11Device5_AddRef(&processor->device->ID3D11Device5_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_GetPrivateData(ID3D11VideoProcessor *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_video_processor *processor = impl_from_ID3D11VideoProcessor(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d_get_private_data(&processor->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_SetPrivateData(ID3D11VideoProcessor *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_video_processor *processor = impl_from_ID3D11VideoProcessor(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d_set_private_data(&processor->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_SetPrivateDataInterface(ID3D11VideoProcessor *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d_video_processor *processor = impl_from_ID3D11VideoProcessor(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return d3d_set_private_data_interface(&processor->private_store, guid, data);
}

static void STDMETHODCALLTYPE d3d11_video_processor_GetContentDesc(
        ID3D11VideoProcessor *iface, D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc)
{
    struct d3d_video_processor *processor = impl_from_ID3D11VideoProcessor(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
        return;

    *desc = processor->desc;
}

static void STDMETHODCALLTYPE d3d11_video_processor_GetRateConversionCaps(
        ID3D11VideoProcessor *iface, D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *caps)
{
    TRACE("iface %p, caps %p.\n", iface, caps);

    if (!caps)
        return;

    caps->PastFrames = 0;
    caps->FutureFrames = 0;
    caps->ProcessorCaps = D3D11_VP_RATE_CONVERSION_PASSTHROUGH;
    caps->ITelecineCaps = 0;
    caps->CustomRateCount = D3D11_VP_CUSTOM_RATE_COUNT;
}

static const struct ID3D11VideoProcessorVtbl d3d11_video_processor_vtbl =
{
    d3d11_video_processor_QueryInterface,
    d3d11_video_processor_AddRef,
    d3d11_video_processor_Release,
    d3d11_video_processor_GetDevice,
    d3d11_video_processor_GetPrivateData,
    d3d11_video_processor_SetPrivateData,
    d3d11_video_processor_SetPrivateDataInterface,
    d3d11_video_processor_GetContentDesc,
    d3d11_video_processor_GetRateConversionCaps,
};

HRESULT d3d_video_processor_create(struct d3d_device *device,
        struct d3d_video_processor_enumerator *enumerator, UINT rate_conversion_index,
        struct d3d_video_processor **processor)
{
    struct d3d_video_processor *object;
    unsigned int i;

    TRACE("device %p, enumerator %p, rate_conversion_index %u, processor %p.\n",
            device, enumerator, rate_conversion_index, processor);

    if (!enumerator || !processor)
        return E_INVALIDARG;

    if (rate_conversion_index >= D3D11_VP_RATE_CONVERSION_CAPS_COUNT)
    {
        WARN("Invalid rate conversion index %u.\n", rate_conversion_index);
        return E_INVALIDARG;
    }

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID3D11VideoProcessor_iface.lpVtbl = &d3d11_video_processor_vtbl;
    object->refcount = 1;
    object->desc = enumerator->desc;
    object->rate_conversion_index = rate_conversion_index;
    object->enumerator = enumerator;
    ID3D11VideoProcessorEnumerator_AddRef(&enumerator->ID3D11VideoProcessorEnumerator_iface);

    for (i = 0; i < D3D11_VP_MAX_STREAMS; ++i)
    {
        object->streams[i].alpha_fill_mode = D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_OPAQUE;
        object->streams[i].auto_processing_mode = true;
        object->streams[i].src_aspect_ratio.Numerator = 1;
        object->streams[i].src_aspect_ratio.Denominator = 1;
        object->streams[i].dst_aspect_ratio.Numerator = 1;
        object->streams[i].dst_aspect_ratio.Denominator = 1;
    }

    wined3d_private_store_init(&object->private_store);
    object->device = device;
    ID3D11Device5_AddRef(&device->ID3D11Device5_iface);

    TRACE("Created video processor %p.\n", object);
    *processor = object;
    return S_OK;
}

struct d3d_video_processor *unsafe_impl_from_ID3D11VideoProcessor(ID3D11VideoProcessor *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d11_video_processor_vtbl);

    return impl_from_ID3D11VideoProcessor(iface);
}

static unsigned int d3d11_video_processor_view_get_sub_resource_idx(struct wined3d_texture *texture,
        unsigned int mip_slice)
{
    unsigned int level_count = wined3d_texture_get_level_count(texture);

    if (mip_slice >= level_count)
        return ~0u;

    return mip_slice;
}

static HRESULT d3d11_video_processor_view_resolve_resource(ID3D11Resource *resource,
        unsigned int mip_slice, struct wined3d_texture **wined3d_texture, unsigned int *sub_resource_idx)
{
    struct wined3d_resource_desc desc;
    struct wined3d_resource *wined3d_resource;
    struct wined3d_texture *texture;
    unsigned int idx;

    if (!(wined3d_resource = wined3d_resource_from_d3d11_resource(resource)))
        return E_INVALIDARG;

    wined3d_resource_get_desc(wined3d_resource, &desc);
    if (desc.resource_type != WINED3D_RTYPE_TEXTURE_2D)
    {
        WARN("Resource is not a 2d texture, type %#x.\n", desc.resource_type);
        return E_INVALIDARG;
    }

    texture = wined3d_texture_from_resource(wined3d_resource);
    idx = d3d11_video_processor_view_get_sub_resource_idx(texture, mip_slice);
    if (idx == ~0u)
    {
        WARN("Invalid mip slice %u.\n", mip_slice);
        return E_INVALIDARG;
    }

    wined3d_texture_incref(texture);
    *wined3d_texture = texture;
    *sub_resource_idx = idx;
    return S_OK;
}

/* ID3D11VideoProcessorInputView methods */

static struct d3d_video_processor_input_view *impl_from_ID3D11VideoProcessorInputView(
        ID3D11VideoProcessorInputView *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_video_processor_input_view, ID3D11VideoProcessorInputView_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_input_view_QueryInterface(
        ID3D11VideoProcessorInputView *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID3D11VideoProcessorInputView)
            || IsEqualGUID(iid, &IID_ID3D11View)
            || IsEqualGUID(iid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D11VideoProcessorInputView_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d11_video_processor_input_view_AddRef(ID3D11VideoProcessorInputView *iface)
{
    struct d3d_video_processor_input_view *view = impl_from_ID3D11VideoProcessorInputView(iface);
    ULONG refcount = InterlockedIncrement(&view->refcount);

    TRACE("%p increasing refcount to %lu.\n", view, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d11_video_processor_input_view_Release(ID3D11VideoProcessorInputView *iface)
{
    struct d3d_video_processor_input_view *view = impl_from_ID3D11VideoProcessorInputView(iface);
    ULONG refcount = InterlockedDecrement(&view->refcount);

    TRACE("%p decreasing refcount to %lu.\n", view, refcount);

    if (!refcount)
    {
        wined3d_texture_decref(view->wined3d_texture);
        ID3D11VideoProcessorEnumerator_Release(&view->enumerator->ID3D11VideoProcessorEnumerator_iface);
        ID3D11Device5_Release(&view->device->ID3D11Device5_iface);
        wined3d_private_store_cleanup(&view->private_store);
        free(view);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d3d11_video_processor_input_view_GetDevice(ID3D11VideoProcessorInputView *iface,
        ID3D11Device **device)
{
    struct d3d_video_processor_input_view *view = impl_from_ID3D11VideoProcessorInputView(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    *device = (ID3D11Device *)&view->device->ID3D11Device5_iface;
    ID3D11Device5_AddRef(&view->device->ID3D11Device5_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_input_view_GetPrivateData(
        ID3D11VideoProcessorInputView *iface, REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_video_processor_input_view *view = impl_from_ID3D11VideoProcessorInputView(iface);

    return d3d_get_private_data(&view->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_input_view_SetPrivateData(
        ID3D11VideoProcessorInputView *iface, REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_video_processor_input_view *view = impl_from_ID3D11VideoProcessorInputView(iface);

    return d3d_set_private_data(&view->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_input_view_SetPrivateDataInterface(
        ID3D11VideoProcessorInputView *iface, REFGUID guid, const IUnknown *data)
{
    struct d3d_video_processor_input_view *view = impl_from_ID3D11VideoProcessorInputView(iface);

    return d3d_set_private_data_interface(&view->private_store, guid, data);
}

static void STDMETHODCALLTYPE d3d11_video_processor_input_view_GetResource(ID3D11VideoProcessorInputView *iface,
        ID3D11Resource **resource)
{
    struct d3d_video_processor_input_view *view = impl_from_ID3D11VideoProcessorInputView(iface);

    TRACE("iface %p, resource %p.\n", iface, resource);

    *resource = view->resource;
    ID3D11Resource_AddRef(*resource);
}

static void STDMETHODCALLTYPE d3d11_video_processor_input_view_GetDesc(ID3D11VideoProcessorInputView *iface,
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *desc)
{
    struct d3d_video_processor_input_view *view = impl_from_ID3D11VideoProcessorInputView(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
        return;

    *desc = view->desc;
}

static const struct ID3D11VideoProcessorInputViewVtbl d3d11_video_processor_input_view_vtbl =
{
    d3d11_video_processor_input_view_QueryInterface,
    d3d11_video_processor_input_view_AddRef,
    d3d11_video_processor_input_view_Release,
    d3d11_video_processor_input_view_GetDevice,
    d3d11_video_processor_input_view_GetPrivateData,
    d3d11_video_processor_input_view_SetPrivateData,
    d3d11_video_processor_input_view_SetPrivateDataInterface,
    d3d11_video_processor_input_view_GetResource,
    d3d11_video_processor_input_view_GetDesc,
};

HRESULT d3d_video_processor_input_view_create(struct d3d_device *device, ID3D11Resource *resource,
        struct d3d_video_processor_enumerator *enumerator, const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *desc,
        struct d3d_video_processor_input_view **view)
{
    struct wined3d_texture *wined3d_texture;
    struct d3d_video_processor_input_view *object;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC d;
    unsigned int sub_resource_idx;
    HRESULT hr;

    TRACE("device %p, resource %p, enumerator %p, desc %p, view %p.\n", device, resource, enumerator, desc, view);

    if (!resource || !enumerator)
        return E_INVALIDARG;

    d = desc ? *desc : (D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC){0};
    if (d.ViewDimension != D3D11_VPIV_DIMENSION_TEXTURE2D)
    {
        WARN("Invalid view dimension %#x.\n", d.ViewDimension);
        return E_INVALIDARG;
    }

    if (FAILED(hr = d3d11_video_processor_view_resolve_resource(resource, d.Texture2D.MipSlice,
            &wined3d_texture, &sub_resource_idx)))
        return hr;

    if (!(object = calloc(1, sizeof(*object))))
    {
        wined3d_texture_decref(wined3d_texture);
        return E_OUTOFMEMORY;
    }

    object->ID3D11VideoProcessorInputView_iface.lpVtbl = &d3d11_video_processor_input_view_vtbl;
    object->refcount = 1;
    object->desc = d;
    object->resource = resource;
    object->wined3d_texture = wined3d_texture;
    object->sub_resource_idx = sub_resource_idx;
    object->enumerator = enumerator;
    ID3D11VideoProcessorEnumerator_AddRef(&enumerator->ID3D11VideoProcessorEnumerator_iface);

    wined3d_private_store_init(&object->private_store);
    object->device = device;
    ID3D11Device5_AddRef(&device->ID3D11Device5_iface);

    TRACE("Created video processor input view %p.\n", object);
    *view = object;
    return S_OK;
}

struct d3d_video_processor_input_view *unsafe_impl_from_ID3D11VideoProcessorInputView(
        ID3D11VideoProcessorInputView *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d11_video_processor_input_view_vtbl);

    return impl_from_ID3D11VideoProcessorInputView(iface);
}

/* ID3D11VideoProcessorOutputView methods */

static struct d3d_video_processor_output_view *impl_from_ID3D11VideoProcessorOutputView(
        ID3D11VideoProcessorOutputView *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_video_processor_output_view, ID3D11VideoProcessorOutputView_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_output_view_QueryInterface(
        ID3D11VideoProcessorOutputView *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID3D11VideoProcessorOutputView)
            || IsEqualGUID(iid, &IID_ID3D11View)
            || IsEqualGUID(iid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D11VideoProcessorOutputView_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d11_video_processor_output_view_AddRef(ID3D11VideoProcessorOutputView *iface)
{
    struct d3d_video_processor_output_view *view = impl_from_ID3D11VideoProcessorOutputView(iface);
    ULONG refcount = InterlockedIncrement(&view->refcount);

    TRACE("%p increasing refcount to %lu.\n", view, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d11_video_processor_output_view_Release(ID3D11VideoProcessorOutputView *iface)
{
    struct d3d_video_processor_output_view *view = impl_from_ID3D11VideoProcessorOutputView(iface);
    ULONG refcount = InterlockedDecrement(&view->refcount);

    TRACE("%p decreasing refcount to %lu.\n", view, refcount);

    if (!refcount)
    {
        wined3d_texture_decref(view->wined3d_texture);
        ID3D11VideoProcessorEnumerator_Release(&view->enumerator->ID3D11VideoProcessorEnumerator_iface);
        ID3D11Device5_Release(&view->device->ID3D11Device5_iface);
        wined3d_private_store_cleanup(&view->private_store);
        free(view);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d3d11_video_processor_output_view_GetDevice(
        ID3D11VideoProcessorOutputView *iface, ID3D11Device **device)
{
    struct d3d_video_processor_output_view *view = impl_from_ID3D11VideoProcessorOutputView(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    *device = (ID3D11Device *)&view->device->ID3D11Device5_iface;
    ID3D11Device5_AddRef(&view->device->ID3D11Device5_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_output_view_GetPrivateData(
        ID3D11VideoProcessorOutputView *iface, REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_video_processor_output_view *view = impl_from_ID3D11VideoProcessorOutputView(iface);

    return d3d_get_private_data(&view->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_output_view_SetPrivateData(
        ID3D11VideoProcessorOutputView *iface, REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_video_processor_output_view *view = impl_from_ID3D11VideoProcessorOutputView(iface);

    return d3d_set_private_data(&view->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d11_video_processor_output_view_SetPrivateDataInterface(
        ID3D11VideoProcessorOutputView *iface, REFGUID guid, const IUnknown *data)
{
    struct d3d_video_processor_output_view *view = impl_from_ID3D11VideoProcessorOutputView(iface);

    return d3d_set_private_data_interface(&view->private_store, guid, data);
}

static void STDMETHODCALLTYPE d3d11_video_processor_output_view_GetResource(
        ID3D11VideoProcessorOutputView *iface, ID3D11Resource **resource)
{
    struct d3d_video_processor_output_view *view = impl_from_ID3D11VideoProcessorOutputView(iface);

    TRACE("iface %p, resource %p.\n", iface, resource);

    *resource = view->resource;
    ID3D11Resource_AddRef(*resource);
}

static void STDMETHODCALLTYPE d3d11_video_processor_output_view_GetDesc(
        ID3D11VideoProcessorOutputView *iface, D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *desc)
{
    struct d3d_video_processor_output_view *view = impl_from_ID3D11VideoProcessorOutputView(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
        return;

    *desc = view->desc;
}

static const struct ID3D11VideoProcessorOutputViewVtbl d3d11_video_processor_output_view_vtbl =
{
    d3d11_video_processor_output_view_QueryInterface,
    d3d11_video_processor_output_view_AddRef,
    d3d11_video_processor_output_view_Release,
    d3d11_video_processor_output_view_GetDevice,
    d3d11_video_processor_output_view_GetPrivateData,
    d3d11_video_processor_output_view_SetPrivateData,
    d3d11_video_processor_output_view_SetPrivateDataInterface,
    d3d11_video_processor_output_view_GetResource,
    d3d11_video_processor_output_view_GetDesc,
};

HRESULT d3d_video_processor_output_view_create(struct d3d_device *device, ID3D11Resource *resource,
        struct d3d_video_processor_enumerator *enumerator, const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *desc,
        struct d3d_video_processor_output_view **view)
{
    struct wined3d_texture *wined3d_texture;
    struct d3d_video_processor_output_view *object;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC d;
    unsigned int sub_resource_idx;
    HRESULT hr;

    TRACE("device %p, resource %p, enumerator %p, desc %p, view %p.\n", device, resource, enumerator, desc, view);

    if (!resource || !enumerator)
        return E_INVALIDARG;

    d = desc ? *desc : (D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC){0};
    if (d.ViewDimension != D3D11_VPOV_DIMENSION_TEXTURE2D)
    {
        WARN("Invalid view dimension %#x.\n", d.ViewDimension);
        return E_INVALIDARG;
    }

    if (FAILED(hr = d3d11_video_processor_view_resolve_resource(resource, d.Texture2D.MipSlice,
            &wined3d_texture, &sub_resource_idx)))
        return hr;

    if (!(object = calloc(1, sizeof(*object))))
    {
        wined3d_texture_decref(wined3d_texture);
        return E_OUTOFMEMORY;
    }

    object->ID3D11VideoProcessorOutputView_iface.lpVtbl = &d3d11_video_processor_output_view_vtbl;
    object->refcount = 1;
    object->desc = d;
    object->resource = resource;
    object->wined3d_texture = wined3d_texture;
    object->sub_resource_idx = sub_resource_idx;
    object->enumerator = enumerator;
    ID3D11VideoProcessorEnumerator_AddRef(&enumerator->ID3D11VideoProcessorEnumerator_iface);

    wined3d_private_store_init(&object->private_store);
    object->device = device;
    ID3D11Device5_AddRef(&device->ID3D11Device5_iface);

    TRACE("Created video processor output view %p.\n", object);
    *view = object;
    return S_OK;
}

struct d3d_video_processor_output_view *unsafe_impl_from_ID3D11VideoProcessorOutputView(
        ID3D11VideoProcessorOutputView *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d11_video_processor_output_view_vtbl);

    return impl_from_ID3D11VideoProcessorOutputView(iface);
}

static unsigned int d3d11_rect_width(const RECT *rect)
{
    return rect->right - rect->left;
}

static unsigned int d3d11_rect_height(const RECT *rect)
{
    return rect->bottom - rect->top;
}

static void d3d11_video_processor_get_surface_rect(struct wined3d_texture *texture,
        unsigned int sub_resource_idx, RECT *rect)
{
    struct wined3d_sub_resource_desc desc;

    wined3d_texture_get_sub_resource_desc(texture, sub_resource_idx, &desc);
    SetRect(rect, 0, 0, desc.width, desc.height);
}

static void d3d11_video_processor_clip_rect(RECT *rect, const RECT *bounds)
{
    if (rect->left < bounds->left)
        rect->left = bounds->left;
    if (rect->top < bounds->top)
        rect->top = bounds->top;
    if (rect->right > bounds->right)
        rect->right = bounds->right;
    if (rect->bottom > bounds->bottom)
        rect->bottom = bounds->bottom;
    if (rect->right < rect->left)
        rect->right = rect->left;
    if (rect->bottom < rect->top)
        rect->bottom = rect->top;
}

HRESULT d3d_video_processor_blt(struct d3d11_device_context *context, struct d3d_video_processor *processor,
        struct d3d_video_processor_output_view *output_view, UINT stream_count,
        const D3D11_VIDEO_PROCESSOR_STREAM *streams)
{
    struct wined3d_texture *dst_texture = output_view->wined3d_texture;
    unsigned int dst_sub_resource_idx = output_view->sub_resource_idx;
    RECT dst_bounds, src_bounds, dst_rect, src_rect;
    struct d3d_video_processor_stream_state *state;
    HRESULT hr;
    unsigned int i;

    d3d11_video_processor_get_surface_rect(dst_texture, dst_sub_resource_idx, &dst_bounds);

    /* Output constriction limits the destination area, as reported by
     * VideoProcessorSetOutputConstriction(). */
    if (processor->has_output_constriction)
    {
        dst_bounds.right = processor->output_constriction_size.cx;
        dst_bounds.bottom = processor->output_constriction_size.cy;
        d3d11_video_processor_clip_rect(&dst_bounds, &(RECT){0, 0, dst_bounds.right, dst_bounds.bottom});
    }

    for (i = 0; i < stream_count; ++i)
    {
        const D3D11_VIDEO_PROCESSOR_STREAM *stream = &streams[i];
        struct d3d_video_processor_input_view *input_view;
        enum wined3d_texture_filter_type filter;
        struct wined3d_texture *src_texture;

        if (!stream->Enable)
            continue;

        if (!(input_view = unsafe_impl_from_ID3D11VideoProcessorInputView(stream->pInputSurface)))
            return E_INVALIDARG;

        if (stream->OutputIndex >= D3D11_VP_MAX_STREAMS)
            return E_INVALIDARG;

        state = &processor->streams[stream->OutputIndex];
        src_texture = input_view->wined3d_texture;

        d3d11_video_processor_get_surface_rect(src_texture, input_view->sub_resource_idx, &src_bounds);
        src_rect = state->has_source_rect ? state->source_rect : src_bounds;
        d3d11_video_processor_clip_rect(&src_rect, &src_bounds);

        if (state->has_dest_rect)
            dst_rect = state->dest_rect;
        else if (processor->has_output_target_rect)
            dst_rect = processor->output_target_rect;
        else
            dst_rect = dst_bounds;
        d3d11_video_processor_clip_rect(&dst_rect, &dst_bounds);

        if (!d3d11_rect_width(&src_rect) || !d3d11_rect_height(&src_rect)
                || !d3d11_rect_width(&dst_rect) || !d3d11_rect_height(&dst_rect))
            continue;

        /* Nearest for a 1:1 copy, bilinear when stretching. Filters, rotation, colour
         * space conversion, luma key, alpha fill and stereo modes are recorded in the
         * stream state but are not applied by this path; NV12 sources cannot be
         * converted by wined3d's blitter and surface as a plain plane pair. */
        filter = (d3d11_rect_width(&src_rect) == d3d11_rect_width(&dst_rect)
                && d3d11_rect_height(&src_rect) == d3d11_rect_height(&dst_rect))
                ? WINED3D_TEXF_POINT : WINED3D_TEXF_LINEAR;

        hr = wined3d_device_context_blt(context->wined3d_context, dst_texture, dst_sub_resource_idx, &dst_rect,
                src_texture, input_view->sub_resource_idx, &src_rect, 0, NULL, filter);
        if (FAILED(hr))
        {
            WARN("Failed to blit stream %u, hr %#lx.\n", i, hr);
            return hr;
        }
    }

    return S_OK;
}
