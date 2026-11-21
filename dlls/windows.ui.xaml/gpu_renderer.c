/*
 * GPU-Native XAML Instanced SDF Uber-Shader + Dynamic Glyph/Image Atlas
 *
 * Copyright 2026 Wine XAML Contributors
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
#include <d3dcompiler.h>
#include "weakreference.h"
#include "uwp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xaml);

#define MAX_GPU_INSTANCES 4096
#define ATLAS_SIZE        2048
#define ATLAS_HASH_SIZE   2048
#define FONT_CACHE_SIZE   24
#define SCRATCH_MAX_W     1920
#define SCRATCH_MAX_H     512

struct xaml_gpu_instance
{
    float rect[4];         /* x0, y0, x1, y1 in pixel coordinates */
    float clip[4];         /* clip_x0, clip_y0, clip_x1, clip_y1 */
    float radii[4];        /* tl, tr, br, bl corner radii */
    float fill_color[4];   /* straight RGBA 0..1 */
    float border_color[4]; /* straight RGBA 0..1 */
    float uv[4];           /* u0, v0, u1, v1 in atlas */
    float params[4];       /* [0]=border_thickness, [1]=mode (0=SDF, 1=atlas, 2=ring), [2]=opacity, [3]=reserved */
};

struct xaml_gpu_constants
{
    float viewport_size[2];
    float pad[2];
};

struct atlas_entry
{
    BOOL occupied;
    BOOL is_image;
    UINT64 hash;
    UINT64 img_key;
    int font_height;
    int font_weight;
    UINT dt_flags;
    UINT box_w;
    UINT box_h;
    WCHAR text[512];
    float uv[4];
};

struct cached_font
{
    int height;
    int weight;
    HFONT hfont;
};

typedef HRESULT (WINAPI *pfn_D3DCompile)( LPCVOID src_data, SIZE_T src_data_size,
                                          LPCSTR source_name, const D3D_SHADER_MACRO *defines,
                                          ID3DInclude *include, LPCSTR entrypoint,
                                          LPCSTR target, UINT flags1, UINT flags2,
                                          ID3DBlob **code, ID3DBlob **error_msgs );

static struct
{
    BOOL initialized;
    BOOL init_failed;
    ID3D11Device *dev;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *layout;
    ID3D11Buffer *cb;
    ID3D11Buffer *instance_vb;
    ID3D11BlendState *blend_state;
    ID3D11RasterizerState *rast_state;
    ID3D11SamplerState *sampler;
    ID3D11Texture2D *atlas_tex;
    ID3D11ShaderResourceView *atlas_srv;

    ID3D11RenderTargetView *rtv;
    IDXGISwapChain1 *rtv_swapchain;
    UINT rtv_width;
    UINT rtv_height;

    UINT shelf_x;
    UINT shelf_y;
    UINT shelf_h;
    struct atlas_entry hash_table[ATLAS_HASH_SIZE];

    HDC scratch_dc;
    HBITMAP scratch_bmp;
    HBITMAP scratch_old_bmp;
    UINT32 *scratch_bits;
    struct cached_font fonts[FONT_CACHE_SIZE];
    int font_cursor;

    struct xaml_gpu_instance instances[MAX_GPU_INSTANCES];
    UINT instance_count;
    UINT frame_width;
    UINT frame_height;
    UINT atlas_hits;
    UINT atlas_misses;
} g_gpu;

