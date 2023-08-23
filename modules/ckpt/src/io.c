#include "io.h"

ckpt_desc_t ckpt_open(const char *path, int flags, int mode)
{
    return filp_open(path, flags, mode);
}


int ckpt_close(ckpt_desc_t desc)
{
    return filp_close(desc, NULL);
}


int ckpt_read(ckpt_desc_t desc, void *buf, size_t len)
{
    int ret;
    size_t bytes = len;
    char *ptr = buf;
    mm_segment_t fs = get_fs();

    set_fs(KERNEL_DS);
    while (bytes > 0) {
        ret = vfs_read(desc, ptr, bytes, &ckpt_get_pos(desc));
        if (ret < 0)
            goto out;
        bytes -= ret;
        ptr += ret;
    }
    ret = len;
out:
    set_fs(fs);
    return ret;
}


int ckpt_uread(ckpt_desc_t desc, void *buf, size_t len)
{
    int ret;
    size_t bytes = len;
    char *ptr = buf;

    while (bytes > 0) {
        ret = vfs_read(desc, ptr, bytes, &ckpt_get_pos(desc));
        if (ret < 0)
            return ret;
        bytes -= ret;
        ptr += ret;
    }
    return len;
}


int ckpt_write(ckpt_desc_t desc, void *buf, size_t len)
{
    int ret;
    size_t bytes = len;
    const char *ptr = buf;
    mm_segment_t fs = get_fs();

    set_fs(KERNEL_DS);
    while (bytes > 0) {
        ret = vfs_write(desc, ptr, bytes, &ckpt_get_pos(desc));
        if (ret < 0)
            goto out;
        bytes -= ret;
        ptr += ret;
    }
    ret = len;
out:
    set_fs(fs);
    return ret;
}
