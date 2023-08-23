#ifndef _EVAL_H
#define _EVAL_H

#include <stdio.h>
#include <sys/time.h>

#define EVAL_INTERVAL 256

#define eval_log(fmt, ...) printf("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#define eval_time(start, end) (((end)->tv_sec - (start)->tv_sec) * 1000000 + (end)->tv_usec - (start)->tv_usec)

#ifdef EVALUATE
#define EVAL_DECL(name) \
    long name##_time= 0; \
    long name##_count = 0; \
    struct timeval name##_start

#define EVAL_START(name) gettimeofday(&name##_start, NULL)

#define EVAL_END(name) do { \
    struct timeval end; \
    gettimeofday(&end, NULL); \
    name##_time += eval_time(&name##_start, &end); \
    name##_count++; \
    if (name##_count % EVAL_INTERVAL == 0) \
        eval_log("time=%ld (usec)", name##_time / name##_count); \
} while (0)

#else
#define EVAL_DECL(name)
#define EVAL_START(name) do {} while (0)
#define EVAL_END(name) do {} while (0)
#endif

#endif
