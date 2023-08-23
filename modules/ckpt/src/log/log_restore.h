#ifndef _LOG_RESTORE_H
#define _LOG_RESTORE_H

#include "../log.h"

#ifdef LOG_RESTORE_FS
#define log_restore_fs log_debug
#else
#define log_restore_fs(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_POS
#define log_restore_pos log_pos
#else
#define log_restore_pos(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_SIGNALS
#define log_restore_signals log_debug
#else
#define log_restore_signals(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_VMA
#define log_restore_vma(start, end, path, nr_pages, checksum) do { \
    if (path) { \
        if (nr_pages >= 0) \
            log_debug("[%08lx, %08lx] %s pages=%d checksum=%02lx", start, end, path, nr_pages, checksum); \
        else \
            log_debug("[%08lx, %08lx] %s checksum=%02lx", start, end, path, checksum); \
    } else { \
        if (nr_pages >= 0) \
            log_debug("[%08lx, %08lx] pages=%d checksum=%02lx", start, end, nr_pages, checksum); \
        else \
            log_debug("[%08lx, %08lx] checksum=%02lx", start, end, checksum); \
    } \
} while (0)
#else
#define log_restore_vma(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_MEM
#define log_restore_mem log_debug
#else
#define log_restore_mem(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_CRED
#define log_restore_cred log_debug
#else
#define log_restore_cred(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_REGS
#define log_restore_regs log_debug
#else
#define log_restore_regs(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_FPU
#define log_restore_fpu log_debug
#else
#define log_restore_fpu(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_FILE
#define log_restore_file log_debug
#else
#define log_restore_file(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_FILES
#define log_restore_files log_debug
#else
#define log_restore_files(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_MISC
#define log_restore_misc log_debug
#else
#define log_restore_misc(...) do {} while (0)
#endif

#endif
