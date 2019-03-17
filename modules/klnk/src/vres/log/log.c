/* log.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#define OP_MAX    64
#define OP_LEN    32
#define SHMOP_MAX 7

char op_unknown[OP_LEN] = "UNKNOWN";
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
    "MSGGET",
    "SEMGET",
    "SHMGET",
    "TSKPUT",
    "MSGPUT",
    "SEMPUT",
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

char shmop_list[SHMOP_MAX][OP_LEN] = {
    "UNUSED0",
    "PROPOSE",
    "CHK_OWNER",
    "CHK_HOLDER",
    "NOTIFY_OWNER",
    "NOTIFY_HOLDER",
    "NOTIFY_PROPOSER"};

char *log_get_op(int op)
{
    if (op < OP_MAX)
        return op_list[op];
    else
        return op_unknown;
}


char *log_get_shmop(int op)
{
    if (op < SHMOP_MAX)
        return shmop_list[op];
    else
        return op_unknown;
}
