#ifndef _RESTORE_H
#define _RESTORE_H

#include <vres.h>
#include <stdlib.h>
#include "dump.h"
#include "tsk.h"
#include "tmp.h"

#ifdef SHOW_RESTORE
#define LOG_RESTORE
#define LOG_RESTORE_FILE
#define LOG_RESTORE_TASK
#define LOG_RESTORE_RESOURCE
#endif

#include "log_restore.h"

int vres_restore(vres_t *resource, int flags);

#endif
