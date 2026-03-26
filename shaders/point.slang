#include "shared.h"

struct PointVertexOutput
{
    float4 position : SV_Position;
    float pointSize : SV_PointSize;
};

struct PointFragmentOutput
{
    [[vk::location(0)]]
    float4 color : SV_Target0;
    float depth : SV_Depth;
};

[shader("vertex")]
PointVertexOutput vertexMain(uint vertexId: SV_VertexID, uniform PushConstants pc)
{
    GpuScene* scene = (GpuScene*)pc.gpScene;

    PointVertexOutput output;
    output.position = mul(scene.worldToScreen, float4(scene.pointPositions[vertexId].xyz, 1.0));
    output.pointSize = 5.0;
    return output;
}

[shader("fragment")]
PointFragmentOutput fragmentMain(float4 position: SV_Position, float2 pointCoord: SV_PointCoord)
{
    float2 d = pointCoord * 2.0 - 1.0;
    if (dot(d, d) > 1.0)
        discard;

    PointFragmentOutput output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    output.depth = position.z;
    return output;
}
