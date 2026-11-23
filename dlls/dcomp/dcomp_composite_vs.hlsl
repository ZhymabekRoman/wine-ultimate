cbuffer vs_data : register(b0)
{
    float4 m0;
    float4 m1;
    float4 m2;
    float4 m3;
};
void main(in float4 in_position : POSITION,
          in float2 in_texcoord : TEXCOORD,
          out float4 position : SV_Position,
          out float2 texcoord : TEXCOORD)
{
     float4 p = float4(in_position.xy, 0.0, 1.0);
     position = p.x * m0 + p.y * m1 + p.z * m2 + p.w * m3;
     texcoord = in_texcoord;
}
