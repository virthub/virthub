#include "trace.h"

void vres_trace(vres_req_t *req)
{
    static int count = 0;
    static int cmd[VRES_SHM_NR_COMMANDS] = {0};
    vres_op_t op = vres_get_op(&req->resource);

    if (VRES_OP_SHMFAULT == op) {
        vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;

        if (shm_req->cmd >= VRES_SHM_NR_COMMANDS)
            return;
        cmd[shm_req->cmd]++;
    }
    count++;
    if (VRES_TRACE_INTERVAL == count) {
        int i;

        count = 0;
        log("%s:", log_get_op(VRES_OP_SHMFAULT));
        for (i = 0; i < VRES_SHM_NR_COMMANDS - 1; i++)
            log("%s=%d, ", log_get_shm_cmd(i), cmd[i]);
        log("%s=%d\n", log_get_shm_cmd(i), cmd[i]);
    }
}