static const char xaml_uber_hlsl[] =
    "cbuffer Constants : register(b0)\n"
    "{\n"
    "    float2 viewport_size;\n"
    "    float2 pad;\n"
    "};\n"
    "struct VS_INPUT\n"
    "{\n"
    "    uint vid : SV_VertexID;\n"
    "    float4 rect : RECT;\n"
    "    float4 clip : CLIP;\n"
    "    float4 radii : RADII;\n"
    "    float4 fill_color : FILL;\n"
    "    float4 border_color : BORDER;\n"
    "    float4 uv : UV;\n"
    "    float4 params : PARAMS;\n"
    "};\n"
    "struct PS_INPUT\n"
    "{\n"
    "    float4 pos : SV_Position;\n"
    "    float2 p_pos : TEXCOORD0;\n"
    "    float4 rect : TEXCOORD1;\n"
    "    float4 clip : TEXCOORD2;\n"
    "    float4 radii : TEXCOORD3;\n"
    "    float4 fill_color : COLOR0;\n"
    "    float4 border_color : COLOR1;\n"
    "    float4 uv_coord : TEXCOORD4;\n"
    "    float4 params : TEXCOORD5;\n"
    "};\n"
    "PS_INPUT vs_main(VS_INPUT inp)\n"
    "{\n"
    "    PS_INPUT o;\n"
    "    float2 corner = float2((inp.vid & 1) ? 1.0 : 0.0, (inp.vid & 2) ? 1.0 : 0.0);\n"
    "    float expand = (inp.params.y < 0.5 || inp.params.y > 1.5) ? 1.0 : 0.0;\n"
    "    float2 r0 = inp.rect.xy - expand;\n"
    "    float2 r1 = inp.rect.zw + expand;\n"
    "    float2 px = lerp(r0, r1, corner);\n"
    "    o.pos = float4(px / viewport_size * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
    "    o.p_pos = px;\n"
    "    o.rect = inp.rect;\n"
    "    o.clip = inp.clip;\n"
    "    o.radii = inp.radii;\n"
    "    o.fill_color = inp.fill_color;\n"
    "    o.border_color = inp.border_color;\n"
    "    o.uv_coord = float4(lerp(inp.uv.xy, inp.uv.zw, corner), 0.0, 0.0);\n"
    "    o.params = inp.params;\n"
    "    return o;\n"
    "}\n"
    "Texture2D atlas_tex : register(t0);\n"
    "SamplerState atlas_samp : register(s0);\n"
    "float sdRoundBox(float2 p, float2 b, float4 r)\n"
    "{\n"
    "    float2 rr = (p.x > 0.0) ? r.yz : r.xw;\n"
    "    float rad = (p.y > 0.0) ? rr.y : rr.x;\n"
    "    float2 q = abs(p) - b + rad;\n"
    "    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rad;\n"
    "}\n"
    "float4 ps_main(PS_INPUT inp) : SV_Target\n"
    "{\n"
    "    if (inp.p_pos.x < inp.clip.x || inp.p_pos.y < inp.clip.y ||\n"
    "        inp.p_pos.x > inp.clip.z || inp.p_pos.y > inp.clip.w)\n"
    "        discard;\n"
    "    float mode = inp.params.y;\n"
    "    float opacity = inp.params.z;\n"
    "    if (mode < 0.5)\n"
    "    {\n"
    "        float2 center = 0.5 * (inp.rect.xy + inp.rect.zw);\n"
    "        float2 half_ext = 0.5 * (inp.rect.zw - inp.rect.xy);\n"
    "        float2 p = inp.p_pos - center;\n"
    "        float d = sdRoundBox(p, half_ext, inp.radii);\n"
    "        float outer_alpha = saturate(0.5 - d);\n"
    "        if (outer_alpha <= 0.0) discard;\n"
    "        float bw = inp.params.x;\n"
    "        if (bw > 0.0 && inp.border_color.a > 0.0)\n"
    "        {\n"
    "            float inner_alpha = saturate(0.5 - (d + bw));\n"
    "            float4 fill_pm = float4(inp.fill_color.rgb * inp.fill_color.a, inp.fill_color.a);\n"
    "            float4 bord_pm = float4(inp.border_color.rgb * inp.border_color.a, inp.border_color.a);\n"
    "            float4 mixed = lerp(bord_pm, fill_pm, inner_alpha);\n"
    "            float scale = outer_alpha * opacity;\n"
    "            return float4(mixed.rgb * scale, mixed.a * scale);\n"
    "        }\n"
    "        else\n"
    "        {\n"
    "            float a = inp.fill_color.a * outer_alpha * opacity;\n"
    "            return float4(inp.fill_color.rgb * a, a);\n"
    "        }\n"
    "    }\n"
    "    else if (mode < 1.5)\n"
    "    {\n"
    "        float4 tex = atlas_tex.Sample(atlas_samp, inp.uv_coord.xy);\n"
    "        float a = tex.a * inp.fill_color.a * opacity;\n"
    "        if (a <= 0.001) discard;\n"
    "        return float4(tex.rgb * inp.fill_color.rgb * a, a);\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        float2 center = 0.5 * (inp.rect.xy + inp.rect.zw);\n"
    "        float2 half_ext = 0.5 * (inp.rect.zw - inp.rect.xy);\n"
    "        float r_outer = min(half_ext.x, half_ext.y);\n"
    "        float bw = max(1.0, inp.params.x);\n"
    "        float r_inner = max(0.0, r_outer - bw);\n"
    "        float2 p = inp.p_pos - center;\n"
    "        float dist = length(p);\n"
    "        float ring_a = saturate(0.5 - (dist - r_outer)) * saturate(0.5 + (dist - r_inner));\n"
    "        if (ring_a <= 0.001) discard;\n"
    "        if (inp.params.w > 0.5)\n"
    "        {\n"
    "            /* Top-left quadrant arc */\n"
    "            if (p.x > 0.0 || p.y > 0.0) discard;\n"
    "        }\n"
    "        float a = inp.fill_color.a * ring_a * opacity;\n"
    "        return float4(inp.fill_color.rgb * a, a);\n"
    "    }\n"
    "}\n";

