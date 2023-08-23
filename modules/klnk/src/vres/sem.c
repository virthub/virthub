#include "sem.h"

vres_semundo_t *vres_sem_alloc_undo(unsigned long nsems, pid_t pid)
{
    size_t size = sizeof(vres_semundo_t) + nsems * sizeof(short);
    vres_semundo_t *un = (vres_semundo_t *)calloc(1, size);

    if (!un) {
        log_err("failed");
        return NULL;
    }
    un->nsems = nsems;
    un->pid = pid;
    return un;
}


int vres_sem_create(vres_t *resource)
{
    int ret = 0;
    vres_file_t *filp;
    char path[VRES_PATH_MAX];
    struct vres_sem_array *sma;
    int nsems = vres_entry_val1(resource->entry);
    size_t size = vres_sma_size(nsems);

    sma = (struct vres_sem_array *)calloc(1, size);
    if (!sma) {
        log_err("no memory");
        return -ENOMEM;
    }
    sma->sem_perm.mode = vres_get_flags(resource) & S_IRWXUGO;
    sma->sem_perm.__key = resource->key;
    sma->sem_nsems = nsems;
    sma->sem_ctime = time(0);
    vres_get_state_path(resource, path);
    filp = vres_file_open(path, "w");
    if (!filp) {
        log_err("no entry");
        free(sma);
        return -ENOENT;
    }
    if (vres_file_write((char *)sma, 1, size, filp) != size) {
        log_err("failed to write");
        ret = -EIO;
    }
    vres_file_close(filp);
    free(sma);
     return ret;
}


int vres_sem_get_arg(vres_t *resource, vres_arg_t *arg)
{
    if (VRES_OP_SEMOP == vres_get_op(resource)) {
        char empty[sizeof(struct timespec)] = {0};
        vres_semop_arg_t *semop_arg = (vres_semop_arg_t *)arg->in;
        struct timespec *timeout = &semop_arg->timeout;

        if (arg->inlen < sizeof(vres_semop_arg_t))
            return -EINVAL;
        if (memcmp(timeout, empty, sizeof(struct timespec)))
            arg->timeout = timeout;
    }
    return 0;
}


struct vres_sem_array *vres_sem_check(vres_t *resource)
{
    char *buf;
    size_t size;
    vres_file_t *filp;
    char path[VRES_PATH_MAX];
    struct vres_sem_array *sma = NULL;

    vres_get_state_path(resource, path);
    filp = vres_file_open(path, "r");
    if (!filp)
        return NULL;
    size = vres_file_size(filp);
    if (size < sizeof(struct vres_sem_array))
        goto out;
    buf = malloc(size);
    if (!buf)
        goto out;
    if (vres_file_read(buf, 1, size, filp) != size) {
        free(buf);
        goto out;
    }
    if (size != vres_sma_size(((struct vres_sem_array *)buf)->sem_nsems)) {
        free(buf);
        goto out;
    }
    sma = (struct vres_sem_array *)buf;
out:
    vres_file_close(filp);
    return sma;
}


int vres_sem_update(vres_t *resource, struct vres_sem_array  *sma)
{
    int ret = 0;
    size_t size;
    vres_file_t *filp;
    char path[VRES_PATH_MAX];

    vres_get_state_path(resource, path);
    filp = vres_file_open(path, "w");
    if (!filp) {
        log_err("no entry");
        return -ENOENT;
    }
    size = vres_sma_size(sma->sem_nsems);
    if (vres_file_write((char *)sma, 1, size, filp) != size) {
        log_err("failed to write");
        ret = -EIO;
    }
    vres_file_close(filp);
    return ret;
}


vres_semundo_t *vres_sem_undo_at(vres_semundo_t *undos, size_t pos)
{
    size_t size = vres_semundo_size(undos);

    return (vres_semundo_t *)((char *)undos + pos * size);
}


