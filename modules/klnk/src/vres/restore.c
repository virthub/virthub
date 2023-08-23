#include "restore.h"

int vres_restore_resource(vres_t *resource, int flags)
{
    int ret;
    FILE *file;
    int pos = 0;
    size_t length;
    char path[VRES_PATH_MAX];
    char dirname[VRES_PATH_MAX];

    if (!(flags & CKPT_LOCAL))
        return 0;

    ret = vres_ckpt_get_path(resource, RES_NAME, path);
    if (ret) {
        log_resource_err(resource, "invalid path");
        return ret;
    }

    file = fopen(path, "r");
    if (!file)
        return 0;

    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);

    while (pos < length) {
        size_t len;
        char *buf = NULL;
        vres_file_t *filp;

        fscanf(file, "%s", path);
        fseek(file, 1, SEEK_CUR);
        len = strlen(path);
        if (!len) {
            log_resource_err(resource, "failed to read path");
            ret = -EIO;
            break;
        }
        pos += len + 1;
        ret = vres_file_dirname(path, dirname);
        if (ret) {
            log_resource_err(resource, "failed to get directory");
            break;
        }
        if (!vres_file_is_dir(dirname)) {
            ret = vres_file_mkdir(dirname);
            if (ret) {
                log_resource_err(resource, "failed to create");
                break;
            }
        }
        if (fread(&len, sizeof(size_t), 1, file) != 1) {
            ret = -EIO;
            break;
        }
        pos += sizeof(size_t);
        log_restore_file(path, len);
        if (len) {
            buf = malloc(len);
            if (!buf) {
                log_resource_err(resource, "no memory");
                ret = -ENOMEM;
                break;
            }
            if (fread(buf, 1, len, file) != len) {
                log_resource_err(resource, "failed to read");
                ret = -EIO;
                free(buf);
                break;
            }
            pos += len;
        }
        filp = vres_file_open(path, "w");
        if (!filp) {
            log_resource_err(resource, "failed to open file");
            ret = -ENOENT;
            if (buf)
                free(buf);
            break;
        }
        if (buf) {
            if (vres_file_write(buf, 1, len, filp) != len) {
                log_resource_err(resource, "failed to write");
                ret = -EIO;
            }
            free(buf);
        }
        vres_file_close(filp);
        if (ret)
            break;
    }
    fclose(file);
    log_restore_resource(resource);
    return ret;
}


int vres_restore_task(vres_t *resource, int flags)
{
    int ret;
    vres_id_t id;

    if (!(flags & CKPT_LOCAL))
        return 0;

    ret = vres_tsk_get(resource, &id);
    if (ret || (id != resource->owner)) {
        log_resource_err(resource, "failed to get task");
        return -EINVAL;
    }
    log_restore_task(resource);
    return 0;
}


int vres_restore(vres_t *resource, int flags)
{
    int ret;

    log_debug("restore ...");
    ret = vres_restore_resource(resource, flags);
    if (ret) {
        log_resource_err(resource, "failed to restore resource");
        goto out;
    }
    ret = vres_restore_task(resource, flags);
    if (ret) {
        log_resource_err(resource, "failed to restore task");
        goto out;
    }
    ret = vres_ckpt_resume(resource, flags);
    if (ret) {
        log_resource_err(resource, "failed to resume");
        goto out;
    }
    log_restore(resource);
out:
    return ret;
}
