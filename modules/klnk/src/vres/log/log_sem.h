#ifndef _LOG_SEM_H
#define _LOG_SEM_H

#include "log.h"

#ifdef LOG_SEM_SEMOP
#define log_sem_semop(key, nsops, sem_num, semval, src) \
        log_debug("key=%d, nsops=%d, sem_num=%d, semval=%d, src=%d", key, nsops, sem_num, semval, src);
#else
#define log_sem_semop(...) do {} while (0)
#endif

#endif
