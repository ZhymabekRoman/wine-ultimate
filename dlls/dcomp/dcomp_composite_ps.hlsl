Texture2D src_texture : register(t0);
SamplerState sam_linear : register(s0);
cbuffer blit_data : register(b0)
{
    float opacity;
    float replace_popup_black;
    float backdrop_radius;
    float pad;
    float4 clip_rect; /* left, top, right, bottom in device pixels */
    float4 backdrop_rect; /* left, top, right, bottom in device pixels */
    float4 popup_bg_color; /* theme-aware popup backdrop color */
};
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET
{
    float2 px = pos.xy - float2(0.5, 0.5);
    if (px.x < clip_rect.x || px.y < clip_rect.y || px.x > clip_rect.z || px.y > clip_rect.w)
        discard;
    float4 c = src_texture.Sample(sam_linear, uv);
    if (replace_popup_black > 0.5 && c.a > 0.99 && c.r < 0.005 && c.g < 0.005 && c.b < 0.005)
        c = float4(popup_bg_color.rgb, 1.0);
    if (backdrop_rect.z > backdrop_rect.x && backdrop_rect.w > backdrop_rect.y)
    {
        float2 center = 0.5 * (backdrop_rect.xy + backdrop_rect.zw);
        float2 half_size = 0.5 * (backdrop_rect.zw - backdrop_rect.xy);
        float r = min(backdrop_radius, min(half_size.x, half_size.y));
        float2 q = abs(px - center) - (half_size - float2(r, r));
        float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        if (dist > 0.0)
            discard;
        float3 bg = popup_bg_color.rgb;
        c = float4(c.rgb + bg * (1.0 - c.a), 1.0);
    }
    return float4(c.rgb * opacity, c.a * opacity);
}
