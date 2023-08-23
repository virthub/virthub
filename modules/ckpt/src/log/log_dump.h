#ifndef _LOG_DUMP_H
#define _LOG_DUMP_H

#include "../log.h"

#ifdef LOG_DUMP_FS
#define log_dump_fs log_debug
#else
#define log_dump_fs(...) do {} while (0)
#endif

#ifdef LOG_DUMP_POS
#define log_dump_pos log_pos
#else
#define log_dump_pos(...) do {} while (0)
#endif

#ifdef LOG_DUMP_VMA
#define log_dump_vma(start, end, path, nr_pages, checksum) do { \
    if (path) \
        log_debug("[%08lx, %08lx] %s pages=%d checksum=%02lx", start, end, path, nr_pages, checksum); \
    else \
        log_debug("[%08lx, %08lx] pages=%d checksum=%02lx", start, end, nr_pages, checksum); \
} while (0)
#else
#define log_dump_vma(...) do {} while (0)
#endif

#ifdef LOG_DUMP_FILE
#define log_dump_file log_debug
#else
#define log_dump_file(...) do {} while (0)
#endif

#ifdef LOG_DUMP_FILES
#define log_dump_files log_debug
#else
#define log_dump_files(...) do {} while (0)
#endif

#ifdef LOG_DUMP_CPU
#define log_dump_cpu log_debug
#else
#define log_dump_cpu(...) do {} while (0)
#endif

#ifdef LOG_DUMP_FPU
#define log_dump_fpu log_debug
#else
#define log_dump_fpu(...) do {} while (0)
#endif

#ifdef LOG_DUMP_MEM
#define log_dump_mem log_debug
#else
#define log_dump_mem(...) do {} while (0)
#endif

#ifdef LOG_DUMP_CRED
#define log_dump_cred log_debug
#else
#define log_dump_cred(...) do {} while (0)
#endif

#ifdef LOG_DUMP_SIGNALS
#define log_dump_signals log_debug
#else
#define log_dump_signals(...) do {} while (0)
#endif

#ifdef LOG_DUMP_EXT
#define log_dump_ext log_debug
#else
#define log_dump_ext(...) do {} while (0)
#endif

#endif
