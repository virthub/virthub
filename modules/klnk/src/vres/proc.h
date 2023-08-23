#ifndef _PROC_H
#define _PROC_H

#include "msg.h"
#include "sem.h"
#include "shm.h"
#include "tsk.h"
#include "ipc.h"
#include "mig.h"
#include "util.h"
#include "dump.h"
#include "trace.h"
#include "restore.h"
#include "resource.h"

#ifdef SHOW_PROC
#define LOG_PROC
#endif

#include "log_proc.h"

#ifdef TRACE_REQ
#define trace_req(req) vres_trace(req)
#else
#define trace_req(...) do {} while (0)
#endif

int vres_proc_local(vres_arg_t *arg);
vres_reply_t *vres_proc(vres_req_t *req, int flags);

#endif
