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
#define SHM_CMD_MAX 8
#define ERR_ENT_MAX 128
#define FLG_ENT_MAX 16
#define FLG_GRP_MAX 128

char unknown[32] = "UNKNOWN";
char errnum[ERR_ENT_MAX][32];
char op_str[OP_MAX][OP_LEN] = {
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

char shm_cmd_str[SHM_CMD_MAX][CMD_LEN] = {
    "UNUSED0",
    "PROPOSE",
    "CHK_OWNER",
    "CHK_HOLDER",
    "NOTIFY_OWNER",
    "NOTIFY_HOLDER",
    "NOTIFY_PROPOSER",
    "RETRY"};

char flg_str[FLG_MAX][32] = {
    "rd",       // 0x00000001
    "rw",       // 0x00000002
    "creat",    // 0x00000004
    "chown",    // 0x00000008
    "none",     // 0x00000010
    "cancel",   // 0x00000020
    "diff",     // 0x00000040
    "crit",     // 0x00000080
    "redo",     // 0x00000100
    "prio",     // 0x00000200
    "present",  // 0x00000400
    "dump",     // 0x00000800
    "chunk",    // 0x00001000
    "slice",    // 0x00002000
    "retry",    // 0x00004000
    "split",    // 0x00008000
    "active",   // 0x00010000
    "own",      // 0x00020000
    "spec",     // 0x00040000
    "inprog",   // 0x00080000
    "refl",     // 0x00100000
    "none",     // 0x00200000
    "none",     // 0x00400000
    "none",     // 0x00800000
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

char flg_buf[FLG_GRP_MAX][FLG_ENT_MAX][128];
unsigned long flg_grp[FLG_GRP_MAX] = {0};

char *log_get_op(int op)
{
    if (op < OP_MAX)
        return op_str[op];
    else
        return unknown;
}


char *log_get_shm_cmd(int cmd)
{
    if (cmd < SHM_CMD_MAX)
        return shm_cmd_str[cmd];
    else
        return unknown;
}


char *log_get_err(int err)
{
    int val = err;
    char *p = NULL;
    static int cnt = 0;

    if (val < 0)
        val = -val;
    switch (val) {
    case EOK:
        return eok;
    case EIO:
        return eio;
    case ERMID:
        return ermid;
    case EPERM:
        return eperm;
    case EEXIST:
        return eexist;
    case EINVAL:
        return einval;
    case ENOENT:
        return enoent;
    case EFAULT:
        return efault;
    case ENOMEM:
        return enomem;
    case EAGAIN:
        return eagain;
    case EACCES:
        return eacces;
    case ENOOWNER:
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
#ifdef FLAG2STR
    int n = 0;
    bool first = true;
#endif
    unsigned long grp = rand() % FLG_GRP_MAX;
    unsigned long pos = ++flg_grp[grp];
    char *p = flg_buf[grp][pos % FLG_ENT_MAX];
   
    *p = 0;
#ifdef FLAG2STR
    if (flags) {
        if (flags >= (1 << FLG_MAX) - 1)
            sprintf(p, "0x%x", flags);
        else {
            while (flags > 0) {
                if (flags & 1) {
                    if (first) {
                        first = false;
                        sprintf(p + strlen(p), "%s", flg_str[n]);
                    } else
                        sprintf(p + strlen(p), "|%s", flg_str[n]);
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
