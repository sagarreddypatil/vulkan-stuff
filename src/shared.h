#ifndef SHARED_H
#define SHARED_H

#ifdef __cplusplus
#include <glm/glm.hpp>
using vec4 = glm::vec4;
using mat4 = glm::mat4;
#endif

struct PushConstants
{
    mat4 screenToWorld;
};

#endif
