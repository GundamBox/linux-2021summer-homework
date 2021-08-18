#ifndef _COMMON_H_INCLUDED
#define _COMMON_H_INCLUDED

#include <stdio.h>

#ifdef DEBUG
#define DEBUG_PRINT(...)              \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
    } while (false)
#else
#define DEBUG_PRINT(...) \
    do {                 \
    } while (false)
#endif

#endif