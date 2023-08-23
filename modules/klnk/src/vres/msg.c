#include "msg.h"

int vres_msg_create(vres_t *resource)
{
    int ret = 0;
    vres_file_t *filp;
    struct msqid_ds msq;
    char path[VRES_PATH_MAX];

    memset(&msq, 0, sizeof(struct msqid_ds));
    msq.msg_perm.mode = vres_get_flags(resource) & S_IRWXUGO;
    msq.msg_perm.__key = resource->key;
    msq.msg_ctime = time(0);
    msq.msg_qbytes = VRES_MSGMNB;
    vres_get_state_path(resource, path);
    filp = vres_file_open(path, "w");
    if (!filp)
        return -ENOENT;
    if (vres_file_write(&msq, sizeof(struct msqid_ds), 1, filp) != 1)
        ret = -EIO;
    vres_file_close(filp);
    return ret;
}


int vres_msg_check(vres_t *resource, struct msqid_ds *msq)
{
    int ret = 0;
    vres_file_t *filp;
    char path[VRES_PATH_MAX];

    vres_get_state_path(resource, path);
    filp = vres_file_open(path, "r");
    if (!filp)
        return -ENOENT;
    if (vres_file_read(msq, sizeof(struct msqid_ds), 1, filp) != 1)
        ret = -EIO;
    vres_file_close(filp);
    return ret;
}


int vres_msg_update(vres_t *resource, struct msqid_ds *msq)
{
    int ret = 0;
    vres_file_t *filp;
    char path[VRES_PATH_MAX];

    vres_get_state_path(resource, path);
    filp = vres_file_open(path, "w");
    if (!filp)
        return -ENOENT;
    if (vres_file_write(msq, sizeof(struct msqid_ds), 1, filp) != 1)
        ret = -EIO;
    vres_file_close(filp);
    return ret;
}


vres_reply_t *vres_msg_msgsnd(vres_req_t *req, int flags)
{
    int mode;
    int msgflg;
    long msgtyp;
    long ret = 0;
    size_t msgsz;
    vres_index_t start;
    struct msqid_ds msq;
    vres_record_t record;
    vres_index_t index = -1;
    char path[VRES_PATH_MAX];
    vres_reply_t *reply = NULL;
    vres_t res = req->resource;
    vres_t *resource = &req->resource;
    vres_msgrcv_arg_t *msgrcv_arg = NULL;
    vres_msgsnd_arg_t *msgsnd_arg = NULL;

    if (flags & VRES_CANCEL) {
        ret = -EINVAL;
        goto out;
    }
    msgsnd_arg = (vres_msgsnd_arg_t *)req->buf;
    if (req->length < msgsnd_arg->msgsz + sizeof(vres_msgsnd_arg_t) + sizeof(long)) {
        ret = -EINVAL;
        goto out;
    }
    msgsz = msgsnd_arg->msgsz;
    msgtyp = msgsnd_arg->msgtyp;
    msgflg = msgsnd_arg->msgflg;

    // check message queue
    ret = vres_msg_check(resource, &msq);
    if (ret) {
        ret = -EFAULT;
        goto out;
    }
    if ((msq.msg_cbytes + msgsnd_arg->msgsz > msq.msg_qbytes) || (1 + msq.msg_qnum > msq.msg_qbytes)) {
        if (msgflg & IPC_NOWAIT)
            ret = -EAGAIN;
        else {
            vres_get_record_path(resource, path);
            ret = vres_record_save(path, req, &index);
            if (!ret)
                ret = vres_index_to_err(index);
        }
        goto out;
    }
    vres_set_op(&res, VRES_OP_MSGRCV);
    vres_get_record_path(&res, path);
    ret = vres_record_head(path, &start);
    if (!ret) {
        // check the waiting list
        for (;;) {
            ret = vres_record_get(path, start, &record);
            if (ret)
                goto out;
            msgrcv_arg = (vres_msgrcv_arg_t *)record.req->buf;
            mode = vres_msg_convert_mode(&msgrcv_arg->msgtyp, msgrcv_arg->msgflg);
            if (vres_msgtyp_cmp(msgtyp, msgrcv_arg->msgtyp, mode)) {
                index = start;
                break;
            }
            vres_record_put(&record);
            ret = vres_record_next(path, &start);
            if (ret)
                break;
        }
    }
    if (ret && ret != -ENOENT)
        goto out;
    if (index >= 0) {
        vres_id_t src;

        res = record.req->resource;
        src = vres_get_id(&res);
        vres_set_off(&res, index);
        vres_set_op(&res, VRES_OP_REPLY);

        if ((msgrcv_arg->msgsz >= msgsz) || (msgrcv_arg->msgflg & MSG_NOERROR)) {
            size_t size = msgrcv_arg->msgsz > msgsz ? msgsz : msgrcv_arg->msgsz;
            size_t outlen = size + sizeof(long) + sizeof(vres_msgrcv_result_t);
            char *output = malloc(outlen);

            if (!output) {
                ret = -ENOMEM;
                goto release;
            }
            msq.msg_stime = time(0);
            msq.msg_rtime = msq.msg_stime;
            msq.msg_lspid = vres_get_id(resource);
            msq.msg_lrpid = src;
            ret = vres_msg_update(resource, &msq);
release:
            vres_record_put(&record);
            vres_record_remove(path, index);
            if (!ret) {
                vres_msgrcv_result_t *msgrcv_result = (vres_msgrcv_result_t *)output;
                msgrcv_result->retval = size;
                memcpy(&msgrcv_result[1], (struct msgbuf *)&msgsnd_arg[1], size + sizeof(long));
                klnk_io_sync(&res, output, outlen, NULL, 0, src);
            } else {
                vres_msgrcv_result_t msgrcv_result;
                msgrcv_result.retval = ret;
                klnk_io_sync(&res, (char *)&msgrcv_result, sizeof(msgrcv_result), NULL, 0, src);
            }
            if (output)
                free(output);
        } else {
            vres_msgrcv_result_t msgrcv_result;
            vres_record_put(&record);
            msgrcv_result.retval = -E2BIG;
            klnk_io_sync(&res, (char *)&msgrcv_result, sizeof(msgrcv_result), NULL, 0, src);
        }
    } else { // no receiver
        msq.msg_lspid = vres_get_id(resource);
        msq.msg_stime = time(0);
        msq.msg_cbytes += msgsz;
        msq.msg_qnum++;

        vres_get_record_path(resource, path);
        ret = vres_record_save(path, req, &index);
        if (!ret) {
            ret = vres_msg_update(resource, &msq);
            if (ret) {
                vres_record_remove(path, index);
                ret = -EFAULT;
                goto out;
            }
        }
    }
out:
    vres_create_reply(vres_msgsnd_result_t, ret, reply);
    log_msg(resource);
    return reply;
}


