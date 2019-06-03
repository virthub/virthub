#ifndef _LOG_LINE_H
#define _LOG_LINE_H

#include "log.h"

#ifdef LOG_LINE_DIFF
#define log_line_diff(line_num, prev, curr, str) do { \
    int n; \
    const int cnt = LOG_NR_BYTES / sizeof(unsigned long); \
    unsigned long *ptr; \
    ptr = (unsigned long *)(prev); \
    log_str(str, "line_num=%d, prev=|", line_num); \
    for (n = 0; n < cnt - 1; n++) \
        log_str(str, "%lx|", *ptr++); \
    log_str(str, "%lx|", *ptr); \
    ptr = (unsigned long *)(curr); \
    log_str(str, ", curr=|"); \
    for (n = 0; n < cnt; n++) \
        log_str(str, "%lx|", *ptr++); \
} while (0)
#else
#define log_line_diff(...) do {} while (0)
#endif

#define log_line_info(resource, nr_lines, total, str) log_str(str, "line_info={new:%d, total:%d}", nr_lines, total)

#ifdef LOG_LINE_NUM
#define log_line_num(resource, lines, nr_lines, str) do { \
    log_owner(resource); \
    if (nr_lines > 0) { \
        int i; \
        log_str(str, ", line=["); \
        for (i = 0; i < nr_lines - 1; i++) \
            log_str(str, "%d, ", (lines)[i].num); \
        log_str(str, "%d]", (lines)[i].num); \
    } else \
        log_str(str, ", line=[empty]"); \
} while (0)
#else
#define log_line_num(...) do {} while (0)
#endif

#ifdef LOG_LINE_CONTENT
#define log_line_content(resource, lines, nr_lines, str) do { \
    int n = LOG_NR_LINES < nr_lines ? LOG_NR_LINES : nr_lines; \
    if (n > 0) { \
        int i; \
        int left = -1; \
        const int cnt = LOG_NR_BYTES / sizeof(unsigned long); \
        log_owner(resource); \
        for (i = 0; i < n; i++) { \
            int j, pos = -1; \
            int min = VRES_LINE_MAX; \
            unsigned long *ptr; \
            for (j = 0; j < nr_lines; j++) { \
                int num = (lines)[j].num; \
                if ((num > left) && (num < min)) { \
                    min = num; \
                    pos = j; \
                } \
            } \
            if (pos < 0) \
                break; \
            ptr = (unsigned long *)(lines)[pos].buf; \
            log_str(str, ", ln_%d=|", (lines)[pos].num); \
            for (j = 0; j < cnt - 1; j++) \
                log_str(str, "%lx|", *ptr++); \
            log_str(str, "%lx|", *ptr); \
            left = min; \
        } \
    } \
} while (0)
#else
#define log_line_content(...) do {} while (0)
#endif

#ifdef LOG_LINES
#define log_lines(resource, lines, nr_lines, total) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    log_resource_str(resource, tmp); \
    log_line_info(resource, nr_lines, total, tmp); \
    log_line_num(resource, lines, nr_lines, tmp); \
    log_line_content(resource, lines, nr_lines, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_lines(...) do {} while (0)
#endif

#endif
