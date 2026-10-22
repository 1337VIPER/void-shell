#include "common.h"

#include <share.h>

#include <cstdarg>
#include <cstdio>

static FILE* g_logFile = nullptr;

void LogOpen(const wchar_t* path) {
    if (g_logFile) fclose(g_logFile);
    // Shared so the log can be read while the app is running.
    g_logFile = _wfsopen(path, L"w", _SH_DENYNO);
}

void Log(const char* format, ...) {
    char line[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    OutputDebugStringA(line);
    OutputDebugStringA("\n");
    if (g_logFile) {
        fprintf(g_logFile, "%s\n", line);
        fflush(g_logFile);
    }
}
