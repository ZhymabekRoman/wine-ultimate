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
    int x = (int)pos.x + src_offset_x;
    int y = (int)pos.y + src_offset_y;
    if (y < src_min_y || y >= src_max_y)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (radius <= 0 || sigma <= 0.001f)
    {
        float a = (x >= src_min_x && x < src_max_x) ? src.Load(int3(x, y, 0)).a : 0.0f;
        return float4(a, 0.0f, 0.0f, 1.0f);
    }
    float inv_2sigma2 = 0.5f / (sigma * sigma);
    float sum = 0.0f;
    float wsum = 0.0f;
    [loop]
    for (int i = -radius; i <= radius; ++i)
    {
        float fi = (float)i;
        float w = exp(-fi * fi * inv_2sigma2);
        int sx = x + i;
        float a = (sx >= src_min_x && sx < src_max_x) ? src.Load(int3(sx, y, 0)).a : 0.0f;
        sum += a * w;
        wsum += w;
    }
    return float4(sum / wsum, 0.0f, 0.0f, 1.0f);
}
