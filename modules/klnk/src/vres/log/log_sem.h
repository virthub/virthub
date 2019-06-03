#ifndef _LOG_SEM_H
#define _LOG_SEM_H

#include "log.h"

#ifdef LOG_SEM_SEMOP
#define log_sem_semop(key, sem_num, semval, src) \
        log_debug("key=%d, sem_num=%d, semval=%d, src=%d", key, sem_num, semval, src);
#else
#define log_sem_semop(...) do {} while (0)
#endif

#ifdef LOG_SEM_SEMCTL
#define log_sem_semctl log_resource_info
#else
#define log_sem_semctl(...) do {} while(0)
#endif

#endif