static UINT64 fnv1a_bytes( const void *data, size_t len, UINT64 seed )
{
    const BYTE *p = (const BYTE *)data;
    UINT64 h = seed ? seed : 14695981039346656037ULL;
    size_t i;
    for (i = 0; i < len; i++)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void rgba_to_float4( UINT32 argb, float out[4] )
{
    out[0] = (float)((argb >> 16) & 0xff) / 255.0f;
    out[1] = (float)((argb >> 8) & 0xff) / 255.0f;
    out[2] = (float)(argb & 0xff) / 255.0f;
    out[3] = (float)((argb >> 24) & 0xff) / 255.0f;
}

void xaml_gpu_release_rtv( void )
{
    if (g_gpu.dev)
    {
        ID3D11DeviceContext *ctx = NULL;
        ID3D11Device_GetImmediateContext( g_gpu.dev, &ctx );
        if (ctx)
        {
            ID3D11DeviceContext_OMSetRenderTargets( ctx, 0, NULL, NULL );
            ID3D11DeviceContext_Flush( ctx );
            ID3D11DeviceContext_Release( ctx );
        }
    }
    if (g_gpu.rtv)
    {
        ID3D11RenderTargetView_Release( g_gpu.rtv );
        g_gpu.rtv = NULL;
    }
    g_gpu.rtv_swapchain = NULL;
    g_gpu.rtv_width = 0;
    g_gpu.rtv_height = 0;
}

void xaml_gpu_shutdown( void )
{
    int i;
    xaml_gpu_release_rtv();
    if (g_gpu.atlas_srv) { ID3D11ShaderResourceView_Release( g_gpu.atlas_srv ); g_gpu.atlas_srv = NULL; }
    if (g_gpu.atlas_tex) { ID3D11Texture2D_Release( g_gpu.atlas_tex ); g_gpu.atlas_tex = NULL; }
    if (g_gpu.sampler) { ID3D11SamplerState_Release( g_gpu.sampler ); g_gpu.sampler = NULL; }
    if (g_gpu.rast_state) { ID3D11RasterizerState_Release( g_gpu.rast_state ); g_gpu.rast_state = NULL; }
    if (g_gpu.blend_state) { ID3D11BlendState_Release( g_gpu.blend_state ); g_gpu.blend_state = NULL; }
    if (g_gpu.instance_vb) { ID3D11Buffer_Release( g_gpu.instance_vb ); g_gpu.instance_vb = NULL; }
    if (g_gpu.cb) { ID3D11Buffer_Release( g_gpu.cb ); g_gpu.cb = NULL; }
    if (g_gpu.layout) { ID3D11InputLayout_Release( g_gpu.layout ); g_gpu.layout = NULL; }
    if (g_gpu.ps) { ID3D11PixelShader_Release( g_gpu.ps ); g_gpu.ps = NULL; }
    if (g_gpu.vs) { ID3D11VertexShader_Release( g_gpu.vs ); g_gpu.vs = NULL; }
    if (g_gpu.scratch_dc)
    {
        if (g_gpu.scratch_old_bmp) SelectObject( g_gpu.scratch_dc, g_gpu.scratch_old_bmp );
        if (g_gpu.scratch_bmp) DeleteObject( g_gpu.scratch_bmp );
        DeleteDC( g_gpu.scratch_dc );
        g_gpu.scratch_dc = NULL;
        g_gpu.scratch_bmp = NULL;
        g_gpu.scratch_old_bmp = NULL;
        g_gpu.scratch_bits = NULL;
    }
    for (i = 0; i < FONT_CACHE_SIZE; i++)
    {
        if (g_gpu.fonts[i].hfont)
        {
            DeleteObject( g_gpu.fonts[i].hfont );
            g_gpu.fonts[i].hfont = NULL;
        }
    }
    g_gpu.dev = NULL;
    g_gpu.initialized = FALSE;
    g_gpu.init_failed = FALSE;
}

static BOOL xaml_gpu_init( ID3D11Device *dev )
{
    static const D3D11_INPUT_ELEMENT_DESC input_desc[] =
    {
        { "RECT",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(struct xaml_gpu_instance, rect),         D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "CLIP",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(struct xaml_gpu_instance, clip),         D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "RADII",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(struct xaml_gpu_instance, radii),        D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "FILL",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(struct xaml_gpu_instance, fill_color),   D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "BORDER", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(struct xaml_gpu_instance, border_color), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "UV",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(struct xaml_gpu_instance, uv),           D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "PARAMS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(struct xaml_gpu_instance, params),       D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    };
    HMODULE mod;
    pfn_D3DCompile compile_fn;
    ID3DBlob *vs_blob = NULL, *ps_blob = NULL, *err_blob = NULL;
    HRESULT hr;

    if (g_gpu.initialized && g_gpu.dev == dev) return TRUE;
    if (g_gpu.init_failed && g_gpu.dev == dev) return FALSE;
    if (g_gpu.initialized && g_gpu.dev != dev) xaml_gpu_shutdown();

    g_gpu.dev = dev;

    mod = LoadLibraryW( L"d3dcompiler_47.dll" );
    if (!mod)
    {
        WARN( "xaml_gpu_init: d3dcompiler_47.dll not found\n" );
        g_gpu.init_failed = TRUE;
        return FALSE;
    }
    compile_fn = (pfn_D3DCompile)GetProcAddress( mod, "D3DCompile" );
    if (!compile_fn)
    {
        WARN( "xaml_gpu_init: D3DCompile missing\n" );
        g_gpu.init_failed = TRUE;
        return FALSE;
    }

    hr = compile_fn( xaml_uber_hlsl, sizeof(xaml_uber_hlsl) - 1, "xaml_uber.hlsl", NULL, NULL,
                     "vs_main", "vs_4_0", 0, 0, &vs_blob, &err_blob );
    if (FAILED(hr) || !vs_blob)
    {
        WARN( "xaml_gpu_init: VS compile failed hr=%#lx: %s\n", hr,
              err_blob ? (const char *)ID3D10Blob_GetBufferPointer( err_blob ) : "" );
        if (err_blob) ID3D10Blob_Release( err_blob );
        g_gpu.init_failed = TRUE;
        return FALSE;
    }
    if (err_blob) { ID3D10Blob_Release( err_blob ); err_blob = NULL; }

    hr = compile_fn( xaml_uber_hlsl, sizeof(xaml_uber_hlsl) - 1, "xaml_uber.hlsl", NULL, NULL,
                     "ps_main", "ps_4_0", 0, 0, &ps_blob, &err_blob );
    if (FAILED(hr) || !ps_blob)
    {
        WARN( "xaml_gpu_init: PS compile failed hr=%#lx: %s\n", hr,
              err_blob ? (const char *)ID3D10Blob_GetBufferPointer( err_blob ) : "" );
        if (err_blob) ID3D10Blob_Release( err_blob );
        ID3D10Blob_Release( vs_blob );
        g_gpu.init_failed = TRUE;
        return FALSE;
    }
    if (err_blob) { ID3D10Blob_Release( err_blob ); err_blob = NULL; }

    hr = ID3D11Device_CreateVertexShader( dev, ID3D10Blob_GetBufferPointer( vs_blob ),
                                          ID3D10Blob_GetBufferSize( vs_blob ), NULL, &g_gpu.vs );
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateInputLayout( dev, input_desc, ARRAY_SIZE(input_desc),
                                             ID3D10Blob_GetBufferPointer( vs_blob ),
                                             ID3D10Blob_GetBufferSize( vs_blob ), &g_gpu.layout );
    ID3D10Blob_Release( vs_blob );

    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreatePixelShader( dev, ID3D10Blob_GetBufferPointer( ps_blob ),
                                             ID3D10Blob_GetBufferSize( ps_blob ), NULL, &g_gpu.ps );
    ID3D10Blob_Release( ps_blob );

    if (FAILED(hr))
    {
        WARN( "xaml_gpu_init: shader/layout creation failed hr=%#lx\n", hr );
        xaml_gpu_shutdown();
        g_gpu.init_failed = TRUE;
        return FALSE;
    }

    {
        D3D11_BUFFER_DESC cbd = {0};
        cbd.ByteWidth = sizeof(struct xaml_gpu_constants);
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = ID3D11Device_CreateBuffer( dev, &cbd, NULL, &g_gpu.cb );
    }
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC vbd = {0};
        vbd.ByteWidth = sizeof(struct xaml_gpu_instance) * MAX_GPU_INSTANCES;
        vbd.Usage = D3D11_USAGE_DYNAMIC;
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = ID3D11Device_CreateBuffer( dev, &vbd, NULL, &g_gpu.instance_vb );
    }
    if (SUCCEEDED(hr))
    {
        D3D11_BLEND_DESC bd = {0};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = ID3D11Device_CreateBlendState( dev, &bd, &g_gpu.blend_state );
    }
    if (SUCCEEDED(hr))
    {
        D3D11_RASTERIZER_DESC rd = {0};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        hr = ID3D11Device_CreateRasterizerState( dev, &rd, &g_gpu.rast_state );
    }
    if (SUCCEEDED(hr))
    {
        D3D11_SAMPLER_DESC sd = {0};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = ID3D11Device_CreateSamplerState( dev, &sd, &g_gpu.sampler );
    }
    if (SUCCEEDED(hr))
    {
        D3D11_TEXTURE2D_DESC td = {0};
        td.Width = ATLAS_SIZE;
        td.Height = ATLAS_SIZE;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        hr = ID3D11Device_CreateTexture2D( dev, &td, NULL, &g_gpu.atlas_tex );
        if (SUCCEEDED(hr))
            hr = ID3D11Device_CreateShaderResourceView( dev, (ID3D11Resource *)g_gpu.atlas_tex, NULL, &g_gpu.atlas_srv );
    }
    if (SUCCEEDED(hr))
    {
        BITMAPINFO bmi = {0};
        HDC screen_dc = GetDC( NULL );
        g_gpu.scratch_dc = CreateCompatibleDC( screen_dc );
        if (screen_dc) ReleaseDC( NULL, screen_dc );
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = SCRATCH_MAX_W;
        bmi.bmiHeader.biHeight = -SCRATCH_MAX_H;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        g_gpu.scratch_bmp = CreateDIBSection( g_gpu.scratch_dc, &bmi, DIB_RGB_COLORS,
                                              (void **)&g_gpu.scratch_bits, NULL, 0 );
        if (g_gpu.scratch_dc && g_gpu.scratch_bmp && g_gpu.scratch_bits)
            g_gpu.scratch_old_bmp = (HBITMAP)SelectObject( g_gpu.scratch_dc, g_gpu.scratch_bmp );
        else
            hr = E_FAIL;
    }

    if (FAILED(hr))
    {
        WARN( "xaml_gpu_init: resource creation failed hr=%#lx\n", hr );
        xaml_gpu_shutdown();
        g_gpu.init_failed = TRUE;
        return FALSE;
    }

    g_gpu.initialized = TRUE;
    TRACE( "xaml_gpu_init: initialized SDF + %ux%u atlas renderer\n", ATLAS_SIZE, ATLAS_SIZE );
    return TRUE;
}

static HFONT xaml_gpu_get_font( int height, int weight )
{
    int i, slot;
    for (i = 0; i < FONT_CACHE_SIZE; i++)
    {
        if (g_gpu.fonts[i].hfont && g_gpu.fonts[i].height == height && g_gpu.fonts[i].weight == weight)
            return g_gpu.fonts[i].hfont;
    }
    slot = g_gpu.font_cursor % FONT_CACHE_SIZE;
    g_gpu.font_cursor++;
    if (g_gpu.fonts[slot].hfont)
        DeleteObject( g_gpu.fonts[slot].hfont );
    g_gpu.fonts[slot].height = height;
    g_gpu.fonts[slot].weight = weight;
    g_gpu.fonts[slot].hfont = CreateFontW( -height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           ANTIALIASED_QUALITY, FF_SWISS, L"Segoe UI" );
    return g_gpu.fonts[slot].hfont;
}

static BOOL xaml_gpu_draw_batch( ID3D11DeviceContext *ctx );

static BOOL atlas_alloc_rect( ID3D11DeviceContext *ctx, UINT w, UINT h, UINT *out_x, UINT *out_y )
{
    if (w == 0 || h == 0 || w + 1 > ATLAS_SIZE || h + 1 > ATLAS_SIZE)
        return FALSE;
    if (g_gpu.shelf_x + w + 1 > ATLAS_SIZE)
    {
        g_gpu.shelf_y += g_gpu.shelf_h + 1;
        g_gpu.shelf_x = 0;
        g_gpu.shelf_h = 0;
    }
    if (g_gpu.shelf_y + h + 1 > ATLAS_SIZE)
    {
        /* Flush queued quads before overwriting atlas coordinates mid-frame */
        if (ctx && g_gpu.instance_count > 0)
        {
            xaml_gpu_draw_batch( ctx );
            g_gpu.instance_count = 0;
        }
        /* Atlas full: reset shelf allocator and hash table */
        g_gpu.shelf_x = 0;
        g_gpu.shelf_y = 0;
        g_gpu.shelf_h = 0;
        memset( g_gpu.hash_table, 0, sizeof(g_gpu.hash_table) );
    }
    *out_x = g_gpu.shelf_x;
    *out_y = g_gpu.shelf_y;
    g_gpu.shelf_x += w + 1;
    if (h > g_gpu.shelf_h) g_gpu.shelf_h = h;
    return TRUE;
}

BOOL xaml_gpu_begin_frame( ID3D11Device *dev, ID3D11DeviceContext *ctx, IDXGISwapChain1 *swapchain,
                           UINT width, UINT height, COLORREF clear_color )
{
    float clear_rgba[4];
    HRESULT hr;

    if (!dev || !ctx || !swapchain || width == 0 || height == 0)
        return FALSE;
    if (!xaml_gpu_init( dev ))
        return FALSE;

    if (!g_gpu.rtv || g_gpu.rtv_swapchain != swapchain || g_gpu.rtv_width != width || g_gpu.rtv_height != height)
    {
        ID3D11Texture2D *backbuf = NULL;
        xaml_gpu_release_rtv();
        hr = IDXGISwapChain1_GetBuffer( swapchain, 0, &IID_ID3D11Texture2D, (void **)&backbuf );
        if (FAILED(hr) || !backbuf)
            return FALSE;
        hr = ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)backbuf, NULL, &g_gpu.rtv );
        ID3D11Texture2D_Release( backbuf );
        if (FAILED(hr) || !g_gpu.rtv)
            return FALSE;
        g_gpu.rtv_swapchain = swapchain;
        g_gpu.rtv_width = width;
        g_gpu.rtv_height = height;
    }

    if (xaml_has_active_swapchain_panel())
    {
        clear_rgba[0] = 0.0f;
        clear_rgba[1] = 0.0f;
        clear_rgba[2] = 0.0f;
        clear_rgba[3] = 0.0f;
    }
    else
    {
        clear_rgba[0] = (float)GetRValue( clear_color ) / 255.0f;
        clear_rgba[1] = (float)GetGValue( clear_color ) / 255.0f;
        clear_rgba[2] = (float)GetBValue( clear_color ) / 255.0f;
        clear_rgba[3] = 1.0f;
    }

    ID3D11DeviceContext_OMSetRenderTargets( ctx, 1, &g_gpu.rtv, NULL );
    ID3D11DeviceContext_ClearRenderTargetView( ctx, g_gpu.rtv, clear_rgba );

    g_gpu.instance_count = 0;
    g_gpu.frame_width = width;
    g_gpu.frame_height = height;
    g_gpu.atlas_hits = 0;
    g_gpu.atlas_misses = 0;
    return TRUE;
}

