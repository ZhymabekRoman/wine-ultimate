#define BRUSH_TYPE_SOLID    0
#define BRUSH_TYPE_LINEAR   1
#define BRUSH_TYPE_RADIAL   2
#define BRUSH_TYPE_BITMAP   3
#define BRUSH_TYPE_COUNT    4

bool outline;
bool is_arc;
bool antialias;
struct brush
{
    uint type;
    float opacity;
    float4 data[3];
} colour_brush, opacity_brush;

SamplerState s0, s1;
Texture2D t0, t1;
Buffer<float4> b0, b1;

struct input
{
    float2 p : WORLD_POSITION;
    float4 b : BEZIER;
    nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM;
};

float4 sample_gradient(Buffer<float4> gradient, uint stop_count, float position)
{
    float4 c_low, c_high;
    float p_low, p_high;
    uint i;

    p_low = gradient.Load(0).x;
    c_low = gradient.Load(1);
    c_high = c_low;

    if (position < p_low)
        return c_low;

    [loop]
    for (i = 1; i < stop_count; ++i)
    {
        p_high = gradient.Load(i * 2).x;
        c_high = gradient.Load(i * 2 + 1);

        if (position >= p_low && position <= p_high)
            return lerp(c_low, c_high, (position - p_low) / (p_high - p_low));

        p_low = p_high;
        c_low = c_high;
    }

    return c_high;
}

float4 brush_linear(struct brush brush, Buffer<float4> gradient, float2 position)
{
    float2 start, end, v_p, v_q;
    uint stop_count;
    float p;

    start = brush.data[0].xy;
    end = brush.data[0].zw;
    stop_count = asuint(brush.data[1].x);

    v_p = position - start;
    v_q = end - start;
    p = dot(v_q, v_p) / dot(v_q, v_q);

    return sample_gradient(gradient, stop_count, p);
}

float4 brush_radial(struct brush brush, Buffer<float4> gradient, float2 position)
{
    float2 centre, offset, ra, rb, v_p, v_q, r;
    float b, c, l, t;
    uint stop_count;

    centre = brush.data[0].xy;
    offset = brush.data[0].zw;
    ra = brush.data[1].xy;
    rb = brush.data[1].zw;
    stop_count = asuint(brush.data[2].x);

    /* Project onto ra, rb. */
    r = float2(dot(ra, ra), dot(rb, rb));
    v_p = position - (centre + offset);
    v_p = float2(dot(v_p, ra), dot(v_p, rb)) / r;
    v_q = float2(dot(offset, ra), dot(offset, rb)) / r;

    /* ‖t·p̂ + q⃑‖ = 1
     * (t·p̂ + q⃑) · (t·p̂ + q⃑) = 1
     * t² + 2·(p̂·q⃑)·t + (q⃑·q⃑) = 1
     *
     * b = p̂·q⃑
     * c = q⃑·q⃑ - 1
     * t = -b + √(b² - c) */
    l = length(v_p);
    b = dot(v_p, v_q) / l;
    c = dot(v_q, v_q) - 1.0;
    t = -b + sqrt(b * b - c);

    return sample_gradient(gradient, stop_count, l / t);
}

float4 brush_bitmap(struct brush brush, Texture2D t, SamplerState s, float2 position)
{
    float3 transform[2];
    bool ignore_alpha;
    float2 texcoord;
    float4 colour;

    transform[0] = brush.data[0].xyz;
    transform[1] = brush.data[1].xyz;
    ignore_alpha = asuint(brush.data[1].w);

    texcoord.x = dot(position.xy, transform[0].xy) + transform[0].z;
    texcoord.y = dot(position.xy, transform[1].xy) + transform[1].z;
    colour = t.Sample(s, texcoord);
    if (ignore_alpha)
        colour.a = 1.0;
    return colour;
}

