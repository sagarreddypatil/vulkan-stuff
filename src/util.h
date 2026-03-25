#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef int8_t I8;
typedef int16_t I16;
typedef int32_t I32;
typedef int64_t I64;

#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Max(a, b) ((a) > (b) ? (a) : (b))
#define Cdiv(a, b) (((a) + (b) - 1) / (b))

static constexpr const char* PathBasename(const char* path)
{
    const char* base = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    return base;
}

#define __FILENAME__ PathBasename(__FILE__)

void Internal_LogWrite(
    FILE* out, char level, const char* const file, const I32 line, const char* fmt, ...);

#define LOG_INFO(fmt, ...)                                                                         \
    do                                                                                             \
    {                                                                                              \
        Internal_LogWrite(stdout, 'I', __FILENAME__, __LINE__, fmt, ##__VA_ARGS__);                \
    } while (0)

#define LOG_ERR(fmt, ...)                                                                          \
    do                                                                                             \
    {                                                                                              \
        Internal_LogWrite(stderr, 'E', __FILENAME__, __LINE__, fmt, ##__VA_ARGS__);                \
        fflush(stderr);                                                                            \
    } while (0)

#define LOG_FATAL(fmt, ...)                                                                        \
    do                                                                                             \
    {                                                                                              \
        Internal_LogWrite(stderr, 'F', __FILENAME__, __LINE__, fmt, ##__VA_ARGS__);                \
        fflush(stdout);                                                                            \
        fflush(stderr);                                                                            \
        abort();                                                                                   \
    } while (0)

template <typename T, size_t N>
char (*Internal_ArrayCountHelper(T (&)[N]))[N];
#define ARRAY_COUNT(arr) (sizeof(*Internal_ArrayCountHelper(arr)))

#define CONCAT2(a, b) a##b
#define CONCAT(a, b) CONCAT2(a, b)

#define ON_SCOPE_EXIT(stmt)                                                                        \
    auto CONCAT(scopeExitFunc_, __LINE__) = [&]() { stmt; };                                       \
    struct CONCAT(ScopeExit_, __LINE__)                                                            \
    {                                                                                              \
        ~CONCAT(ScopeExit_, __LINE__)() { f(); }                                                   \
        decltype(CONCAT(scopeExitFunc_, __LINE__)) f;                                              \
    } CONCAT(scopeExit_, __LINE__)                                                                 \
    {                                                                                              \
        CONCAT(scopeExitFunc_, __LINE__)                                                           \
    }

struct Vec3
{
    float x;
    float y;
    float z;
};

static Vec3 operator+(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vec3 operator-(Vec3 a)
{
    return {-a.x, -a.y, -a.z};
}

static Vec3 operator*(Vec3 v, float s)
{
    return {v.x * s, v.y * s, v.z * s};
}

static float Dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 Cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static Vec3 Normalize(Vec3 v)
{
    float invLen = 1.0f / sqrtf(Dot(v, v));
    return v * invLen;
}

struct alignas(16) float4
{
    float x;
    float y;
    float z;
    float w;

    float4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    float4(Vec3 xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
};

struct alignas(16) float4x4
{
    float4 c0;
    float4 c1;
    float4 c2;
    float4 c3;

    float4x4(float4 c0, float4 c1, float4 c2, float4 c3) : c0(c0), c1(c1), c2(c2), c3(c3) {}
};

static float4 operator+(float4 a, float4 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

static float4 operator*(float4 v, float s)
{
    return {v.x * s, v.y * s, v.z * s, v.w * s};
}

static float4 Mul(const float4x4& m, float4 v)
{
    return m.c0 * v.x + m.c1 * v.y + m.c2 * v.z + m.c3 * v.w;
}

static float4x4 Mul(const float4x4& a, const float4x4& b)
{
    return float4x4(Mul(a, b.c0), Mul(a, b.c1), Mul(a, b.c2), Mul(a, b.c3));
}