/* Same as xaml_gpu_begin_frame() but targeting a caller-owned offscreen render
 * target (WS_CHILD islands have no on-screen X11 window to present to). */
BOOL xaml_gpu_begin_frame_rtv( ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv,
                               UINT width, UINT height, COLORREF clear_color )
{
    float clear_rgba[4];

    if (!dev || !ctx || !rtv || width == 0 || height == 0)
        return FALSE;
    if (!xaml_gpu_init( dev ))
        return FALSE;

    if (g_gpu.rtv != rtv)
    {
        xaml_gpu_release_rtv();
        ID3D11RenderTargetView_AddRef( rtv );
        g_gpu.rtv = rtv;
        g_gpu.rtv_swapchain = NULL;
        g_gpu.rtv_width = width;
        g_gpu.rtv_height = height;
    }

    if (xaml_has_active_swapchain_panel())
    {
        clear_rgba[0] = 0.0f;
        clear_rgba[1] = 0.0f;
        clear_rgba[2] = 0.0f;
        clear_rgba[3] = 0.0f;
    }
    else
    {
        clear_rgba[0] = (float)GetRValue( clear_color ) / 255.0f;
        clear_rgba[1] = (float)GetGValue( clear_color ) / 255.0f;
        clear_rgba[2] = (float)GetBValue( clear_color ) / 255.0f;
        clear_rgba[3] = 1.0f;
    }

    ID3D11DeviceContext_OMSetRenderTargets( ctx, 1, &g_gpu.rtv, NULL );
    ID3D11DeviceContext_ClearRenderTargetView( ctx, g_gpu.rtv, clear_rgba );

    g_gpu.instance_count = 0;
    g_gpu.frame_width = width;
    g_gpu.frame_height = height;
    g_gpu.atlas_hits = 0;
    g_gpu.atlas_misses = 0;
    return TRUE;
}