vres_semundo_t *vres_sem_check_undo(vres_t *resource)
{
    int i;
    int total;
    size_t undo_size;
    vres_file_entry_t *entry;
    vres_semundo_t *undos;
    char path[VRES_PATH_MAX];
    vres_semundo_t *un = NULL;
    pid_t pid = vres_get_id(resource);

    vres_get_action_path(resource, path);
    entry = vres_file_get_entry(path, 0, FILE_RDONLY);
    if (!entry)
        return NULL;
    undos = vres_file_get_desc(entry, vres_semundo_t);
    undo_size = vres_semundo_size(undos);
    total = vres_file_items_count(entry, undo_size);
    for (i = 0; i < total; i++) {
        vres_semundo_t *undo = vres_sem_undo_at(undos, i);

        if (undo->pid == pid) {
            un = (vres_semundo_t *)malloc(undo_size);
            if (un)
                memcpy(un, undo, undo_size);
            break;
        }
    }
    vres_file_put_entry(entry);
    return un;
}


void vres_sem_free_undo(vres_semundo_t *un)
{
    un->pid = -1;
}


bool vres_sem_is_empy_undo(vres_semundo_t *un)
{
    return un->pid < 0;
}


int vres_sem_update_undo(vres_t *resource, vres_semundo_t *un)
{
    int ret = 0;
    int updated = 0;
    size_t undo_size;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!un) {
        log_err("invalid undo");
        return -EINVAL;
    }
    undo_size = vres_semundo_size(un);
    vres_get_action_path(resource, path);
    entry = vres_file_get_entry(path, 0, FILE_RDWR);
    if (entry) {
        int i;
        int total;
        vres_semundo_t *undos = vres_file_get_desc(entry, vres_semundo_t);

        total = vres_file_items_count(entry, undo_size);
        for (i = 0; i < total; i++) {
            vres_semundo_t *undo = vres_sem_undo_at(undos, i);

            if (vres_sem_is_empy_undo(undo)) {
                memcpy(undo, un, undo_size);
                updated = 1;
                break;
            }
        }
    }
    if (!updated) {
        if (!entry) {
            entry = vres_file_get_entry(path, 0, FILE_RDWR | FILE_CREAT);
            if (!entry)
                return -ENOENT;
        }
        ret = vres_file_append(entry, (char *)un, undo_size);
    }
    vres_file_put_entry(entry);
    return ret;
}


void vres_sem_reset_undos(vres_t *resource, int semnum)
{
    int i;
    int total;
    size_t undo_size;
    vres_file_entry_t *entry;
    vres_semundo_t *undo;
    vres_semundo_t *undos;
    char path[VRES_PATH_MAX];

    vres_get_action_path(resource, path);
    entry = vres_file_get_entry(path, 0, FILE_RDWR);
    if (!entry)
        return;
    undos = vres_file_get_desc(entry, vres_semundo_t);
    undo_size = vres_semundo_size(undos);
    total = vres_file_items_count(entry, undo_size);
    for (i = 0; i < total; i++) {
        undo = vres_sem_undo_at(undos, i);
        undo->semadj[semnum] = 0;
    }
    vres_file_put_entry(entry);
}


void vres_sem_remove_undo(vres_t *resource)
{
    int i;
    int total;
    size_t undo_size;
    vres_file_entry_t *entry;
    vres_semundo_t *undos;
    char path[VRES_PATH_MAX];
    pid_t pid = vres_get_id(resource);

    vres_get_action_path(resource, path);
    entry = vres_file_get_entry(path, 0, FILE_RDWR);
    if (!entry)
        return;
    undos = vres_file_get_desc(entry, vres_semundo_t);
    undo_size = vres_semundo_size(undos);
    total = vres_file_items_count(entry, undo_size);
    for (i = 0; i < total; i++) {
        vres_semundo_t *undo = vres_sem_undo_at(undos, i);

        if (undo->pid == pid) {
            vres_sem_free_undo(undo);
            break;
        }
    }
    vres_file_put_entry(entry);
}


int vres_sem_remove_all_undos(vres_t *resource)
{
    char path[VRES_PATH_MAX];

    vres_get_action_path(resource, path);
    return vres_file_remove(path);
}


