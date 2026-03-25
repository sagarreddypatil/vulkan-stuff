#ifndef SHARED_H
#define SHARED_H

#define kScenePointCountValue 10000
#define kSceneOrbitRadiusValue 1.05f

#ifdef __cplusplus
    #include "util.h"

static constexpr U32 kScenePointCount = kScenePointCountValue;
static constexpr float kSceneOrbitRadius = kSceneOrbitRadiusValue;
#else
typedef uint64_t U64;
static const uint kScenePointCount = kScenePointCountValue;
static const float kSceneOrbitRadius = kSceneOrbitRadiusValue;
#endif

struct PushConstants
{
    float4x4 screenToWorld;
    float4x4 worldToScreen;
    U64 gpScene;
};

struct GpuScene
{
    float4 pointPositions[kScenePointCount];
};

#undef kScenePointCountValue
#undef kSceneOrbitRadiusValue

#endif
