float3x2 transform_geometry;
float stroke_width;
float4 transform_rtx;
float4 transform_rty;

struct output
{
    float2 p : WORLD_POSITION;
    float4 b : BEZIER;
    nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM;
    float4 position : SV_POSITION;
};

void main(float2 position : POSITION, float2 p0 : P0, float2 p1 : P1, float2 p2 : P2,
        float2 prev : PREV, float2 next : NEXT, out struct output o)
{
    float2 q_prev, q_next, v_p, q_i, p;
    float2x2 geom, rt;
    float l;

    geom = float2x2(transform_geometry._11_21, transform_geometry._12_22);
    rt = float2x2(transform_rtx.xy, transform_rty.xy);
    o.stroke_transform = rt * stroke_width * 0.5f;

    p = mul(geom, position);
    p0 = mul(geom, p0);
    p1 = mul(geom, p1);
    p2 = mul(geom, p2);

    p -= p0;
    p1 -= p0;
    p2 -= p0;

    q_prev = normalize(mul(geom, prev));
    q_next = normalize(mul(geom, next));

    v_p = float2(-q_prev.y, q_prev.x);
    l = -dot(v_p, q_next) / (1.0f + dot(q_prev, q_next));
    q_i = l * q_prev + v_p;
    p += 0.5f * stroke_width * q_i;

    v_p = mul(rt, p2);
    v_p = normalize(float2(-v_p.y, v_p.x));
    if (abs(dot(mul(rt, p1), v_p)) < 1.0f)
    {
        o.b.xzw = float3(0.0f, 0.0f, 0.0f);
        o.b.y = dot(mul(rt, p), v_p);
    }
    else
    {
        o.b.zw = sign(dot(mul(rt, p1), v_p)) * v_p;
        v_p = -float2(-p.y, p.x) / dot(float2(-p1.y, p1.x), p2);
        o.b.x = dot(v_p, p1 - 0.5f * p2);
        o.b.y = dot(v_p, p1);
    }

    o.p = mul(float3(position, 1.0f), transform_geometry) + 0.5f * stroke_width * q_i;
    position = mul(float2x3(transform_rtx.xyz, transform_rty.xyz), float3(o.p, 1.0f))
            * float2(transform_rtx.w, transform_rty.w);
    o.position = float4(position + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}
