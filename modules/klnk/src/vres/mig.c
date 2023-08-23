#include "mig.h"

#define CHECK_INTERVAL 100000 // usec

int vres_mig_check(vres_t *resource)
{
    char path[VRES_PATH_MAX];

    vres_get_mig_path(resource, path);
    if (!vres_file_access(path, F_OK)) {
        vres_file_remove(path);
        return -EAGAIN;
    } else {
        int ret = 0;

        vres_get_loc_path(resource, path);
        if (vres_file_access(path, F_OK)) {
            vres_file_t *filp = vres_file_open(path, "w");

            if (!filp) {
                log_resource_err(resource, "no entry");
                return -ENOENT;
            }
            if (vres_file_write(&node_addr, sizeof(vres_addr_t), 1, filp) != 1) {
                log_resource_err(resource, "failed to write");
                ret = -EIO;
            }
            vres_file_close(filp);
        }
        return ret;
    }
}


void *vres_mig_create(void *arg)
{
    vres_t *resource = (vres_t *)arg;

    while (!vres_event_exists(resource))
        vres_sleep(CHECK_INTERVAL);
    vres_dump(resource, CKPT_LOCAL);
    free(arg);
    log_mig_create(resource);
    return NULL;
}


int vres_mig_set(vres_t *resource, vres_addr_t *addr, vres_arg_t *arg)
{
    int ret = 0;
    vres_file_t *filp;
    char path[VRES_PATH_MAX];

    vres_get_mig_path(resource, path);
    filp = vres_file_open(path, "w");
    if (!filp) {
        log_resource_err(resource, "failed to open");
        ret = -ENOENT;
    } else {
        if (vres_file_write(addr, sizeof(vres_addr_t), 1, filp) != 1) {
            log_resource_err(resource, "failed to write");
            ret = -EIO;
        }
        vres_file_close(filp);
    }
    if (!ret) {
        vres_t *res;
        pthread_t tid;
        pthread_attr_t attr;

        res = (vres_t *)malloc(sizeof(vres_t));
        if (!res) {
            log_resource_err(resource, "no memory");
            return -ENOMEM;
        }
        arg->index = 0;
        memcpy(res, resource, sizeof(vres_t));
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
        ret = pthread_create(&tid, &attr, vres_mig_create, res);
        pthread_attr_destroy(&attr);
        if (ret) {
            log_resource_err(resource, "failed to create thread");
            free(res);
        }
    }
    log_mig_set(resource);
    return ret;
}


int vres_mig_fst(vres_addr_t *nodes, int total, vres_addr_t *addr)
{
    memcpy(addr, nodes, sizeof(vres_addr_t));
    return 0;
}


int vres_mig_lst(vres_addr_t *nodes, int total, vres_addr_t *addr)
{
    memcpy(addr, &nodes[total - 1], sizeof(vres_addr_t));
    return 0;
}


int vres_mig_nxt(vres_addr_t *nodes, int total, vres_addr_t *addr)
{
    int i;

    for (i = 0; i < total; i++)
        if (!memcmp(&nodes[i], &node_addr, sizeof(vres_addr_t)))
            break;
    if (i == total) {
        log_err("cannot find node");
        return -EINVAL;
    }
    if (i < total)
        i++;
    memcpy(addr, &nodes[i], sizeof(vres_addr_t));
    return 0;
}


int vres_mig_bac(vres_addr_t *nodes, int total, vres_addr_t *addr)
{
    int i;

    for (i = 0; i < total; i++)
        if (!memcmp(&nodes[i], &node_addr, sizeof(vres_addr_t)))
            break;
    if (i == total) {
        log_err("cannot find node");
        return -EINVAL;
    }
    if (i > 0)
        i--;
    memcpy(addr, &nodes[i], sizeof(vres_addr_t));
    return 0;
}


int vres_mig_rnd(vres_addr_t *nodes, int total, vres_addr_t *addr)
{
    int i;

    i = rand() % total;
    memcpy(addr, &nodes[i], sizeof(vres_addr_t));
    return 0;
}


int vres_mig_loc(vres_t *resource, vres_addr_t *addr)
{
    int ret = 0;
    vres_file_t *filp;
    char path[VRES_PATH_MAX];

    vres_get_loc_path(resource, path);
    filp = vres_file_open(path, "r");
    if (!filp) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    if (vres_file_read(addr, sizeof(vres_addr_t), 1, filp) != 1) {
        log_resource_err(resource, "failed to read");
        ret = -EIO;
    }
    vres_file_close(filp);
    return ret;
}


const char *vres_mig_path(vres_mig_arg_t *arg)
{
    char *path = arg->path;
    const int len = strlen(VRES_PATH_MIG);

    if (!path || strncmp(path, VRES_PATH_MIG, len)) {
        log_err("invalid path, path=%s", path);
        return NULL;
    }
    if (strlen(path) == len)
        return MIG_PATH_ROOT;
    else
        return &path[len];
}


int vres_migrate(vres_t *resource, vres_arg_t *arg)
{
    int total;
    const char *path;
    vres_addr_t addr;
    int ret = -EINVAL;
    vres_addr_t *nodes;

    path = vres_mig_path((vres_mig_arg_t *)arg->in);
    if (!path) {
        log_resource_err(resource, "failed to get path");
        return -EINVAL;
    }
    ret = vres_mig_check(resource);
    if (ret) {
        log_mig(resource, path);
        return ret == -EAGAIN ? ret : -EINVAL;
    }
    total = vres_node_list(&nodes);
    if (total <= 0) {
        log_resource_err(resource, "failed to get nodes");
        return -EINVAL;
    }
    if (!strcmp(path, MIG_PATH_FST))
        ret = vres_mig_fst(nodes, total, &addr);
    else if (!strcmp(path, MIG_PATH_LST))
        ret = vres_mig_lst(nodes, total, &addr);
    else if (!strcmp(path, MIG_PATH_NXT))
        ret = vres_mig_nxt(nodes, total, &addr);
    else if (!strcmp(path, MIG_PATH_BAC))
        ret = vres_mig_bac(nodes, total, &addr);
    else if (!strcmp(path, MIG_PATH_RND))
        ret = vres_mig_rnd(nodes, total, &addr);
    else if (!strcmp(path, MIG_PATH_LOC))
        ret = vres_mig_loc(resource, &addr);
    if (!ret) {
        if (!memcmp(&addr, &node_addr, sizeof(vres_addr_t)))
            ret = -EAGAIN;
        else {
            if (vres_mig_set(resource, &addr, arg)) {
                log_resource_err(resource, "failed to set");
                ret = -EINVAL;
            }
        }
    } else {
        log_resource_err(resource, "invalid path");
        ret = -EINVAL;
    }
    free(nodes);
    log_mig(resource, path);
    return ret;
}
