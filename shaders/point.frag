#version 450

layout(location = 0) out vec4 outColor;

void main()
{
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    if (dot(d, d) > 1.0) discard;
    outColor = vec4(1.0);
    gl_FragDepth = gl_FragCoord.z;
}
