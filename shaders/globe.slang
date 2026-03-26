#include "shared.h"

struct GlobeVertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]]
    float2 uv : TEXCOORD0;
};

struct GlobeFragmentOutput
{
    [[vk::location(0)]]
    float4 color : SV_Target0;
    float depth : SV_Depth;
};

[shader("vertex")]
GlobeVertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };

    float2 clipPos = positions[vertexId];

    GlobeVertexOutput output;
    output.uv = (clipPos + 1.0) * 0.5;
    output.position = float4(clipPos, 0.0, 1.0);
    return output;
}

[shader("fragment")]
GlobeFragmentOutput fragmentMain([[vk::location(0)]] float2 uv: TEXCOORD0, uniform PushConstants pc)
{
    GpuScene* scene = (GpuScene*)pc.gpScene;

    float2 ndc = uv * 2.0 - 1.0;
    float3 ro = mul(scene.screenToWorld, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float3 rd = normalize(mul(scene.screenToWorld, float4(ndc.x, ndc.y, 1.0, 0.0)).xyz);

    float b = dot(ro, rd);
    float c = dot(ro, ro) - 1.0;
    float disc = b * b - c;

    GlobeFragmentOutput output;
    if (disc < 0.0)
    {
        output.color = float4(0.0, 0.0, 0.0, 1.0);
        output.depth = 1.0;
        return output;
    }

    float s = sqrt(disc);
    float t = -b - s;
    if (t < 0.0)
        t = -b + s;

    if (t < 0.0)
    {
        output.color = float4(0.0, 0.0, 0.0, 1.0);
        output.depth = 1.0;
        return output;
    }

    float3 hit = ro + rd * t;
    float3 n = normalize(hit);
    float lat = asin(n.y);
    float lon = atan2(n.z, n.x);
    float latLine = smoothstep(0.02, 0.01, abs(frac(lat * 5.7296) - 0.5));
    float lonLine = smoothstep(0.02, 0.01, abs(frac(lon * 2.8648) - 0.5));
    float grid = max(latLine, lonLine);
    float facing = max(dot(n, -rd), 0.0);

    output.color =
        float4(lerp(float3(0.05, 0.05, 0.05), float3(0.3, 0.7, 1.0), grid) * facing, 1.0);

    float4 clipHit = mul(scene.worldToScreen, float4(hit, 1.0));
    output.depth = clipHit.z / clipHit.w;
    return output;
}
