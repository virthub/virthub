#ifndef _DUMP_H
#define _DUMP_H

#include "io.h"
#include "map.h"
#include "sys.h"
#include "ckpt.h"
#include "util.h"
#include "debug.h"

#ifdef SHOW_DUMP
#define LOG_DUMP_FS
#define LOG_DUMP_POS
#define LOG_DUMP_CPU
#define LOG_DUMP_EXT
#define LOG_DUMP_FPU
#define LOG_DUMP_MEM
#define LOG_DUMP_VMA
#define LOG_DUMP_CRED
#define LOG_DUMP_FILE
#define LOG_DUMP_FILES
#define LOG_DUMP_SIGNALS
#endif

#include "log/log_dump.h"

int ckpt_dump_task(char *node, struct task_struct *tsk, ckpt_desc_t desc);

#endif
