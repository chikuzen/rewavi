#ifndef REWAVI_COMMON_H
#define REWAVI_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <io.h>
#include <fcntl.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vfw.h>

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 3
#endif

#define VERSION "0.01"

typedef enum {
    LOG_INFO,
    LOG_ERROR,
    LOG_WARNING
} log_t;

int rewavi_log(const char *name, log_t level, const char *message, ...);

#endif /* REWAVI_COMMON_H */