float4 sample_brush(struct brush brush, Texture2D t, SamplerState s, Buffer<float4> b, float2 position)
{
    if (brush.type == BRUSH_TYPE_SOLID)
        return brush.data[0] * brush.opacity;
    if (brush.type == BRUSH_TYPE_LINEAR)
        return brush_linear(brush, b, position) * brush.opacity;
    if (brush.type == BRUSH_TYPE_RADIAL)
        return brush_radial(brush, b, position) * brush.opacity;
    if (brush.type == BRUSH_TYPE_BITMAP)
        return brush_bitmap(brush, t, s, position) * brush.opacity;
    return float4(0.0, 0.0, 0.0, brush.opacity);
}

float4 main(struct input i) : SV_Target
{
    float4 colour;

    colour = sample_brush(colour_brush, t0, s0, b0, i.p);
    if (opacity_brush.type < BRUSH_TYPE_COUNT)
        colour *= sample_brush(opacity_brush, t1, s1, b1, i.p).a;

    if (outline)
    {
        float2 du, dv, df;
        float4 uv;

        /* Evaluate the implicit form of the curve (u² - v = 0
         * for Béziers, u² + v² - 1 = 0 for arcs) in texture
         * space, using the screen-space partial derivatives
         * to convert the calculated distance to object space.
         *
         * d(x, y) = |f(x, y)| / ‖∇f(x, y)‖
         *         = |f(x, y)| / √((∂f/∂x)² + (∂f/∂y)²)
         *
         * For Béziers:
         * f(x, y) = u(x, y)² - v(x, y)
         * ∂f/∂x = 2u · ∂u/∂x - ∂v/∂x
         * ∂f/∂y = 2u · ∂u/∂y - ∂v/∂y
         *
         * For arcs:
         * f(x, y) = u(x, y)² + v(x, y)² - 1
         * ∂f/∂x = 2u · ∂u/∂x + 2v · ∂v/∂x
         * ∂f/∂y = 2u · ∂u/∂y + 2v · ∂v/∂y */
        uv = i.b;
        du = float2(ddx(uv.x), ddy(uv.x));
        dv = float2(ddx(uv.y), ddy(uv.y));

        if (!is_arc)
        {
            df = 2.0f * uv.x * du - dv;

            clip(dot(df, uv.zw));
            float dist = length(mul(i.stroke_transform, df)) - abs(uv.x * uv.x - uv.y);
            if (antialias && dot(df, df) > 1e-12f)
            {
                float cov = saturate(0.5f + dist / max(length(df), 1e-6f));
                clip(cov - 1e-4f);
                colour *= cov;
            }
            else
            {
                clip(dist);
            }
        }
        else
        {
            df = 2.0f * uv.x * du + 2.0f * uv.y * dv;

            clip(dot(df, uv.zw));
            float dist = length(mul(i.stroke_transform, df)) - abs(uv.x * uv.x + uv.y * uv.y - 1.0f);
            if (antialias)
            {
                float cov = saturate(0.5f + dist / max(length(df), 1e-6f));
                clip(cov - 1e-4f);
                colour *= cov;
            }
            else
            {
                clip(dist);
            }
        }
    }
    else
    {
        /* Evaluate the implicit form of the curve in texture space.
         * "i.b.z" determines which side of the curve is shaded. */
        if (i.b.z != 0.0f)
        {
            float signed_f = (is_arc ? (i.b.x * i.b.x + i.b.y * i.b.y - 1.0f)
                    : (i.b.x * i.b.x - i.b.y)) * i.b.z;
            if (antialias)
            {
                float2 du = float2(ddx(i.b.x), ddy(i.b.x));
                float2 dv = float2(ddx(i.b.y), ddy(i.b.y));
                float2 df = is_arc ? (2.0f * i.b.x * du + 2.0f * i.b.y * dv) : (2.0f * i.b.x * du - dv);
                float cov = saturate(0.5f + signed_f / max(length(df), 1e-6f));
                clip(cov - 1e-4f);
                colour *= cov;
            }
            else
            {
                clip(signed_f);
            }
        }
    }

    return colour;
}
