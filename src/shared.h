#ifndef SHARED_H
#define SHARED_H

#ifdef __cplusplus
    #include <cstdint>
    #include <glm/glm.hpp>
using vec4 = glm::vec4;
using mat4 = glm::mat4;
using uvec2 = glm::uvec2;
#endif

#define SCENE_POINT_COUNT 10000
#define SCENE_ORBIT_RADIUS 1.05

struct PushConstants
{
    mat4 screenToWorld;
    mat4 worldToScreen;
    uvec2 gpScene;
    uvec2 _pad0;
};

struct GpuScene
{
    vec4 pointPositions[SCENE_POINT_COUNT];
};

#endif
