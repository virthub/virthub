#ifndef _RESTORE_H
#define _RESTORE_H

#include "io.h"
#include "map.h"
#include "sys.h"
#include "util.h"
#include "ckpt.h"
#include "debug.h"

#ifdef SHOW_RESTORE
#define LOG_RESTORE_FS
#define LOG_RESTORE_POS
#define LOG_RESTORE_CPU
#define LOG_RESTORE_EXT
#define LOG_RESTORE_FPU
#define LOG_RESTORE_MEM
#define LOG_RESTORE_VMA
#define LOG_RESTORE_CRED
#define LOG_RESTORE_FILE
#define LOG_RESTORE_FILES
#define LOG_RESTORE_SIGNALS
#endif

#include "log/log_restore.h"

int ckpt_restore_task(char *node, pid_t gpid, ckpt_desc_t desc);

#endif