vres_reply_t *vres_msg_msgrcv(vres_req_t *req, int flags)
{
    int mode;
    int msgflg;
    long msgtyp;
    long ret = 0;
    size_t msgsz;
    vres_index_t start;
    vres_record_t record;
    vres_index_t index = -1;
    char path[VRES_PATH_MAX];
    vres_reply_t *reply = NULL;
    vres_t dest = req->resource;
    vres_t *resource = &req->resource;
    vres_msgrcv_arg_t *msgrcv_arg = NULL;
    vres_msgsnd_arg_t *msgsnd_arg = NULL;

    if (flags & VRES_CANCEL) {
        ret = -EINVAL;
        goto out;
    }
    if (req->length < sizeof(vres_msgrcv_arg_t)) {
        ret = -EINVAL;
        goto out;
    }
    msgrcv_arg = (vres_msgrcv_arg_t *)req->buf;
    msgflg = msgrcv_arg->msgflg;
    msgsz = msgrcv_arg->msgsz;
    msgtyp = msgrcv_arg->msgtyp;
    mode = vres_msg_convert_mode(&msgtyp, msgflg);
    vres_set_op(&dest, VRES_OP_MSGSND);
    vres_get_record_path(&dest, path);
    ret = vres_record_head(path, &start);
    if (!ret) {
        // check the waiting senders
        for (;;) {
            ret = vres_record_get(path, start, &record);
            if (ret)
                goto out;
            msgsnd_arg = (vres_msgsnd_arg_t *)record.req->buf;
            if (vres_msgtyp_cmp(msgsnd_arg->msgtyp, msgtyp, mode)) {
                index = start;
                if ((SEARCH_LESSEQUAL == mode) && (msgsnd_arg->msgtyp != 1))
                    msgtyp = msgsnd_arg->msgtyp - 1;
                else
                    break;
            }
            vres_record_put(&record);
            ret = vres_record_next(path, &start);
            if (ret)
                break;
        }
    }
    if (-ENOENT == ret) {
        if (index >= 0) {
            ret = vres_record_get(path, index, &record);
            if (ret)
                goto out;
        }
    } else if (ret)
        goto out;
    if (index >= 0) {
        msgsnd_arg = (vres_msgsnd_arg_t *)record.req->buf;
        if ((msgsnd_arg->msgsz <= msgsz) || (msgflg & MSG_NOERROR)) {
            int outlen;
            int bytes_read;
            struct msqid_ds msq;
            vres_msgrcv_result_t *result;

            ret = vres_msg_check(resource, &msq);
            if (ret) {
                vres_record_put(&record);
                goto out;
            }
            if ((record.req->length > VRES_BUF_MAX) || (record.req->length < 0)) {
                ret = -EINVAL;
                goto release;
            }
            bytes_read = sizeof(long) + (msgsnd_arg->msgsz > msgsz ? msgsz : msgsnd_arg->msgsz);
            outlen = sizeof(vres_msgrcv_result_t) + bytes_read;
            reply = vres_reply_alloc(outlen);
            if (!reply) {
                ret = -ENOMEM;
                goto release;
            }
            result = vres_result_check(reply, vres_msgrcv_result_t);
            result->retval = bytes_read - sizeof(long);
            memcpy(&result[1], &record.req->buf[sizeof(vres_msgsnd_arg_t)], bytes_read);
            msq.msg_qnum--;
            msq.msg_rtime = time(0);
            msq.msg_lrpid = vres_get_id(resource);
            msq.msg_cbytes -= msgsnd_arg->msgsz;
            ret = vres_msg_update(resource, &msq);
release:
            vres_record_put(&record);
            vres_record_remove(path, index);
            if (ret && reply) {
                free(reply);
                reply = NULL;
            }
        } else {
            ret = -E2BIG;
            vres_record_put(&record);
        }
    } else if (msgflg & IPC_NOWAIT)
        ret = -ENOMSG;
    else {
        vres_get_record_path(resource, path);
        ret = vres_record_save(path, req, &index);
        if (!ret)
            ret = vres_index_to_err(index);
    }
out:
    if (!reply)
        vres_create_reply(vres_msgrcv_result_t, ret, reply);
    log_msg(resource);
    return reply;
}


