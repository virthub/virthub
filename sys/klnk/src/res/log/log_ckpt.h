#ifndef _LOG_CKPT_H
#define _LOG_CKPT_H

#include "log.h"

#ifdef LOG_CKPT_CLEAR
#define log_ckpt_clear(resource) vres_log(resource)
#else
#define log_ckpt_clear(...) do {} while (0)
#endif

#ifdef LOG_CKPT_SUSPEND
#define log_ckpt_suspend(resource) vres_log(resource)
#else
#define log_ckpt_suspend(...) do {} while (0)
#endif

#ifdef LOG_CKPT_CHECK_DIR
#define log_ckpt_check_dir(path) log_debug("path=%s", path)
#else
#define log_ckpt_check_dir(...) do {} while (0)
#endif

#ifdef LOG_CKPT_NOTIFY_OWNER
#define log_ckpt_notify_owner(resource) vres_log(resource)
#else
#define log_ckpt_notify_owner(...) do {} while (0)
#endif

#ifdef LOG_CKPT_NOTIFY_MEMBERS
#define log_ckpt_notify_members(resource) vres_log(resource)
#else
#define log_ckpt_notify_members(...) do {} while (0)
#endif

#ifdef LOG_CKPT_RESUME_MEMBERS
#define log_ckpt_resume_members(path) log_debug("path=%s", path)
#else
#define log_ckpt_resume_members(...) do {} while (0)
#endif

#ifdef LOG_CKPT_SUSPEND_MEMBERS
#define log_ckpt_suspend_members(path) log_debug("path=%s", path)
#else
#define log_ckpt_suspend_members(...) do {} while (0)
#endif

#endif
