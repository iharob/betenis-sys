#ifndef __bt_william_hill_DEBUG_H__
#define __bt_william_hill_DEBUG_H__

#include <stdio.h>

#include <time.h>

#include <pthread.h>
#include <syslog.h>
#define log(...) do {                                                        \
        fprintf(stderr, ": ");                                          \
        fprintf(stderr, __VA_ARGS__);                                          \
    } while (0)
#endif /* __bt_william_hill_DEBUG_H__ */