void xaml_gpu_push_rect( float x0, float y0, float x1, float y1, const RECT *clip_rc,
                         float radius, UINT32 fill_argb, UINT32 border_argb,
                         float border_thickness, float opacity )
{
    struct xaml_gpu_instance *inst;
    if (!g_gpu.initialized || g_gpu.instance_count >= MAX_GPU_INSTANCES) return;
    if (x1 <= x0 || y1 <= y0) return;

    inst = &g_gpu.instances[g_gpu.instance_count++];
    inst->rect[0] = x0;
    inst->rect[1] = y0;
    inst->rect[2] = x1;
    inst->rect[3] = y1;
    if (clip_rc)
    {
        inst->clip[0] = (float)clip_rc->left;
        inst->clip[1] = (float)clip_rc->top;
        inst->clip[2] = (float)clip_rc->right;
        inst->clip[3] = (float)clip_rc->bottom;
    }
    else
    {
        inst->clip[0] = 0.0f;
        inst->clip[1] = 0.0f;
        inst->clip[2] = (float)g_gpu.frame_width;
        inst->clip[3] = (float)g_gpu.frame_height;
    }
    inst->radii[0] = radius;
    inst->radii[1] = radius;
    inst->radii[2] = radius;
    inst->radii[3] = radius;
    rgba_to_float4( fill_argb, inst->fill_color );
    rgba_to_float4( border_argb, inst->border_color );
    memset( inst->uv, 0, sizeof(inst->uv) );
    inst->params[0] = border_thickness;
    inst->params[1] = 0.0f; /* SDF rect/border */
    inst->params[2] = opacity;
    inst->params[3] = 0.0f;
}

