#ifndef _LOG_H
#define _LOG_H

#ifndef LOG2FILE
#define log printf
#else
#define log(fmt, ...) do { \
    FILE *filp; \
    char path[256]; \
    sprintf(path, "%s/vmap.log", PATH_LOG); \
    filp = fopen(PATH_LOG, "a"); \
    fprintf(filp, fmt, ##__VA_ARGS__); \
    fclose(filp); \
} while (0)
#endif

#ifdef DEBUG
#define log_debug(fmt, ...) log("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define log_debug(fmt, ...) do {} while(0)
#endif

#ifdef ERROR
#define log_err(fmt, ...) log("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define log_err(fmt, ...) do {} while(0)
#endif

#endif