vres_reply_t *vres_sem_semop(vres_req_t *req,  int flags)
{
    size_t len;
    int max = 0;
    long ret = 0;
    int undos = 0;
    struct sembuf *sop;
    vres_reply_t *reply;
    vres_semundo_t *un = NULL;
    struct vres_sem_array *sma;
    vres_t *resource = &req->resource;
    vres_semop_arg_t *arg = (vres_semop_arg_t *)req->buf;
    struct sembuf *sops = arg->sops;
    unsigned nsops = arg->nsops;

    len = sizeof(vres_semop_arg_t) + nsops * sizeof(struct sembuf);
    if ((flags & VRES_CANCEL) || (req->length < len))
        return vres_reply_err(-EINVAL);
    sma = vres_sem_check(resource);
    if (!sma)
        return vres_reply_err(-EFAULT);
    for (sop = sops; sop < sops + nsops; sop++) {
        if (sop->sem_num >= max)
            max = sop->sem_num;
        if (sop->sem_flg & SEM_UNDO)
            undos = 1;
    }
    if (undos) {
        un = vres_sem_check_undo(resource);
        if (!un) {
            un = vres_sem_alloc_undo(sma->sem_nsems, vres_get_id(resource));
            if (!un) {
                free(sma);
                return vres_reply_err(-EFAULT);
            }
        }
    }
    if (max >= sma->sem_nsems) {
        log_err("invalid sem");
        ret = -EFBIG;
        goto out;
    }
    for (sop = sops; sop < sops + nsops; sop++) {
        int semval;
        int sem_op = sop->sem_op;
        struct vres_sem *curr = sma->sem_base + sop->sem_num;

        semval = curr->semval + sem_op;
        if ((!sem_op && curr->semval) || (semval < 0)) {
            if (!(flags & VRES_REDO) && !(sop->sem_flg & IPC_NOWAIT)) {
                vres_index_t index;
                char path[VRES_PATH_MAX];

                vres_get_record_path(resource, path);
                ret = vres_record_save(path, req, &index);
                if (!ret)
                    ret = vres_index_to_err(index);
            } else {
                log_resource_debug(resource, "retry, semop=%d, semval=%d (old=%d)", sem_op, semval, curr->semval);
                ret = -EAGAIN;
            }
            goto out;
        }
        if (semval > VRES_SEMVMX) {
            log_err("invalid sem");
            ret = -ERANGE;
            goto out;
        }
        if (sop->sem_flg & SEM_UNDO) {
            int undo = un->semadj[sop->sem_num] - sem_op;

            if (undo < (-VRES_SEMVMX - 1) || undo > VRES_SEMVMX) {
                log_err("invalid undo");
                ret = -ERANGE;
                goto out;
            }
            un->semadj[sop->sem_num] -= sop->sem_op;
        }
        curr->semval = semval;
        curr->sempid =vres_get_id(resource);
        log_sem_semop(resource->key, sop->sem_num, sem_op, semval, vres_get_id(resource));
    }
    sma->sem_otime = time(0);
    if (un)
        ret = vres_sem_update_undo(resource, un);
    if (ret) {
        log_err("failed to update undo");
        ret = -EFAULT;
        goto out;
    }
    ret = vres_sem_update(resource, sma);
    if (ret) {
        log_err("failed to update sem");
        ret = -EFAULT;
        goto out;
    }
    if (!(flags & VRES_REDO)) {
        ret = vres_redo_all(resource, 0);
        if (ret) {
            log_err("failed to redo");
            ret = -EFAULT;
            goto out;
        }
    }
out:
    free(sma);
    if (undos)
        free(un);
    vres_create_reply(vres_semop_result_t, ret, reply);
    return reply;
}


