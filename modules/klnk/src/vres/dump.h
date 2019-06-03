#ifndef _DUMP_H
#define _DUMP_H

#include <vres.h>
#include "ckpt.h"
#include "file.h"

#ifdef SHOW_DUMP
#define LOG_DUMP
#define LOG_DUMP_FILE
#define LOG_DUMP_TASK
#define LOG_DUMP_RESOURCE
#endif

#include "log_dump.h"

int vres_dump(vres_t *resource, int flags);

#endif
