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

/* The lines PₚᵣₑᵥP₀ and P₀Pₙₑₓₜ, both offset by ±½w, intersect each other at:
 *
 *   Pᵢ = P₀ ± w · ½q⃑ᵢ.
 *
 * Where:
 *
 *   q⃑ᵢ = q̂ₚᵣₑᵥ⊥ + tan(½θ) · -q̂ₚᵣₑᵥ
 *   θ  = ∠PₚᵣₑᵥP₀Pₙₑₓₜ
 *   q⃑ₚᵣₑᵥ = P₀ - Pₚᵣₑᵥ */
void main(float2 position : POSITION, float2 prev : PREV, float2 next : NEXT, out struct output o)
{
    float2 q_prev, q_next, v_p, q_i;
    float2x2 geom;
    float l;

    o.stroke_transform = float2x2(transform_rtx.xy, transform_rty.xy) * stroke_width * 0.5f;

    geom = float2x2(transform_geometry._11_21, transform_geometry._12_22);
    q_prev = normalize(mul(geom, prev));
    q_next = normalize(mul(geom, next));

    /* tan(½θ) = sin(θ) / (1 + cos(θ))
     *         = (q̂ₚᵣₑᵥ⊥ · q̂ₙₑₓₜ) / (1 + (q̂ₚᵣₑᵥ · q̂ₙₑₓₜ)) */
    v_p = float2(-q_prev.y, q_prev.x);
    l = -dot(v_p, q_next) / (1.0f + dot(q_prev, q_next));
    q_i = l * q_prev + v_p;

    o.b = float4(0.0, 0.0, 0.0, 0.0);

    o.p = mul(float3(position, 1.0f), transform_geometry) + stroke_width * 0.5f * q_i;
    position = mul(float2x3(transform_rtx.xyz, transform_rty.xyz), float3(o.p, 1.0f))
            * float2(transform_rtx.w, transform_rty.w);
    o.position = float4(position + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}
