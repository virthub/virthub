#include "proc.h"

vres_reply_t *vres_proc_msg(vres_req_t *req, int flags)
{
    vres_t *resource = &req->resource;
    vres_op_t op = vres_get_op(resource);

    switch(op) {
    case VRES_OP_MSGSND:
        return vres_msg_msgsnd(req, flags);
    case VRES_OP_MSGRCV:
        return vres_msg_msgrcv(req, flags);
    case VRES_OP_MSGCTL:
        return vres_msg_msgctl(req, flags);
    default:
        log_resource_err(resource, "invalid operation, op=%d", op);
        break;
    }
    return NULL;
}


vres_reply_t *vres_proc_sem(vres_req_t *req, int flags)
{
    vres_t *resource = &req->resource;
    vres_op_t op = vres_get_op(resource);

    switch(op) {
    case VRES_OP_SEMOP:
        return vres_sem_semop(req, flags);
    case VRES_OP_SEMCTL:
        return vres_sem_semctl(req, flags);
    case VRES_OP_SEMEXIT:
        vres_sem_exit(req);
        break;
    default:
        log_resource_err(resource, "invalid operation, op=%d", op);
        break;
    }
    return NULL;
}


vres_reply_t *vres_proc_shm(vres_req_t *req, int flags)
{
    vres_t *resource = &req->resource;
    vres_op_t op = vres_get_op(resource);

    switch(op) {
    case VRES_OP_SHMFAULT:
        return vres_shm_fault(req, flags);
    case VRES_OP_SHMCTL:
        return vres_shm_shmctl(req, flags);
    default:
        log_resource_err(resource, "invalid operation, op=%d", op);
        break;
    }
    return NULL;
}


vres_reply_t *vres_proc_tsk(vres_req_t *req, int flags)
{
    vres_t *resource = &req->resource;
    vres_op_t op = vres_get_op(resource);

    switch(op) {
    case VRES_OP_TSKCTL:
        return vres_tsk_tskctl(req, flags);
    default:
        log_resource_err(resource, "invalid operation, op=%d", op);
        break;
    }
    return NULL;
}


vres_reply_t *vres_proc_sync(vres_req_t *req, int flags)
{
    vres_t *resource = &req->resource;
    vres_op_t op = vres_get_op(resource);

    switch (op) {
    case VRES_OP_JOIN:
        return vres_join(req, flags);
    case VRES_OP_LEAVE:
        return vres_leave(req, flags);
    case VRES_OP_SYNC:
        return vres_sync(req, flags);
    case VRES_OP_CANCEL:
        return vres_cancel(req, flags);
    case VRES_OP_REPLY:
        return vres_reply(req, flags);
    case VRES_OP_DUMP:
    case VRES_OP_RESTORE:
        return vres_ckpt(req, flags);
    default:
        log_resource_err(resource, "invalid operation, op=%d", op);
        return NULL;
    }
}


vres_reply_t *vres_proc(vres_req_t *req, int flags)
{
    vres_reply_t *reply = NULL;
    vres_t *resource = &req->resource;
    vres_op_t op = vres_get_op(resource);

    log_proc(req, NULL, true);
    trace_req(req);
    if (vres_is_sync(op))
        reply = vres_proc_sync(req, flags);
    else {
        switch(resource->cls) {
        case VRES_CLS_MSG:
            reply = vres_proc_msg(req, flags);
            break;
        case VRES_CLS_SEM:
            reply = vres_proc_sem(req, flags);
            break;
        case VRES_CLS_SHM:
            reply = vres_proc_shm(req, flags);
            break;
        case VRES_CLS_TSK:
            reply = vres_proc_tsk(req, flags);
            break;
        default:
            log_resource_err(resource, "invalid class");
            break;
        }
    }
    log_proc(req, reply, false);
    return reply;
}


int vres_proc_local(vres_arg_t *arg)
{
    vres_t *resource = &arg->resource;
    vres_op_t op = vres_get_op(resource);

    switch (op) {
    case VRES_OP_MSGGET:
        return vres_ipc_get(resource, vres_msg_create, NULL);
    case VRES_OP_SEMGET:
        return vres_ipc_get(resource, vres_sem_create, NULL);
    case VRES_OP_SHMGET:
        return vres_ipc_get(resource, vres_shm_create, vres_shm_ipc_init);
    case VRES_OP_SHMPUT:
        return vres_ipc_put(resource);
    case VRES_OP_TSKGET:
        return vres_tsk_get(resource, (int *)arg->out);
    case VRES_OP_TSKPUT:
        return vres_tsk_put(resource);
    case VRES_OP_MIGRATE:
        return vres_migrate(resource, arg);
    case VRES_OP_DUMP:
        return vres_dump(resource, CKPT_LOCAL);
    case VRES_OP_RESTORE:
        return vres_restore(resource, CKPT_LOCAL);
    case VRES_OP_PGSAVE:
        return vres_shm_save_page(resource, arg->in, arg->inlen);
    case VRES_OP_RELEASE:
        break;
    case VRES_OP_PROBE:
        vres_probe(resource);
        break;
    default:
        log_resource_err(resource, "operation %d is not supported", op);
        return -EINVAL;
    }
    return 0;
}
