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

template <U32 kCapacity>
class String
{
  public:
    constexpr String() { m_buf[0] = '\0'; }
    constexpr String(const char* const pStr)
    {
        m_size = Min(__builtin_strlen(pStr), kCapacity);
        __builtin_memcpy(m_buf, pStr, m_size);
        m_buf[m_size] = '\0';
    }

    constexpr U64 Size() const { return m_size; }
    constexpr const char* Str() const { return m_buf; }

    constexpr bool operator==(const char* const pStr) const
    {
        return __builtin_strcmp(m_buf, pStr) == 0;
    }

    template <U64 kOther>
    constexpr bool operator==(const String<kOther>& pOther) const
    {
        if (m_size != pOther.Size())
            return false;
        return __builtin_memcmp(m_buf, pOther.Str(), m_size) == 0;
    }

  private:
    char m_buf[kCapacity + 1];
    U64 m_size = 0;
};

using EzString = String<247>;

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