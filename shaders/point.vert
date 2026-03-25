#version 450
#extension GL_GOOGLE_include_directive : require
#include "shared.h"

layout(push_constant) uniform PushConstantBlock { PushConstants pc; };
layout(set = 0, binding = 0) readonly buffer SceneBuffer { SceneData scene; };

void main()
{
    gl_Position = pc.worldToScreen * vec4(scene.pointPositions[gl_VertexIndex].xyz, 1.0);
    gl_PointSize = 5.0;
}
