#include "map.h"

static inline void ckpt_map_addr2str(unsigned long addr, char *str)
{
    sprintf(str, "0x%lx", addr);
}


static inline int ckpt_map_get_path(char *node, pid_t pid, unsigned long area, char *path)
{
    if ('\0' != *node)
        sprintf(path, "%s/%s/%lx_%lx", PATH_MAP, node, (unsigned long)pid, area);
    else
        sprintf(path, "%s/%lx_%lx", PATH_MAP, (unsigned long)pid, area);
    return 0;
}


int ckpt_map_setattr(char *node, struct task_struct *tsk, unsigned long area, char *key, void *value, size_t size)
{
    int ret;
    struct file *file;
    char path[MAP_PATH_MAX];

    ret = ckpt_map_get_path(node, klnk_get_gpid(tsk), area, path);
    if (ret) {
        log_err("failed to get path");
        return ret;
    }
    file = filp_open(path, MAP_FLAG, MAP_MODE);
    if (IS_ERR(file)) {
        log_err("failed to open, path=%s", path);
        return PTR_ERR(file);
    }
    ret = vfs_setxattr(file->f_dentry, key, value, size, 0);
    filp_close(file, NULL);
    log_map_setattr(path);
    return 0;
}


int ckpt_map(char *node, struct task_struct *tsk, unsigned long area, unsigned long address, void *buf, size_t len)
{
    int ret;
    char name[MAP_PATH_MAX];
    unsigned long off = address - area;

    ckpt_map_addr2str(off, name);
    ret = ckpt_map_setattr(node, tsk, area, name, buf, len);
    log_map(name, area, address, len);
    return ret;
}


int ckpt_map_attach(char *node, pid_t gpid, unsigned long area, size_t size, int prot, int flags)
{
    struct file *file;
    unsigned long ret;
    unsigned long populate;
    char path[MAP_PATH_MAX];

    ret = ckpt_map_get_path(node, gpid, area, path);
    if (ret) {
        log_err("failed to get path");
        return ret;
    }
    file = filp_open(path, MAP_FLAG, MAP_MODE);
    if (IS_ERR(file)) {
        log_err("failed to open, path=%s", path);
        return PTR_ERR(file);
    }
    ret = do_mmap(file, area, size, prot, flags, 0, &populate, NULL);
    if (populate)
        mm_populate(ret, populate);
    if (ret != area) {
        log_err("failed to attach, ret=%d", (int)ret);
        return -EINVAL;
    }
    log_map_attach(path);
    return 0;
}
