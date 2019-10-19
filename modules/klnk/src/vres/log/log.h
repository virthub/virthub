#ifndef _LOG_H
#define _LOG_H

#include <errno.h>

#define FLAG2STR
#define LOG_STR_LEN 1024

#ifdef DEBUG
#define log_print printf
#define log_debug(fmt, ...) log_print("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define log_print(...) do {} while(0)
#define log_debug(...) do {} while(0)
#endif

#if !defined(LOG2FILE) || !defined(DEBUG)
#define log log_print
#define log_ln(fmt, ...) log_print(fmt "\n", ##__VA_ARGS__)
#define log_alarm(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
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
#define log_alarm log_ln
#endif

#define log_func(fmt, ...) log_ln("%s: " fmt, __func__, ##__VA_ARGS__)
#define log_op(resource) log(", op=%s", log_get_op(vres_get_op(resource)))
#define log_str(str, fmt, ...) sprintf(str + strlen(str), fmt, ##__VA_ARGS__)
#define log_op_ln(resource) log(", op=%s\n", log_get_op(vres_get_op(resource)))

#define log_resource_str(resource, str) do { \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) \
        log_str(str, "%s@%d: cls=%s, key=%d, src=%d, off=%lu, flg=%s", __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), log_get_flags(vres_get_flags(resource))); \
    else \
        log_str(str, "%s@%d: cls=%s, key=%d, src=%d", __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource)); \
} while (0)

#define log_resource_ln(resource) do { \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) \
        log_ln("%s@%d: cls=%s, key=%d, src=%d, off=%lu, flg=%s", __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), log_get_flags(vres_get_flags(resource))); \
    else \
        log_ln("%s@%d: cls=%s, key=%d, src=%d", __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource)); \
} while (0)

#define log_resource_info(resource, fmt, ...) do { \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) \
        log_ln("%s@%d: cls=%s, key=%d, src=%d, off=%lu, flg=%s, " fmt, __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), log_get_flags(vres_get_flags(resource)), ##__VA_ARGS__); \
    else \
        log_ln("%s@%d: cls=%s, key=%d, src=%d, " fmt, __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), ##__VA_ARGS__); \
} while (0)

#ifdef ERROR
#define log_err(fmt, ...) log_alarm("error: in function %s " fmt " !!!", __func__, ##__VA_ARGS__)
#define log_resource_err(resource, fmt, ...) do { \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) \
        log_err("(owner=%d, cls=%s, key=%d, src=%d, off=%lu), " fmt, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), ##__VA_ARGS__); \
    else \
        log_err("(owner=%d, cls=%s, key=%d, src=%d), " fmt, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), ##__VA_ARGS__); \
    assert(0); \
} while (0)
#else
#define log_err(...) do {} while (0)
#define log_resource_err(...) do {} while (0)
#endif

#ifdef WARNING
#define log_warning log_func
#define log_resource_warning(resource, fmt, ...) do { \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) \
        log_ln("warning: in function %s (owner=%d, clk=%s, key=%d, src=%d, off=%lu), " fmt, \
               __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), ##__VA_ARGS__); \
    else \
        log_ln("warning: in function %s (owner=%d, clk=%s, key=%d, src=%d), " fmt, \
               __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), ##__VA_ARGS__); \
} while (0)
#else
#define log_warning(...) do {} while (0)
#define log_resource_warning(...) do {} while (0)
#endif

#define log_get_cls(cls) (((cls) == VRES_CLS_MSG) ? "MSG" : (((cls) == VRES_CLS_SEM) ? "SEM" : (((cls) == VRES_CLS_SHM) ? "SHM" : (((cls) == VRES_CLS_TSK) ? "TSK" : "UNKNOWN"))))

char *log_get_op(int op);
char *log_get_err(int err);
char *log_get_flags(int flags);
char *log_get_shm_cmd(int cmd);

#endif
