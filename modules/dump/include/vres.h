#ifndef _VRES_H
#define _VRES_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

enum vres_class {
    VRES_CLS_MSG,
    VRES_CLS_SEM,
    VRES_CLS_SHM,
    VRES_CLS_TSK,
    VRES_NR_CLASSES,
};

enum vres_operation {
    VRES_OP_UNUSED0,
    VRES_OP_MSGSND,
    VRES_OP_MSGRCV,
    VRES_OP_MSGCTL,
    VRES_OP_SEMOP,
    VRES_OP_SEMCTL,
    VRES_OP_SEMEXIT,
    VRES_OP_SHMFAULT,
    VRES_OP_SHMCTL,
    VRES_OP_TSKCTL,
    VRES_OP_UNUSED1,
    VRES_OP_TSKGET,
    VRES_OP_MSGGET,
    VRES_OP_SEMGET,
    VRES_OP_SHMGET,
    VRES_OP_SHMPUT,
    VRES_OP_TSKPUT,
    VRES_OP_MIGRATE,
    VRES_OP_DUMP,
    VRES_OP_RESTORE,
    VRES_OP_RELEASE,
    VRES_OP_PROBE,
    VRES_OP_UNUSED2,
    VRES_OP_JOIN,
    VRES_OP_LEAVE,
    VRES_OP_SYNC,
    VRES_OP_REPLY,
    VRES_OP_CANCEL,
    VRES_OP_UNUSED3,
    VRES_NR_OPERATIONS,
};

static inline int vres_request(char *node,
                                unsigned long cls,
                                unsigned long key,
                                unsigned long op,
                                unsigned long id,
                                unsigned long flg)
{
    int fd;
    char path[256];

    if ('\0' != *node)
        sprintf(path, "%s/%s/%lx_%lx_%lx_%lx_%lx_%lx_%lx_%lx_%lx", PATH_KLNK, node, cls, key, op, id, (unsigned long)0, flg, (unsigned long)0, (unsigned long)0, (unsigned long)0);
    else
        sprintf(path, "%s/%lx_%lx_%lx_%lx_%lx_%lx_%lx_%lx_%lx", PATH_KLNK, cls, key, op, id, (unsigned long)0, flg, (unsigned long)0, (unsigned long)0, (unsigned long)0);

    fd = open(path, O_RDONLY);
    if (-1 == fd)
        return -1;

    close(fd);
    return 0;
}
#endif