void xaml_gpu_push_ring( float x0, float y0, float x1, float y1, const RECT *clip_rc,
                         UINT32 color_argb, float thickness, BOOL arc_quadrant )
{
    struct xaml_gpu_instance *inst;
    if (!g_gpu.initialized || g_gpu.instance_count >= MAX_GPU_INSTANCES) return;
    if (x1 <= x0 || y1 <= y0) return;

    inst = &g_gpu.instances[g_gpu.instance_count++];
    inst->rect[0] = x0;
    inst->rect[1] = y0;
    inst->rect[2] = x1;
    inst->rect[3] = y1;
    if (clip_rc)
    {
        inst->clip[0] = (float)clip_rc->left;
        inst->clip[1] = (float)clip_rc->top;
        inst->clip[2] = (float)clip_rc->right;
        inst->clip[3] = (float)clip_rc->bottom;
    }
    else
    {
        inst->clip[0] = 0.0f;
        inst->clip[1] = 0.0f;
        inst->clip[2] = (float)g_gpu.frame_width;
        inst->clip[3] = (float)g_gpu.frame_height;
    }
    memset( inst->radii, 0, sizeof(inst->radii) );
    rgba_to_float4( color_argb, inst->fill_color );
    memset( inst->border_color, 0, sizeof(inst->border_color) );
    memset( inst->uv, 0, sizeof(inst->uv) );
    inst->params[0] = thickness;
    inst->params[1] = 2.0f; /* SDF ring */
    inst->params[2] = 1.0f;
    inst->params[3] = arc_quadrant ? 1.0f : 0.0f;
}

void xaml_gpu_push_text( ID3D11DeviceContext *ctx, const WCHAR *text, int font_height, int font_weight,
                         UINT dt_flags, const RECT *rc, const RECT *clip_rc, COLORREF color, float opacity )
{
    UINT bw, bh, idx, probe;
    UINT64 h;
    size_t tlen;
    struct atlas_entry *entry = NULL;
    struct xaml_gpu_instance *inst;
    UINT align_flags;

    if (!g_gpu.initialized || !text || !text[0] || !rc) return;
    if (g_gpu.instance_count >= MAX_GPU_INSTANCES) return;
    if (rc->right <= rc->left || rc->bottom <= rc->top) return;

    bw = (UINT)(rc->right - rc->left);
    bh = (UINT)(rc->bottom - rc->top);
    if (bw > SCRATCH_MAX_W) bw = SCRATCH_MAX_W;
    if (bh > SCRATCH_MAX_H) bh = SCRATCH_MAX_H;
    tlen = wcslen( text );
    if (tlen > 511) tlen = 511;

    align_flags = dt_flags & (DT_LEFT | DT_CENTER | DT_RIGHT | DT_TOP | DT_VCENTER | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_WORDBREAK);
    h = fnv1a_bytes( &font_height, sizeof(font_height), 0 );
    h = fnv1a_bytes( &font_weight, sizeof(font_weight), h );
    h = fnv1a_bytes( &align_flags, sizeof(align_flags), h );
    h = fnv1a_bytes( &bw, sizeof(bw), h );
    h = fnv1a_bytes( &bh, sizeof(bh), h );
    h = fnv1a_bytes( text, tlen * sizeof(WCHAR), h );

    idx = (UINT)(h % ATLAS_HASH_SIZE);
    for (probe = 0; probe < 32; probe++)
    {
        struct atlas_entry *e = &g_gpu.hash_table[(idx + probe) % ATLAS_HASH_SIZE];
        if (!e->occupied)
        {
            entry = e;
            break;
        }
        if (!e->is_image && e->hash == h &&
            e->font_height == font_height && e->font_weight == font_weight &&
            e->dt_flags == align_flags && e->box_w == bw && e->box_h == bh &&
            !wcsncmp( e->text, text, tlen ) && e->text[tlen] == 0)
        {
            entry = e;
            g_gpu.atlas_hits++;
            break;
        }
    }

    if (!entry)
        entry = &g_gpu.hash_table[idx];

    if (!entry->occupied || entry->is_image || entry->hash != h)
    {
        UINT ax = 0, ay = 0, y, x;
        RECT local_rc = { 0, 0, (LONG)bw, (LONG)bh };
        HFONT hf = xaml_gpu_get_font( font_height, font_weight );
        HGDIOBJ old_font;
        D3D11_BOX dst_box;

        if (!atlas_alloc_rect( ctx, bw, bh, &ax, &ay ))
            return;

        g_gpu.atlas_misses++;
        for (y = 0; y < bh; y++)
            memset( g_gpu.scratch_bits + y * SCRATCH_MAX_W, 0, bw * sizeof(UINT32) );

        old_font = SelectObject( g_gpu.scratch_dc, hf );
        SetTextColor( g_gpu.scratch_dc, RGB( 255, 255, 255 ) );
        SetBkMode( g_gpu.scratch_dc, TRANSPARENT );
        DrawTextW( g_gpu.scratch_dc, text, (int)tlen, &local_rc, align_flags );
        SelectObject( g_gpu.scratch_dc, old_font );
        GdiFlush();

        /* Convert grayscale luminance to white + alpha mask */
        for (y = 0; y < bh; y++)
        {
            UINT32 *row = g_gpu.scratch_bits + y * SCRATCH_MAX_W;
            for (x = 0; x < bw; x++)
            {
                UINT32 px = row[x];
                BYTE r = (BYTE)((px >> 16) & 0xff);
                BYTE g = (BYTE)((px >> 8) & 0xff);
                BYTE b = (BYTE)(px & 0xff);
                BYTE lum = r > g ? (r > b ? r : b) : (g > b ? g : b);
                row[x] = ((UINT32)lum << 24) | 0x00ffffffu;
            }
        }

        dst_box.left = ax;
        dst_box.top = ay;
        dst_box.right = ax + bw;
        dst_box.bottom = ay + bh;
        dst_box.front = 0;
        dst_box.back = 1;
        ID3D11DeviceContext_UpdateSubresource( ctx, (ID3D11Resource *)g_gpu.atlas_tex, 0, &dst_box,
                                               g_gpu.scratch_bits, SCRATCH_MAX_W * 4, 0 );

        entry->occupied = TRUE;
        entry->is_image = FALSE;
        entry->hash = h;
        entry->font_height = font_height;
        entry->font_weight = font_weight;
        entry->dt_flags = align_flags;
        entry->box_w = bw;
        entry->box_h = bh;
        memcpy( entry->text, text, tlen * sizeof(WCHAR) );
        entry->text[tlen] = 0;
        entry->uv[0] = (float)ax / ATLAS_SIZE;
        entry->uv[1] = (float)ay / ATLAS_SIZE;
        entry->uv[2] = (float)(ax + bw) / ATLAS_SIZE;
        entry->uv[3] = (float)(ay + bh) / ATLAS_SIZE;
    }

    inst = &g_gpu.instances[g_gpu.instance_count++];
    inst->rect[0] = (float)rc->left;
    inst->rect[1] = (float)rc->top;
    inst->rect[2] = (float)(rc->left + (LONG)bw);
    inst->rect[3] = (float)(rc->top + (LONG)bh);
    if (clip_rc)
    {
        inst->clip[0] = (float)clip_rc->left;
        inst->clip[1] = (float)clip_rc->top;
        inst->clip[2] = (float)clip_rc->right;
        inst->clip[3] = (float)clip_rc->bottom;
    }
    else
    {
        inst->clip[0] = 0.0f;
        inst->clip[1] = 0.0f;
        inst->clip[2] = (float)g_gpu.frame_width;
        inst->clip[3] = (float)g_gpu.frame_height;
    }
    memset( inst->radii, 0, sizeof(inst->radii) );
    inst->fill_color[0] = (float)GetRValue( color ) / 255.0f;
    inst->fill_color[1] = (float)GetGValue( color ) / 255.0f;
    inst->fill_color[2] = (float)GetBValue( color ) / 255.0f;
    inst->fill_color[3] = 1.0f;
    memset( inst->border_color, 0, sizeof(inst->border_color) );
    memcpy( inst->uv, entry->uv, sizeof(inst->uv) );
    inst->params[0] = 0.0f;
    inst->params[1] = 1.0f; /* Atlas quad */
    inst->params[2] = opacity;
    inst->params[3] = 0.0f;
}

