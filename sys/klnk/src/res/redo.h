#ifndef _REDO_H
#define _REDO_H

#include "tmp.h"
#include "trace.h"
#include "resource.h"

#ifdef SHOW_REDO
#define LOG_REDO_REQ
#endif

#include "log_redo.h"

int vres_redo(vres_t *resource, int flags);
int vres_redo_all(vres_t *resource, int flags);

#endif
