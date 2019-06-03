#ifndef _LOG_RWLOCK_H
#define _LOG_RWLOCK_H

#ifdef LOG_RWLOCK_GET
#define log_rwlock_get log_resource_info
#else
#define log_rwlock_get(...) do {} while (0)
#endif

#ifdef LOG_RWLOCK_RDLOCK
#define log_rwlock_rdlock log_resource_info
#else
#define log_rwlock_rdlock(...) do {} while (0)
#endif

#ifdef LOG_RWLOCK_WRLOCK
#define log_rwlock_wrlock log_resource_info
#else
#define log_rwlock_wrlock(...) do {} while (0)
#endif

#ifdef LOG_RWLOCK_UNLOCK
#define log_rwlock_unlock log_resource_info
#else
#define log_rwlock_unlock(...) do {} while (0)
#endif

#endif
