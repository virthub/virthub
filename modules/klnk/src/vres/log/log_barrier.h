#ifndef _LOG_BARRIER_H
#define _LOG_BARRIER_H

#ifdef LOG_BARRIER_SET
#define log_barrier_set log_resource_ln
#else
#define log_barrier_set(...) do {} while (0)
#endif

#ifdef LOG_BARRIER_CLEAR
#define log_barrier_clear log_resource_ln
#else
#define log_barrier_clear(...) do {} while (0)
#endif

#ifdef LOG_BARRIER_WAIT
#define log_barrier_wait log_resource_info
#else
#define log_barrier_wait(...) do {} while (0)
#endif

#ifdef LOG_BARRIER_WAIT_TIMEOUT
#define log_barrier_wait_timeout log_resource_info
#else
#define log_barrier_wait_timeout(...) do {} while (0)
#endif

#endif
