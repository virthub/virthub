#ifndef _LOG_H
#define _LOG_H

#include <errno.h>

#define FLAG2STR
#define LOG_STR_LEN   8192
#define LOG_BUF_LEN   256 // bytes
#define LOG_WIDTH     16  // bytes
#define LOG_VERSIONS  16
#define LOG_DIFF_BITS 0

#define INFO_DEBUG
#define INFO_WARNING

#ifdef DEBUG
#define log_print printf
#define log_debug(fmt, ...) log_print("[debug] %s: " fmt "\n", __func__, ##__VA_ARGS__)
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

#define log_func(fmt, ...) log_ln("[info] %s: " fmt, __func__, ##__VA_ARGS__)
#define log_op(resource) log(", op=%s", log_get_op(vres_get_op(resource)))
#define log_str(str, fmt, ...) sprintf(str + strlen(str), fmt, ##__VA_ARGS__)
#define log_op_ln(resource) log(", op=%s\n", log_get_op(vres_get_op(resource)))

#define log_buf(buf, len, truncate) do { \
    int _i; \
    char *_p; \
    int _cnt = 0; \
    int _total = 0; \
    bool _truncate = truncate; \
    char _tmp[LOG_STR_LEN] = {0}; \
    if (_truncate) \
        _total = len > LOG_BUF_LEN ? LOG_BUF_LEN : len; \
    else \
        _total = len; \
    assert(buf); \
    sprintf(_tmp, "  |-> %04d~%04d: |", 0, LOG_WIDTH - 1); \
    _p = _tmp + strlen(_tmp); \
    for (_i = 0; _i < _total; _i++) { \
        size_t _l; \
        unsigned char _ch = (buf)[_i]; \
        sprintf(_p, "0x%02x|", _ch); \
        _l = strlen(_tmp); \
        assert(_l < LOG_STR_LEN); \
        _p = &_tmp[_l]; \
        _cnt++; \
        if ((_cnt >= LOG_WIDTH) || (_i == _total - 1)) { \
            *_p++ = '\n'; \
            if (_i < _total - 1) { \
                if (!_truncate && ((_i + 1) % LOG_BUF_LEN) == 0) { \
                    *_p = '\0'; \
                    log("%s", _tmp); \
                    *_tmp = '\0'; \
                    _p = _tmp; \
                } \
                sprintf(_p, "  |-> %04d~%04d: |", _i + 1, _i + LOG_WIDTH); \
                _p = _tmp + strlen(_tmp); \
                _cnt = 0; \
            } \
        } \
    } \
    *_p = '\0'; \
    log("%s", _tmp); \
} while (0)

#define log_diff(diff) do { \
    int _i, _j; \
    char _tmp[LOG_STR_LEN] = {0}; \
    const int _diff_bits = LOG_DIFF_BITS; \
    log_str(_tmp, "[diff] %s:\n", __func__); \
    for (_i = 0; _i < LOG_VERSIONS; _i++) { \
        if (_diff_bits) \
            log_str(_tmp, "  |-> diff%02d: |", _i + 1); \
        else \
            log_str(_tmp, "  |-> diff%02d:", _i + 1); \
        for (_j = 0; _j < VRES_LINE_MAX; _j++) { \
            if (_diff_bits) \
                log_str(_tmp, "%d|", diff[_i][_j]); \
            else if (diff[_i][_j]) \
                log_str(_tmp, " %d", _j); \
        } \
        log_str(_tmp, "\n"); \
    } \
    log("%s", _tmp); \
} while (0)

#define log_resource_str(resource, str) do { \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) \
        log_str(str, "[info] %s: id=%d (cls=%s, key=%d, src=%d, off=%lu, flg=%s)", __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), log_get_flags(vres_get_flags(resource))); \
    else \
        log_str(str, "[info] %s: id=%d (cls=%s, key=%d, src=%d)", __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource)); \
} while (0)

