#pragma once
#include <cstdarg>
#include <cstdio>
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
inline int __android_log_print(int, const char* tag, const char* format, ...) {
    std::fprintf(stdout, "[%s] ", tag);
    va_list args; va_start(args, format);
    const int count = std::vfprintf(stdout, format, args);
    va_end(args); std::fputc('\n', stdout);
    return count;
}
