#ifndef _LOG_H
#define _LOG_H

#ifndef LOG2FILE
#define log printf
#define log_ln(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#else
#define log(fmt, ...) do { \
    FILE *filp = fopen(log_name, "a"); \
    fprintf(filp, fmt, ##__VA_ARGS__); \
    fclose(filp); \
} while (0)
#define log_ln(fmt, ...) do { \
    FILE *filp = fopen(log_name, "a"); \
    fprintf(filp, fmt "\n", ##__VA_ARGS__); \
    fclose(filp); \
} while (0)
#endif

#define log_func(fmt, ...) log_ln("%s: " fmt, __func__, ##__VA_ARGS__)
#define log_owner(resource) log("%s@%d: ", __func__, (resource)->owner)
#define log_op(resource) log(", op=%s", log_get_op(vres_get_op(resource)))
#define log_op_ln(resource) log(", op=%s\n", log_get_op(vres_get_op(resource)))

#define log_resource(resource) log("%s@%d: key=%d, src=%d, off=%lu, flg=%x", \
    __func__, (resource)->owner, (resource)->key, \
    vres_get_id(resource), vres_get_off(resource), vres_get_flags(resource))

#define log_resource_ln(resource) log_ln("%s@%d: key=%d, src=%d, off=%lu, flg=%x", \
    __func__, (resource)->owner, (resource)->key, \
    vres_get_id(resource), vres_get_off(resource), vres_get_flags(resource))

#ifdef DEBUG
#define log_debug log_func
#else
#define log_debug(...) do {} while(0)
#endif

#ifdef ERROR
#define log_resource_err(resource, fmt, ...) do { \
    log_resource(resource); \
    log_ln(", " fmt, ##__VA_ARGS__); \
} while (0)
#define log_err log_func
#else
#define log_resource_err(...) do {} while (0)
#define log_err(...) do {} while (0)
#endif

#ifdef WARNING
#define log_resource_warning(resource, fmt, ...) do { \
    log("warning: in function %s (owner=%d, key=%d, src=%d, off=%lu, flg=%x)", \
        __func__, (resource)->owner, (resource)->key, vres_get_id(resource), vres_get_off(resource), vres_get_flags(resource)); \
    log_ln(", " fmt, ##__VA_ARGS__); \
} while (0)
#define log_warning log_func
#else
#define log_resource_warning(...) do {} while (0)
#define log_warning(...) do {} while (0)
#endif

char *log_get_op(int op);
char *log_get_shmop(int op);

#endif
