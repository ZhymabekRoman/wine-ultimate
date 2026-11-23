Texture2D<float4> src : register(t0);
cbuffer params : register(b0)
{
    float4 color;
    float sigma;
    int radius;
    int src_offset_x;
    int src_offset_y;
    int src_min_x;
    int src_min_y;
    int src_max_x;
    int src_max_y;
};
float4 main(float4 pos : SV_POSITION) : SV_TARGET
{
    int x = (int)pos.x;
    int y = (int)pos.y;
    float blurred_a;
    if (radius <= 0 || sigma <= 0.001f)
    {
        blurred_a = src.Load(int3(x, y, 0)).r;
    }
    else
    {
        float inv_2sigma2 = 0.5f / (sigma * sigma);
        float sum = 0.0f;
        float wsum = 0.0f;
        [loop]
        for (int i = -radius; i <= radius; ++i)
        {
            float fi = (float)i;
            float w = exp(-fi * fi * inv_2sigma2);
            int sy = y + i;
            float a = (sy >= 0 && sy < src_max_y) ? src.Load(int3(x, sy, 0)).r : 0.0f;
            sum += a * w;
            wsum += w;
        }
        blurred_a = sum / wsum;
    }
    float out_a = saturate(blurred_a * color.a);
    return float4(color.rgb * out_a, out_a);
}