// get the number of tasks waiting on semval being nonzero
static int vres_sem_getncnt(vres_t *resource, int semnum)
{
    int ret;
    int ncnt = 0;
    vres_index_t index;
    char path[VRES_PATH_MAX];

    vres_get_record_path(resource, path);
    ret = vres_record_head(path, &index);
    if (-ENOENT == ret)
        return 0;
    else if (ret)
        return -EINVAL;
    for (;;) {
        int i;
        struct sembuf *sops;
        vres_record_t record;
        vres_semop_arg_t *arg;

        ret = vres_record_get(path, index, &record);
        if (ret)
            return ret;
        arg = (vres_semop_arg_t *)record.req->buf;
        sops = arg->sops;
        for (i = 0; i < arg->nsops; i++)
            if ((sops[i].sem_num == semnum) && (sops[i].sem_op < 0) && !(sops[i].sem_flg & IPC_NOWAIT))
                ncnt++;
        vres_record_put(&record);
        ret = vres_record_next(path, &index);
        if (-ENOENT == ret)
            break;
        else if (ret)
            return -EINVAL;
    }
    return ncnt;
}


// get the number of tasks waiting on semval being zero
static int vres_sem_getzcnt(vres_t *resource, int semnum)
{
    int ret = 0;
    int zcnt = 0;
    vres_index_t index;
    char path[VRES_PATH_MAX];

    vres_get_record_path(resource, path);
    ret = vres_record_head(path, &index);
    if (-ENOENT == ret)
        return 0;
    else if (ret)
        return -EINVAL;

    for (;;) {
        int i;
        struct sembuf *sops;
        vres_record_t record;
        vres_semop_arg_t *arg;

        ret = vres_record_get(path, index, &record);
        if (ret)
            return ret;
        arg = (vres_semop_arg_t *)record.req->buf;
        sops = arg->sops;
        for (i = 0; i < arg->nsops; i++)
            if ((sops[i].sem_num == semnum) && (0 == sops[i].sem_op) && !(sops[i].sem_flg & IPC_NOWAIT))
                zcnt++;
        vres_record_put(&record);
        ret = vres_record_next(path, &index);
        if (-ENOENT == ret)
            break;
        else if (ret)
            return -EINVAL;
    }
    return zcnt;
}


void vres_sem_set(struct seminfo *seminfo)
{
    memset(seminfo, 0, sizeof(struct seminfo));
    seminfo->semmni = VRES_SEMMNI;
    seminfo->semmns = VRES_SEMMNS;
    seminfo->semmsl = VRES_SEMMSL;
    seminfo->semopm = VRES_SEMOPM;
    seminfo->semvmx = VRES_SEMVMX;
    seminfo->semmnu = VRES_SEMMNU;
    seminfo->semmap = VRES_SEMMAP;
    seminfo->semume = VRES_SEMUME;
    seminfo->semusz = VRES_SEMUSZ;
    seminfo->semaem = VRES_SEMAEM;
}


