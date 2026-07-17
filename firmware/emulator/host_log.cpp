// Host implementation of the firmware's _log / _debug logging helpers.
#include <cstdarg>
#include <cstdio>
#include "app/Log/Log.h"

void _log(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
    fflush(stdout);
}

void _debug(const char *format, ...)
{
#ifdef EMU_DEBUG_LOG
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
    fflush(stdout);
#else
    (void)format;
#endif
}
