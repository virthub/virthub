#ifndef _SEM_H
#define _SEM_H

#include <sys/ipc.h>
#include <sys/sem.h>
#include "resource.h"
#include "record.h"
#include "trace.h"
#include "util.h"
#include "redo.h"
#include "file.h"

#define SEM_NSEMS   20

#define VRES_SEMVMX 32767                       /* <= 32767 semaphore maximum value */
#define VRES_SEMMNI 128                         /* <= IPCMNI  max # of semaphore identifiers */
#define VRES_SEMMSL 250                         /* <= 8 000 max num of semaphores per id */
#define VRES_SEMMNS (VRES_SEMMNI * VRES_SEMMSL) /* <= INT_MAX max # of semaphores in system */
#define VRES_SEMOPM 32                          /* <= 1 000 max num of ops per semop call */
#define VRES_SEMMNU VRES_SEMMNS                 /* num of undo structures system wide */
#define VRES_SEMMAP VRES_SEMMNS                 /* # of entries in semaphore map */
#define VRES_SEMUME VRES_SEMOPM                 /* max num of undo entries per process */
#define VRES_SEMUSZ 20                          /* sizeof struct sem_undo */
#define VRES_SEMAEM VRES_SEMVMX                 /* adjust on exit max value */

#ifdef SHOW_SEM
#define LOG_SEM_SEMOP
#define LOG_SEM_SEMCTL
#endif

#include "log_sem.h"

#define vres_semundo_size(un) (sizeof(vres_semundo_t) + (un)->nsems * sizeof(short))
#define vres_sma_size(nsems) (sizeof(struct vres_sem_array ) + nsems * sizeof(struct vres_sem))

typedef short vres_semadj_t;

union semun {
    int              val;          /* Value for SETVAL */
    struct semid_ds *buf;          /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;        /* Array for GETALL, SETALL */
    struct seminfo  *__buf;        /* Buffer for IPC_INFO (Linux specific) */
};

typedef struct vres_semop_arg {
    struct timespec timeout;
    short nsops;
    struct sembuf sops[0];
} vres_semop_arg_t;

typedef struct vres_semop_result {
    long retval;
} vres_semop_result_t;

typedef struct vres_semctl_arg {
    int semnum;
    int cmd;
} vres_semctl_arg_t;

typedef struct vres_semctl_result {
    long retval;
} vres_semctl_result_t;

typedef struct vres_semundo {
    pid_t pid;
    unsigned long nsems;
    vres_semadj_t semadj[0];
} vres_semundo_t;

struct vres_sem {
    int semval;    /* current value */
    pid_t sempid;  /* pid of last operation */
};

struct vres_sem_array {
  struct ipc_perm sem_perm;        /* Ownership and permissions */
  time_t sem_otime;                /* Last semop time */
  time_t sem_ctime;                /* Last change time */
  unsigned short sem_nsems;        /* No. of semaphores in set */
  struct vres_sem    sem_base[0];  /* ptr to first semaphore in array */
};

void vres_sem_exit(vres_req_t *req);
int vres_sem_create(vres_t *resource);
int vres_sem_get_arg(vres_t *resource, vres_arg_t *arg);
vres_reply_t *vres_sem_semop(vres_req_t *req, int flags);
vres_reply_t *vres_sem_semctl(vres_req_t *req, int flags);

#endif
