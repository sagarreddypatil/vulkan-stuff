#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_buffer_reference_uvec2 : require
#include "shared.h"

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer SceneRef
{
    GpuScene scene;
};

layout(push_constant) uniform PushConstantBlock
{
    PushConstants pc;
};

void main()
{
    SceneRef sr = SceneRef(pc.gpScene);
    gl_Position = pc.worldToScreen * vec4(sr.scene.pointPositions[gl_VertexIndex].xyz, 1.0);
    gl_PointSize = 5.0;
}