vres_reply_t *vres_msg_msgctl(vres_req_t *req, int flags)
{
    long ret = 0;
    vres_reply_t *reply;
    struct msqid_ds msq;
    vres_msgctl_result_t *result;
    vres_t *resource = &req->resource;
    vres_msgctl_arg_t *arg = (vres_msgctl_arg_t *)req->buf;
    int outlen = sizeof(vres_msgctl_result_t);
    int cmd = arg->cmd;

    if ((flags & VRES_CANCEL) || (req->length < sizeof(vres_msgctl_arg_t)))
        return vres_reply_err(-EINVAL);
    switch (cmd) {
    case IPC_INFO:
    case MSG_INFO:
        outlen += sizeof(struct msginfo);
        break;
    case IPC_STAT:
    case MSG_STAT:
        outlen += sizeof(struct msqid_ds);
    case IPC_SET:
        ret = vres_msg_check(resource, &msq);
        break;
    }
    if (!ret) {
        reply = vres_reply_alloc(outlen);
        if (!reply)
            ret = -ENOMEM;
    }
    if (ret)
        return vres_reply_err(ret);
    result = vres_result_check(reply, vres_msgctl_result_t);
    switch (cmd) {
    case IPC_INFO:
    case MSG_INFO:
    {
        struct msginfo *msginfo = (struct msginfo *)&result[1];

        memset(msginfo, 0, sizeof(struct msginfo));
        msginfo->msgmni = VRES_MSGMNI;
        msginfo->msgmax = VRES_MSGMAX;
        msginfo->msgmnb = VRES_MSGMNB;
        msginfo->msgssz = VRES_MSGSSZ;
        msginfo->msgseg = VRES_MSGSEG;
        msginfo->msgmap = VRES_MSGMAP;
        msginfo->msgpool = VRES_MSGPOOL;
        msginfo->msgtql = VRES_MSGTQL;
        if (MSG_INFO == cmd) {
            msginfo->msgpool = vres_get_resource_count(VRES_CLS_MSG);
            //TODO: set 1) msginfo->msgmap and 2) msginfo->msgtql
        }
        ret = vres_get_max_key(VRES_CLS_MSG);
        break;
    }
    case MSG_STAT:
        ret = resource->key;
    case IPC_STAT:
        memcpy((struct msqid_ds *)&result[1], &msq, sizeof(struct msqid_ds));
        break;
    case IPC_SET:
    {
        struct msqid_ds *buf = (struct msqid_ds *)&arg[1];

        memcpy(&msq.msg_perm, &buf->msg_perm, sizeof(sizeof(struct ipc_perm)));
        msq.msg_qbytes = buf->msg_qbytes;
        msq.msg_ctime = time(0);
        ret = vres_msg_update(resource, &msq);
        break;
    }
    case IPC_RMID:
        vres_redo_all(resource, VRES_CANCEL);
        vres_remove(resource);
        break;
    default:
        ret = -EINVAL;
        break;
    }
    result->retval = ret;
    return reply;
}