vres_reply_t *vres_sem_semctl(vres_req_t *req, int flags)
{
    long ret = 0;
    vres_reply_t *reply = NULL;
    struct vres_sem *sem = NULL;
    vres_semctl_result_t *result;
    struct vres_sem_array *sma = NULL;
    vres_t *resource = &req->resource;
    vres_semctl_arg_t *arg = (vres_semctl_arg_t *)req->buf;
    int outlen = sizeof(vres_semctl_result_t);
    int semnum = arg->semnum;
    int cmd = arg->cmd;
    int nsems;

    if ((flags & VRES_CANCEL) || (req->length < sizeof(vres_semctl_arg_t)))
        return vres_reply_err(-EINVAL);
    sma = vres_sem_check(resource);
    if (!sma)
        return vres_reply_err(-EFAULT);
    nsems = sma->sem_nsems;
    switch (cmd) {
    case IPC_INFO:
    case SEM_INFO:
        outlen += sizeof(struct seminfo);
        break;
    case IPC_STAT:
    case SEM_STAT:
        outlen += sizeof(struct semid_ds);
        break;
    case GETALL:
        outlen += sizeof(unsigned short) * nsems;
        break;
    case GETVAL:
    case GETPID:
    case SETVAL:
        sem = &sma->sem_base[semnum];
        break;
    default:
        break;
    }
    reply = vres_reply_alloc(outlen);
    if (!reply) {
        free(sma);
        return vres_reply_err(-ENOMEM);
    }
    result = vres_result_check(reply, vres_semctl_result_t);
    switch (cmd) {
    case SEM_NSEMS:
        ret = nsems;
        break;
    case IPC_SET:
        memcpy(sma, (struct semid_ds *)&arg[1], sizeof(struct semid_ds));
        sma->sem_ctime = time(0);
        ret = vres_sem_update(resource, sma);
        break;
    case IPC_RMID:
        vres_redo_all(resource, VRES_CANCEL);
        vres_remove(resource);
        break;
    case IPC_INFO:
    case SEM_INFO:
    {
        struct seminfo *seminfo = (struct seminfo *)&result[1];

        vres_sem_set(seminfo);
        if (SEM_INFO == cmd) {
            seminfo->semusz = vres_get_resource_count(VRES_CLS_SEM);
            // TODO: set seminfo->semaem
        }
        ret = vres_get_max_key(VRES_CLS_SEM);
        break;
    }
    case SEM_STAT:
        ret = resource->key;
    case IPC_STAT:
        memcpy((struct semid_ds *)&result[1], sma, sizeof(struct semid_ds));
        break;
    case GETALL:
    {
        int i;
        unsigned short *array = (unsigned short *)&result[1];

        for (i = 0; i < nsems; i++)
            array[i] = sma->sem_base[i].semval;
        break;
    }
    case SETALL:
    {
        int i;
        unsigned short *array = (unsigned short *)&arg[1];

        for (i = 0; i < nsems; i++) {
            if (array[i] > VRES_SEMVMX) {
                ret = -ERANGE;
                goto out;
            }
        }
        vres_sem_remove_all_undos(resource);
        for (i = 0; i < nsems; i++) {
            sma->sem_base[i].semval = array[i];
            log_sem_semctl(resource, "SETALL, semnum=%d, semval=%d", i, array[i]);
        }
        sma->sem_ctime = time(0);
        ret = vres_sem_update(resource, sma);
        if (!ret)
            ret = vres_redo_all(resource, 0);
        break;
    }
    case GETVAL:
        ret = sem->semval;
        log_sem_semctl(resource, "GETVAL, semnum=%d, semval=%d", semnum, sem->semval);
        break;
    case GETPID:
        ret = sem->sempid;
        break;
    case GETNCNT:
        ret = vres_sem_getncnt(resource, semnum);
        break;
    case GETZCNT:
        ret = vres_sem_getzcnt(resource, semnum);
        break;
    case SETVAL:
        vres_sem_reset_undos(resource, semnum);
        sem->semval = *(int *)&arg[1];
        sem->sempid = vres_get_id(resource);
        sma->sem_ctime = time(0);
        ret = vres_sem_update(resource, sma);
        if (!ret)
            ret = vres_redo_all(resource, 0);
        log_sem_semctl(resource, "SETVAL, semnum=%d, semval=%d", semnum, sem->semval);
        break;
    default:
        ret = -EINVAL;
        break;
    }
out:
    if (sma)
        free(sma);
    result->retval = ret;
    return reply;
}


void vres_sem_exit(vres_req_t *req)
{
    int i;
    vres_semundo_t *un;
    struct vres_sem_array *sma;
    vres_t *resource = &req->resource;

    sma = vres_sem_check(resource);
    if (!sma)
        return;
    un = vres_sem_check_undo(resource);
    if (!un) {
        free(sma);
        return;
    }
    for (i = 0; i < sma->sem_nsems; i++) {
        struct vres_sem * sem = &sma->sem_base[i];

        if (un->semadj[i]) {
            sem->semval += un->semadj[i];
            if (sem->semval < 0)
                sem->semval = 0;
            if (sem->semval > VRES_SEMVMX)
                sem->semval = VRES_SEMVMX;
            sem->sempid = vres_get_id(resource);
        }
    }
    sma->sem_otime = time(0);
    vres_sem_update(resource, sma);
    vres_sem_remove_undo(resource);
    free(un);
    free(sma);
}
