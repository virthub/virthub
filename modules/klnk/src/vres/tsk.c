#include "tsk.h"

int vres_tsk_get(vres_t *resource, vres_id_t *id)
{
    int i;
    int ret = 0;
    vres_t res = *resource;
    const int retry_max = 1024;
    vres_id_t curr = resource->key;
    vres_t *tsk = &res;

    assert((vres_get_id(resource) == resource->owner) && (resource->key == resource->owner));
    vres_lock(resource);
    for (i = 0; i < retry_max; i++) {
        bool alloc = vres_create(tsk);
#ifdef TSK_REALLOC
        if (!alloc) {
            if (curr < VRES_ID_MAX)
                curr += 1;
            else
                curr = TSK_ID_START;
            tsk->key = curr;
            tsk->owner = curr;
            continue;
        }
#endif
        break;
    }
    ret = vres_mkdir(tsk);
    if (ret) {
        log_err("faild to create directory, ret=%s", log_get_err(ret));
        vres_remove(tsk);
    } else if (id) {
        vres_file_t *filp;
        char path[VRES_PATH_MAX];

        *id = curr;
        vres_get_state_path(tsk, path);
        filp = vres_file_open(path, "w");
        if (!filp)
            ret = -ENOENT;
        vres_file_close(filp);
    }
out:
    vres_unlock(resource, NULL);
    return ret;
}


void vres_tsk_get_resource(vres_id_t id, vres_t *resource)
{
    memset(resource, 0, sizeof(vres_t));
    resource->cls = VRES_CLS_TSK;
    resource->key = id;
    resource->owner = id;
}


void *vres_tsk_request(void *ptr)
{
    vres_tsk_req_t *req = (vres_tsk_req_t *)ptr;
    vres_t *resource = &req->resource;

    if (VRES_TSK_WAKEUP == req->cmd) {
        vres_tskctl_arg_t arg;
        vres_tskctl_result_t result;

        vres_set_op(resource, VRES_OP_TSKCTL);
        arg.cmd = VRES_TSK_WAKEUP;
        klnk_io_sync_by_addr(resource, (char *)&arg, sizeof(vres_tskctl_arg_t), (char *)&result, sizeof(vres_tskctl_result_t), req->addr);
    }
    free(ptr);
    return NULL;
}


void vres_tsk_clear(vres_t *resource)
{
    char path[VRES_PATH_MAX];

#ifdef TSK_RESERVE
    vres_get_state_path(resource, path);
    vres_file_remove(path);
#else
    vres_remove(resource);
    vres_get_root_path(resource, path);
    vres_file_rmdir(path);
#endif
}


int vres_tsk_put(vres_t *resource)
{
    vres_t res;
    int ret = 0;
    vres_t *tsk = &res;
    char path[VRES_PATH_MAX];
    vres_file_entry_t *entry;

    vres_tsk_get_resource(resource->owner, tsk);
    vres_lock(tsk);
    vres_get_mig_path(resource, path);
    entry = vres_file_get_entry(path, sizeof(vres_addr_t), FILE_RDONLY);
    if (entry) {
        vres_addr_t *addr;

        addr = vres_file_get_desc(entry, vres_addr_t);
        if (memcmp(&node_addr, addr, sizeof(vres_addr_t))) {
            pthread_t tid;
            vres_tsk_req_t *req;
            pthread_attr_t attr;

            req = (vres_tsk_req_t *)malloc(sizeof(vres_tsk_req_t));
            if (!req) {
                log_resource_err(resource, "no memory");
                vres_file_put_entry(entry);
                ret = -ENOMEM;
                goto out;
            }
            memcpy(&req->resource, tsk, sizeof(vres_t));
            memcpy(&req->addr, addr, sizeof(vres_addr_t));
            req->cmd = VRES_TSK_WAKEUP;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
            ret = pthread_create(&tid, &attr, vres_tsk_request, req);
            pthread_attr_destroy(&attr);
            if (ret) {
                log_resource_err(resource, "failed to request");
                free(req);
            }
        }
        vres_file_put_entry(entry);
    }
    vres_tsk_clear(tsk);
    if (!ret)
        log_tsk_put(resource);
out:
    vres_unlock(tsk, NULL);
    return ret;
}


int vres_tsk_suspend(vres_id_t id)
{
    int fd;
    int ret;
    vres_t res;
    ckpt_arg_t arg;
    vres_desc_t desc;
    vres_t *tsk = &res;

    vres_tsk_get_resource(id, tsk);
    ret = vres_lookup(tsk, &desc);
    if (ret) {
        log_err("failed to lookup");
        return ret;
    }
    memset(&arg, 0, sizeof(ckpt_arg_t));
    arg.id = desc.id;
    fd = open(CKPT_DEV_FILE, O_RDONLY);
    if (fd < 0) {
        log_err("failed to open file");
        return -EINVAL;
    }
    ret = ioctl(fd, CKPT_IOCTL_SUSPEND, (int_t)&arg);
    close(fd);
    if (ret) {
        log_err("failed to ioctl");
        return ret;
    }
    log_tsk_suspend(id);
    return 0;
}


int vres_tsk_resume(vres_id_t id)
{
    int fd;
    int ret;
    vres_t res;
    ckpt_arg_t arg;
    vres_desc_t desc;
    vres_t *tsk = &res;

    vres_tsk_get_resource(id, tsk);
    ret = vres_lookup(tsk, &desc);
    if (ret) {
        log_err("failed to lookup");
        return ret;
    }
    memset(&arg, 0, sizeof(ckpt_arg_t));
    arg.id = desc.id;
    fd = open(CKPT_DEV_FILE, O_RDONLY);
    if (fd < 0) {
        log_err("failed to open file");
        return -EINVAL;
    }
    ret = ioctl(fd, CKPT_IOCTL_RESUME, (int_t)&arg);
    close(fd);
    if (ret) {
        log_err("failed to ioctl");
        return ret;
    }
    log_tsk_resume(id);
    return 0;
}


int vres_tsk_wakeup(vres_t *resource)
{
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        log_resource_err(resource, "failed to create process");
        return -EINVAL;
    } else if (0 == pid) {
        char cmd[512];

        sprintf(cmd, "lxc-attach -q -n %s -s 'MOUNT|IPC|NETWORK|UTSNAME' -- %s -r %d",
                mds_name, PATH_DUMP, resource->owner);
        log_tsk_wakeup(cmd);
        system(cmd);
        _exit(0);
    }

    return 0;
}


vres_reply_t *vres_tsk_tskctl(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_reply_t *reply = NULL;
    vres_t *resource = &req->resource;
    vres_tskctl_arg_t *arg = (vres_tskctl_arg_t *)req->buf;

    if (VRES_TSK_WAKEUP == arg->cmd)
        ret = vres_tsk_wakeup(resource);
    else {
        log_resource_err(resource, "invalid cmd");
        ret = -EINVAL;
        goto out;
    }
    log_tsk_tskctl(resource, arg->cmd);
out:
    vres_create_reply(vres_tskctl_result_t, ret, reply);
    return reply;
}
