#ifndef _LOG_LOCK_H
#define _LOG_LOCK_H

#ifdef LOG_LOCK
#define log_lock log_resource_ln
#else
#define log_lock(...) do {} while (0)
#endif

#ifdef LOG_UNLOCK
#define log_unlock log_resource_ln
#else
#define log_unlock(...) do {} while (0)
#endif

#ifdef LOG_LOCK_TOP
#define log_lock_top log_resource_ln
#else
#define log_lock_top(...) do {} while (0)
#endif

#ifdef LOG_LOCK_TIMEOUT
#define log_lock_timeout log_resource_ln
#else
#define log_lock_timeout(...) do {} while (0)
#endif

#endif