void xaml_gpu_push_image( ID3D11DeviceContext *ctx, UINT64 img_key, const BYTE *bgra_pixels,
                          UINT img_w, UINT img_h, const RECT *dst_rc, const RECT *clip_rc )
{
    UINT idx, probe;
    struct atlas_entry *entry = NULL;
    struct xaml_gpu_instance *inst;

    if (!g_gpu.initialized || !bgra_pixels || img_w == 0 || img_h == 0 || !dst_rc) return;
    if (g_gpu.instance_count >= MAX_GPU_INSTANCES) return;
    if (img_w > ATLAS_SIZE - 2 || img_h > ATLAS_SIZE - 2) return;

    idx = (UINT)(img_key % ATLAS_HASH_SIZE);
    for (probe = 0; probe < 32; probe++)
    {
        struct atlas_entry *e = &g_gpu.hash_table[(idx + probe) % ATLAS_HASH_SIZE];
        if (!e->occupied) { entry = e; break; }
        if (e->is_image && e->img_key == img_key && e->box_w == img_w && e->box_h == img_h)
        {
            entry = e;
            g_gpu.atlas_hits++;
            break;
        }
    }
    if (!entry) entry = &g_gpu.hash_table[idx];

    if (!entry->occupied || !entry->is_image || entry->img_key != img_key)
    {
        UINT ax = 0, ay = 0;
        D3D11_BOX dst_box;
        if (!atlas_alloc_rect( ctx, img_w, img_h, &ax, &ay )) return;
        g_gpu.atlas_misses++;
        dst_box.left = ax;
        dst_box.top = ay;
        dst_box.right = ax + img_w;
        dst_box.bottom = ay + img_h;
        dst_box.front = 0;
        dst_box.back = 1;
        ID3D11DeviceContext_UpdateSubresource( ctx, (ID3D11Resource *)g_gpu.atlas_tex, 0, &dst_box,
                                               bgra_pixels, img_w * 4, 0 );
        entry->occupied = TRUE;
        entry->is_image = TRUE;
        entry->img_key = img_key;
        entry->box_w = img_w;
        entry->box_h = img_h;
        entry->uv[0] = (float)ax / ATLAS_SIZE;
        entry->uv[1] = (float)ay / ATLAS_SIZE;
        entry->uv[2] = (float)(ax + img_w) / ATLAS_SIZE;
        entry->uv[3] = (float)(ay + img_h) / ATLAS_SIZE;
    }

    inst = &g_gpu.instances[g_gpu.instance_count++];
    inst->rect[0] = (float)dst_rc->left;
    inst->rect[1] = (float)dst_rc->top;
    inst->rect[2] = (float)dst_rc->right;
    inst->rect[3] = (float)dst_rc->bottom;
    if (clip_rc)
    {
        inst->clip[0] = (float)clip_rc->left;
        inst->clip[1] = (float)clip_rc->top;
        inst->clip[2] = (float)clip_rc->right;
        inst->clip[3] = (float)clip_rc->bottom;
    }
    else
    {
        inst->clip[0] = 0.0f;
        inst->clip[1] = 0.0f;
        inst->clip[2] = (float)g_gpu.frame_width;
        inst->clip[3] = (float)g_gpu.frame_height;
    }
    memset( inst->radii, 0, sizeof(inst->radii) );
    inst->fill_color[0] = 1.0f;
    inst->fill_color[1] = 1.0f;
    inst->fill_color[2] = 1.0f;
    inst->fill_color[3] = 1.0f;
    memset( inst->border_color, 0, sizeof(inst->border_color) );
    memcpy( inst->uv, entry->uv, sizeof(inst->uv) );
    inst->params[0] = 0.0f;
    inst->params[1] = 1.0f;
    inst->params[2] = 1.0f;
    inst->params[3] = 0.0f;
}

