#include "util.h"

#include <cstdarg>
#include <ctime>

void Internal_LogWrite(
    FILE* out, char level, const char* const file, const I32 line, const char* fmt, ...)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);

    struct tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &ts.tv_sec);
#else
    gmtime_r(&ts.tv_sec, &tm);
#endif

    fprintf(out,
            "%c%04d%02d%02d %02d:%02d:%02d.%06ld %s:%d] ",
            level,
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            ts.tv_nsec / 1000L,
            file,
            line);
    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);
    fputc('\n', out);
}
