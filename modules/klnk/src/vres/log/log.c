/* log.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "log.h"
#include <vres.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define OP_MAX      64
#define FLG_MAX     24
#define OP_LEN      32
#define CMD_LEN     32
#define SHM_CMD_MAX 7
#define ERR_ENT_MAX 128
#define FLG_ENT_MAX 128

char unknown[32] = "UNKNOWN";
char errnum[ERR_ENT_MAX][32];
char flginf[FLG_ENT_MAX][128];
char op_list[OP_MAX][OP_LEN] = {
    "UNUSED0",
    "MSGSND",
    "MSGRCV",
    "MSGCTL",
    "SEMOP",
    "SEMCTL",
    "SEMEXIT",
    "SHMFAULT",
    "SHMCTL",
    "TSKCTL",
    "UNUSED1",
    "TSKGET",
    "TSKPUT",
    "MSGGET",
    "MSGPUT",
    "SEMGET",
    "SEMPUT",
    "SHMGET",
    "SHMPUT",
    "PGSAVE",
    "MIGRATE",
    "DUMP",
    "RESTORE",
    "RELEASE",
    "PROBE",
    "UNUSED2",
    "JOIN",
    "LEAVE",
    "SYNC",
    "REPLY",
    "CANCEL",
    "UNUSED3"};

char shm_cmd_list[SHM_CMD_MAX][CMD_LEN] = {
    "UNUSED0",
    "PROPOSE",
    "CHK_OWNER",
    "CHK_HOLDER",
    "NOTIFY_OWNER",
    "NOTIFY_HOLDER",
    "NOTIFY_PROPOSER"};

char flg_list[FLG_MAX][32] = {
    "rd",       // RDONLY         (0x00000001)
    "rw",       // RDWR           (0x00000002)
    "creat",    // CREATE         (0x00000004)
    "none",     // reserved       (0x00000008)
    "none",     // reserved       (0x00000010)
    "cancel",   // CANCEL         (0x00000020)
    "diff",     // DIFFERENTIATE  (0x00000040)
    "crit",     // CRITICAL       (0x00000080)
    "redo",     // REDO           (0x00000100)
    "prio",     // PRIORITIZE     (0x00000200)
    "present",  //                (0x00000400)
    "none",     //                (0x00000800)
    "none",     //                (0x00001000)
    "none",     //                (0x00002000)
    "none",     //                (0x00004000)
    "none",     //                (0x00008000)
    "ready",    // READY          (0x00010000)
    "active",   // ACTIVE         (0x00020000)
    "wait",     // WAIT           (0x00040000)
    "update",   // UPDATE         (0x00080000)
    "cmpl",     // COMPLETE       (0x00100000)
    "save",     // SAVE           (0x00200000)
    "cand",     // CANDIDATE      (0x00400000)
    "own",      // OWN            (0x00800000)
};

char eok[32]      = "EOK";
char eio[32]      = "EIO";
char ermid[32]    = "ERMID";
char eperm[32]    = "EPERM";
char eexist[32]   = "EEXIST";
char eagain[32]   = "EAGAIN";
char einval[32]   = "EINVAL";
char enoent[32]   = "ENOENT";
char efault[32]   = "EFAULT";
char enomem[32]   = "ENOMEM";
char eacces[32]   = "EACCES";
char enoowner[32] = "ENOOWNER";

char *log_get_op(int op)
{
    if (op < OP_MAX)
        return op_list[op];
    else
        return unknown;
}


char *log_get_shm_cmd(int cmd)
{
    if (cmd < SHM_CMD_MAX)
        return shm_cmd_list[cmd];
    else
        return unknown;
}


char *log_get_err(int err)
{
    int val = err;
    char *p = NULL;
    static int cnt = 0;

    switch (val) {
    case -EOK:
        return eok;
    case -EIO:
        return eio;
    case -ERMID:
        return ermid;
    case -EPERM:
        return eperm;
    case -EEXIST:
        return eexist;
    case -EINVAL:
        return einval;
    case -ENOENT:
        return enoent;
    case -EFAULT:
        return efault;
    case -ENOMEM:
        return enomem;
    case -EAGAIN:
        return eagain;
    case -EACCES:
        return eacces;
    case -ENOOWNER:
        return enoowner;
    default:
        p = errnum[cnt++];
        if (ERR_ENT_MAX == cnt)
            cnt = 0;
        sprintf(p, "%d", err);
        return p;
    }
}


char *log_get_flags(int flags)
{
    static int cnt = 0;
    char *p = flginf[cnt++];
#ifdef FLAG2STR
    int n = 0;
    bool first = true;

    *p = 0;
    if (FLG_ENT_MAX == cnt)
        cnt = 0;
    if (flags) {
        if (flags >= (1 << FLG_MAX) - 1)
            sprintf(p, "0x%x", flags);
        else {
            while (flags > 0) {
                if (flags & 1) {
                    if (first) {
                        first = false;
                        sprintf(p + strlen(p), "%s", flg_list[n]);
                    } else
                        sprintf(p + strlen(p), "|%s", flg_list[n]);
                }
                flags >>= 1;
                n++;
            }
        }
    } else
        sprintf(p, "0");
#else
    sprintf(p, "0x%x", flags);
#endif
    return p;
}
