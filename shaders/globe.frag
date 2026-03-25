#version 450
#extension GL_GOOGLE_include_directive : require
#include "shared.h"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstantBlock { PushConstants pc; };

void main()
{
    vec2 ndc = uv * 2.0 - 1.0;
    vec3 ro = pc.screenToWorld[3].xyz;
    vec3 rd = normalize(pc.screenToWorld[0].xyz * ndc.x + pc.screenToWorld[1].xyz * ndc.y + pc.screenToWorld[2].xyz);

    float b = dot(ro, rd);
    float c = dot(ro, ro) - 1.0;
    float disc = b * b - c;

    if (disc < 0.0)
    {
        outColor = vec4(vec3(0.0), 1.0);
        gl_FragDepth = 1.0;
        return;
    }

    float t = -b - sqrt(disc);
    if (t < 0.0) t = -b + sqrt(disc);
    if (t < 0.0)
    {
        outColor = vec4(vec3(0.0), 1.0);
        gl_FragDepth = 1.0;
        return;
    }

    vec3 hit = ro + rd * t;
    vec3 n = normalize(hit);
    float lat = asin(n.y);
    float lon = atan(n.z, n.x);
    float latLine = smoothstep(0.02, 0.01, abs(fract(lat * 5.7296) - 0.5));
    float lonLine = smoothstep(0.02, 0.01, abs(fract(lon * 2.8648) - 0.5));
    float grid = max(latLine, lonLine);
    float facing = max(dot(n, -rd), 0.0);
    outColor = vec4(mix(vec3(0.05), vec3(0.3, 0.7, 1.0), grid) * facing, 1.0);

    vec4 clipHit = pc.worldToScreen * vec4(hit, 1.0);
    gl_FragDepth = clipHit.z / clipHit.w;
}
