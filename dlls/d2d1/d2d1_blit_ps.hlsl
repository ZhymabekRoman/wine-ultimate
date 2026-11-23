Texture2D<float4> src : register(t0);
float4 main(float4 pos : SV_POSITION) : SV_TARGET
{
    return src.Load(int3(pos.xy, 0));
}
