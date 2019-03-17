/* trace.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "trace.h"

void vres_trace(vres_req_t *req)
{
    static int count = 0;
    static int ops[VRES_SHM_NR_OPERATIONS] = {0};
    vres_op_t op = vres_get_op(&req->resource);

    if (VRES_OP_SHMFAULT == op) {
        vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;

        if (arg->cmd >= VRES_SHM_NR_OPERATIONS)
            return;

        ops[arg->cmd]++;
    }

    count++;
    if (VRES_TRACE_INTERVAL == count) {
        int i;

        count = 0;
        log("%s:", log_get_op(VRES_OP_SHMFAULT));
        for (i = 0; i < VRES_SHM_NR_OPERATIONS - 1; i++)
            log("%s=%d, ", log_get_shmop(i), ops[i]);
        log("%s=%d\n", log_get_shmop(i), ops[i]);
    }
}