static BOOL xaml_gpu_draw_batch( ID3D11DeviceContext *ctx )
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D11_VIEWPORT vp;
    UINT stride = sizeof(struct xaml_gpu_instance);
    UINT offset = 0;
    float blend_factor[4] = { 0, 0, 0, 0 };
    HRESULT hr;

    if (!g_gpu.initialized || !ctx)
        return FALSE;

    if (g_gpu.instance_count > 0)
    {
        hr = ID3D11DeviceContext_Map( ctx, (ID3D11Resource *)g_gpu.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
        if (FAILED(hr)) return FALSE;
        {
            struct xaml_gpu_constants *c = (struct xaml_gpu_constants *)mapped.pData;
            c->viewport_size[0] = (float)g_gpu.frame_width;
            c->viewport_size[1] = (float)g_gpu.frame_height;
            c->pad[0] = 0.0f;
            c->pad[1] = 0.0f;
        }
        ID3D11DeviceContext_Unmap( ctx, (ID3D11Resource *)g_gpu.cb, 0 );

        hr = ID3D11DeviceContext_Map( ctx, (ID3D11Resource *)g_gpu.instance_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
        if (FAILED(hr)) return FALSE;
        memcpy( mapped.pData, g_gpu.instances, g_gpu.instance_count * sizeof(struct xaml_gpu_instance) );
        ID3D11DeviceContext_Unmap( ctx, (ID3D11Resource *)g_gpu.instance_vb, 0 );

        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = (float)g_gpu.frame_width;
        vp.Height = (float)g_gpu.frame_height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ID3D11DeviceContext_RSSetViewports( ctx, 1, &vp );
        ID3D11DeviceContext_RSSetState( ctx, g_gpu.rast_state );
        ID3D11DeviceContext_OMSetBlendState( ctx, g_gpu.blend_state, blend_factor, 0xffffffff );

        ID3D11DeviceContext_IASetInputLayout( ctx, g_gpu.layout );
        ID3D11DeviceContext_IASetPrimitiveTopology( ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
        ID3D11DeviceContext_IASetVertexBuffers( ctx, 0, 1, &g_gpu.instance_vb, &stride, &offset );

        ID3D11DeviceContext_VSSetShader( ctx, g_gpu.vs, NULL, 0 );
        ID3D11DeviceContext_VSSetConstantBuffers( ctx, 0, 1, &g_gpu.cb );
        ID3D11DeviceContext_PSSetShader( ctx, g_gpu.ps, NULL, 0 );
        ID3D11DeviceContext_PSSetShaderResources( ctx, 0, 1, &g_gpu.atlas_srv );
        ID3D11DeviceContext_PSSetSamplers( ctx, 0, 1, &g_gpu.sampler );

        ID3D11DeviceContext_DrawInstanced( ctx, 4, g_gpu.instance_count, 0, 0 );
    }
    return TRUE;
}

BOOL xaml_gpu_end_frame( ID3D11DeviceContext *ctx, IDXGISwapChain1 *swapchain,
                         UINT *out_instances, UINT *out_hits, UINT *out_misses )
{
    if (!swapchain)
        return FALSE;
    if (!xaml_gpu_draw_batch( ctx ))
        return FALSE;

    IDXGISwapChain1_Present( swapchain, 0, 0 );

    if (out_instances) *out_instances = g_gpu.instance_count;
    if (out_hits) *out_hits = g_gpu.atlas_hits;
    if (out_misses) *out_misses = g_gpu.atlas_misses;
    return TRUE;
}

/* Flush the instanced batch into the offscreen render target set by
 * xaml_gpu_begin_frame_rtv(); no present — the caller reads the pixels back. */
BOOL xaml_gpu_end_frame_rtv( ID3D11DeviceContext *ctx, UINT *out_instances,
                             UINT *out_hits, UINT *out_misses )
{
    if (!xaml_gpu_draw_batch( ctx ))
        return FALSE;

    ID3D11DeviceContext_Flush( ctx );

    if (out_instances) *out_instances = g_gpu.instance_count;
    if (out_hits) *out_hits = g_gpu.atlas_hits;
    if (out_misses) *out_misses = g_gpu.atlas_misses;
    return TRUE;
}