#if defined(DEBUG) && defined(INFO_DEBUG)
#define log_resource_entry_debug(resource, entry, size, format, ...) do { \
    int _i; \
    char _tmp[LOG_STR_LEN] = {0}; \
    log_resource_str(resource, _tmp); \
    log_str(_tmp, ", entry=|"); \
    for(_i = 0; _i < size; _i++) \
        log_str(_tmp, "%lx|", entry[_i]); \
    log_ln("%s, " format, _tmp, ##__VA_ARGS__); \
} while (0)
#else
#define log_resource_entry_debug(...) do {} while (0)
#endif

#define log_resource_ln(resource) do { \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) \
        log_ln("[info] %s: id=%d (cls=%s, key=%d, src=%d, off=%lu, flg=%s)", __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), log_get_flags(vres_get_flags(resource))); \
    else \
        log_ln("[info] %s: id=%d (cls=%s, key=%d, src=%d)", __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource)); \
} while (0)

#define log_resource_info(resource, fmt, ...) do { \
    if (resource->cls == VRES_CLS_SHM) \
        log_ln("[info] %s: id=%d (cls=%s, key=%d, src=%d, off=%lu, flg=%s), " fmt, __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), log_get_flags(vres_get_flags(resource)), ##__VA_ARGS__); \
    else \
        log_ln("[info] %s: id=%d (cls=%s, key=%d, src=%d), " fmt, __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), ##__VA_ARGS__); \
} while (0)

#ifdef ERROR
#define log_err(fmt, ...) log_alarm("[error] %s: " fmt, __func__, ##__VA_ARGS__)
#define log_resource_err(resource, fmt, ...) do { \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) \
        log_alarm("[error] %s: id=%d (cls=%s, key=%d, src=%d, off=%lu) ==> " fmt, __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), ##__VA_ARGS__); \
    else \
        log_alarm("[error] %s: id=%d (cls=%s, key=%d, src=%d) ==> " fmt,  __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), ##__VA_ARGS__); \
    assert(0); \
} while (0)
#else
#define log_err(...) do {} while (0)
#define log_resource_err(...) do {} while (0)
#endif

#if defined(WARNING) && defined(INFO_WARNING)
#define log_warning log_func
#define log_resource_warning(resource, fmt, ...) do { \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) \
        log_ln("[warning] %s: id=%d (cls=%s, key=%d, src=%d, off=%lu) ==> " fmt, \
               __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), ##__VA_ARGS__); \
    else \
        log_ln("[warning] %s: id=%d (cls=%s, key=%d, src=%d) ==> " fmt, \
               __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), ##__VA_ARGS__); \
} while (0)
#else
#define log_warning(...) do {} while (0)
#define log_resource_warning(...) do {} while (0)
#endif

#if defined(DEBUG) && defined(INFO_DEBUG)
#define log_resource_debug(resource, fmt, ...) do { \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) \
        log_ln("[debug] %s: id=%d (cls=%s, key=%d, src=%d, off=%lu) ==> " fmt, \
               __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), (unsigned long)vres_get_off(resource), ##__VA_ARGS__); \
    else \
        log_ln("[debug] %s: id=%d (cls=%s, key=%d, src=%d) ==> " fmt, \
               __func__, (resource)->owner, log_get_cls((resource)->cls), (resource)->key, vres_get_id(resource), ##__VA_ARGS__); \
} while (0)
#else
#define log_resource_debug(...) do {} while (0)
#endif

#define log_get_cls(cls) (((cls) == VRES_CLS_MSG) ? "MSG" : (((cls) == VRES_CLS_SEM) ? "SEM" : (((cls) == VRES_CLS_SHM) ? "SHM" : (((cls) == VRES_CLS_TSK) ? "TSK" : "UNKNOWN"))))

char *log_get_op(int op);
char *log_get_err(int err);
char *log_get_flags(int flags);
char *log_get_shm_cmd(int cmd);

#endif
