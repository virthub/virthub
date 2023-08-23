#include "debug.h"

static inline unsigned long ckpt_update_checksum(unsigned long old, unsigned long new)
{
    return ((old + new) & 0xff);
}


unsigned long ckpt_get_checksum(char *buf, size_t len)
{
    int i;
    unsigned long ret = 0;

    for (i = 0; i < len; i++)
        ret = ckpt_update_checksum(ret, buf[i]);
    return ret;
}


static int ckpt_read_file(struct path *path, loff_t off, char *buf, size_t len)
{
    int ret = 0;
    char *name;
    char *p = buf;
    char *path_buf;
    struct file *file;
    mm_segment_t fs = get_fs();

    memset(buf, 0, len);
    path_buf = (char *)kmalloc(CKPT_PATH_MAX, GFP_KERNEL);
    if (!path_buf) {
        log_err("no memory");
        return -ENOMEM;
    }

    name = d_path(path, path_buf, CKPT_PATH_MAX);
    if (IS_ERR(name)) {
        log_err("invalid path");
        ret = PTR_ERR(name);
        goto out;
    }

    file = filp_open(name, O_RDONLY | O_LARGEFILE, 0);
    if (IS_ERR(file)) {
        log_err("failed to open file");
        ret = PTR_ERR(file);
        goto out;
    }

    set_fs(KERNEL_DS);
    while (len > 0) {
        int n = vfs_read(file, p, len, &off);
        if (n <= 0) {
            if (n < 0) {
                log_err("failed to read");
                ret = -EIO;
            }
            break;
        }
        len -= n;
        p += n;
    }
    set_fs(fs);
    filp_close(file, NULL);
out:
    kfree(path_buf);
    return ret;
}


unsigned long ckpt_debug_checksum(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr)
{
    int map;
    struct file *file;
    unsigned long ret = 0;

    if (!vma) {
        vma = find_vma(mm, addr);
        if (!vma) {
            log_err("cannot find vma");
            return 0;
        }
    }

    if (addr < vma->vm_start) {
        log_err("invalid address");
        return 0;
    }

    file = vma->vm_file;
    map = ckpt_can_map(vma);
    while (addr < vma->vm_end) {
        char *buf = ckpt_get_page(mm, vma, addr);

        if (!buf) {
            if (!file || map) {
                buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
                if (!buf) {
                    log_err("no memory");
                    return 0;
                }

                if (copy_from_user(buf, (void *)addr, PAGE_SIZE)) {
                    log_err("failed to copy from %lx", addr);
                    kfree(buf);
                    return 0;
                }

                ret = ckpt_update_checksum(ret, ckpt_get_checksum(buf, PAGE_SIZE));
                kfree(buf);
            } else {
                loff_t off = addr - vma->vm_start + (vma->vm_pgoff << PAGE_SHIFT);

                buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
                if (!buf) {
                    log_err("no memory");
                    return 0;
                }

                if (ckpt_read_file(&file->f_path, off, buf, PAGE_SIZE)) {
                    log_err("failed to read file");
                    kfree(buf);
                    return 0;
                }

                ret = ckpt_update_checksum(ret, ckpt_get_checksum(buf, PAGE_SIZE));
                kfree(buf);
            }
        } else if (buf)
            ret = ckpt_update_checksum(ret, ckpt_get_checksum(buf, PAGE_SIZE));
        addr += PAGE_SIZE;
    }

    return ret;
}
