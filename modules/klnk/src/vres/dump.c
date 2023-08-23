#include "dump.h"

static inline int vres_dump_file(char *path, void *arg)
{
    char *buf;
    size_t len;
    int ret = 0;
    vres_file_entry_t *entry;
    FILE *filp = (FILE *)arg;

    entry = vres_file_get_entry(path, 0, FILE_RDONLY);
    if (!entry) {
        log_err("no entry");
        return -ENOENT;
    }
    buf = vres_file_get_desc(entry, char);
    len = vres_file_items_count(entry, 1);
    fprintf(filp, "%s\n", path);
    if (fwrite(&len, sizeof(len), 1, filp) != 1)
        ret = -EIO;
    else if (len) {
        if (fwrite(buf, 1, len, filp) != len)
            ret = -EIO;
    }
    vres_file_put_entry(entry);
    if (!ret)
        log_dump_file(path, len);
    else
        log_err("failed to dump %s", path);
    return ret;
}


int vres_dump_task(vres_t *resource, int flags)
{
    int fd;
    int ret;
    ckpt_arg_t arg;
    vres_desc_t desc;

    if (!(flags & CKPT_LOCAL))
        return 0;
    ret = vres_lookup(resource, &desc);
    if (ret) {
        log_err("failed to lookup");
        return ret;
    }
    memset(&arg, 0, sizeof(ckpt_arg_t));
    arg.flags = CKPT_KILL;
    arg.id = desc.id;
    strcpy(arg.node, mds_name);
    ret = vres_ckpt_get_path_by_id(resource->owner, TSK_NAME, arg.path);
    if (ret) {
        log_err("failed to get path");
        return ret;
    }
    fd = open(CKPT_DEV_FILE, O_RDONLY);
    if (fd < 0) {
        log_err("failed to open file");
        return -EINVAL;
    }
    ret = ioctl(fd, CKPT_IOCTL_DUMP, (int_t)&arg);
    log_dump_task(resource);
    close(fd);
    return ret;
}


int vres_dump_resource(vres_t *resource, int flags)
{
    int ret;
    FILE *filp;
    char path[VRES_PATH_MAX];
    char root[VRES_PATH_MAX];

    if (!(flags & CKPT_LOCAL))
        return 0;
    vres_get_root_path(resource, root);
    ret = vres_ckpt_get_path_by_id(resource->owner, RES_NAME, path);
    if (ret) {
        log_err("failed to get path");
        return ret;
    }
    filp = fopen(path, "w");
    if (!filp) {
        log_err("no entry");
        return -ENOENT;
    }
    ret = vres_file_handle_file(vres_dump_file, root, (void *)filp);
    log_dump_resource(resource);
    fclose(filp);
    return ret;
}


int vres_dump(vres_t *resource, int flags)
{
    int ret;

    log_debug("dump ...");
    ret = vres_ckpt_check_dir(resource);
    if (ret) {
        log_resource_err(resource, "failed to check");
        goto out;
    }
    ret = vres_ckpt_clear(resource);
    if (ret) {
        log_resource_err(resource, "failed to clear");
        goto out;
    }
    ret = vres_ckpt_suspend(resource, flags);
    if (ret) {
        log_resource_err(resource, "failed to suspend");
        goto out;
    }
    ret = vres_dump_resource(resource, flags);
    if (ret) {
        log_resource_err(resource, "failed to dump resource");
        goto out;
    }
    ret = vres_dump_task(resource, flags);
    if (ret) {
        log_resource_err(resource, "failed to dump task");
        goto out;
    }
    log_dump(resource);
out:
    return ret;
}
