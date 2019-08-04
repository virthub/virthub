#ifndef _LOG_CKPT_H
#define _LOG_CKPT_H

#include "log.h"

#ifdef LOG_CKPT
#define log_ckpt log_debug
#else
#define log_ckpt(...) do {} while (0)
#endif

#ifdef LOG_CKPT_CLEAR
#define log_ckpt_clear log_resource_ln
#else
#define log_ckpt_clear(...) do {} while (0)
#endif

#ifdef LOG_CKPT_SUSPEND
#define log_ckpt_suspend log_resource_ln
#else
#define log_ckpt_suspend(...) do {} while (0)
#endif

#ifdef LOG_CKPT_CHECK_DIR
#define log_ckpt_check_dir(path) log_func("path=%s", path)
#else
#define log_ckpt_check_dir(...) do {} while (0)
#endif

#ifdef LOG_CKPT_NOTIFY_OWNER
#define log_ckpt_notify_owner log_resource_ln
#else
#define log_ckpt_notify_owner(...) do {} while (0)
#endif

#ifdef LOG_CKPT_NOTIFY_MEMBERS
#define log_ckpt_notify_members log_resource_ln
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
