#ifndef SHARED_H
#define SHARED_H

#ifdef __cplusplus
    #include <glm/glm.hpp>
using vec4 = glm::vec4;
using mat4 = glm::mat4;
#endif

#define SCENE_POINT_COUNT 10000
#define SCENE_ORBIT_RADIUS 1.05

struct PushConstants
{
    mat4 screenToWorld;
    mat4 worldToScreen;
};

struct SceneData
{
    vec4 pointPositions[SCENE_POINT_COUNT];
};

#endif
