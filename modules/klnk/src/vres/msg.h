#ifndef _MSG_H
#define _MSG_H

#include <sys/ipc.h>
#include <sys/msg.h>
#include "resource.h"
#include "record.h"
#include "trace.h"
#include "util.h"
#include "redo.h"
#include "file.h"

#define VRES_MSGMAX      8192                                     /* <= INT_MAX */   /* max size of message (bytes) */
#define VRES_MSGMNI      16                                       /* <= IPCMNI */    /* max # of msg queue identifiers */
#define VRES_MSGMNB      16384                                    /* <= INT_MAX */   /* default max size of a message queue */
#define VRES_MSGTQL      VRES_MSGMNB                              /* number of system message headers */
#define VRES_MSGMAP      VRES_MSGMNB                              /* number of entries in message map */
#define VRES_MSGPOOL     (VRES_MSGMNI * VRES_MSGMNB / 1024)       /* size in kbytes of message pool */
#define VRES_MSGSSZ      16                                       /* message segment size */
#define __MSGSEG         ((VRES_MSGPOOL * 1024) / VRES_MSGSSZ)    /* max no. of segments */
#define VRES_MSGSEG      (__MSGSEG <= 0xffff ? __MSGSEG : 0xffff)

#define SEARCH_ANY       1
#define SEARCH_EQUAL     2
#define SEARCH_NOTEQUAL  3
#define SEARCH_LESSEQUAL 4

#ifdef SHOW_MSG
#define LOG_MSG
#endif

#include "log_msg.h"

typedef struct vres_msgsnd_arg {
    long msgtyp;
    size_t msgsz;
    int msgflg;
} vres_msgsnd_arg_t;

typedef struct vres_msgsnd_result {
    long retval;
} vres_msgsnd_result_t;

typedef struct vres_msgrcv_result {
    long retval;
} vres_msgrcv_result_t;

typedef struct vres_msgrcv_arg {
    long msgtyp;
    size_t msgsz;
    int msgflg;
} vres_msgrcv_arg_t;

typedef struct vres_msgctl_arg {
    int cmd;
} vres_msgctl_arg_t;

typedef struct vres_msgctl_result {
    long retval;
} vres_msgctl_result_t;

static inline int vres_msg_convert_mode(long *msgtyp, int msgflg)
{
    /*
     *  find message of correct type.
     *  msgtyp = 0 => get first.
     *  msgtyp > 0 => get first message of matching type.
     *  msgtyp < 0 => get message with least type must be < abs(msgtype).
     */
    if (*msgtyp == 0)
        return SEARCH_ANY;
    if (*msgtyp < 0) {
        *msgtyp = -*msgtyp;
        return SEARCH_LESSEQUAL;
    }
    if (msgflg & MSG_EXCEPT)
        return SEARCH_NOTEQUAL;
    return SEARCH_EQUAL;
}

static inline int vres_msgtyp_cmp(long typ1, long typ2, int mode)
{
    switch (mode) {
    case SEARCH_ANY:
        return 1;
    case SEARCH_LESSEQUAL:
        if (typ1 <= typ2)
            return 1;
        break;
    case SEARCH_EQUAL:
        if (typ1 == typ2)
            return 1;
        break;
    case SEARCH_NOTEQUAL:
        if (typ1 != typ2)
            return 1;
        break;
    }
    return 0;
}

int vres_msg_create(vres_t *resource);
vres_reply_t *vres_msg_msgsnd(vres_req_t *req, int flags);
vres_reply_t *vres_msg_msgrcv(vres_req_t *req, int flags);
vres_reply_t *vres_msg_msgctl(vres_req_t *req, int flags);

#endif
