float3x2 transform_geometry;
float4 transform_rtx;
float4 transform_rty;

struct output
{
    float2 p : WORLD_POSITION;
    float4 b : BEZIER;
    nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM;
    float4 position : SV_POSITION;
};

void main(float2 position : POSITION, out struct output o)
{
    o.p = mul(float3(position, 1.0f), transform_geometry);
    o.b = float4(1.0, 0.0, 1.0, 1.0);
    o.stroke_transform = float2x2(1.0, 0.0, 0.0, 1.0);
    position = mul(float2x3(transform_rtx.xyz, transform_rty.xyz), float3(o.p, 1.0f))
            * float2(transform_rtx.w, transform_rty.w);
    o.position = float4(position + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}
