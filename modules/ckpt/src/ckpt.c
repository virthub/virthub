#include "io.h"
#include "ckpt.h"
#include "dump.h"
#include "util.h"
#include "restore.h"

void ckpt_init_header(ckpt_header_t *hdr)
{
    strncpy(hdr->signature, "CKPT", 4);
    hdr->major = CKPT_MAJOR;
    hdr->minor = CKPT_MINOR;
}


int ckpt_check_header(ckpt_header_t *hdr)
{
    if (strncmp(hdr->signature, "CKPT", 4)
        || (hdr->major != CKPT_MAJOR)
        || (hdr->minor != CKPT_MINOR)) {
        log_err("invalid checkpoint header");
        return -EINVAL;
    }

    return 0;
}


int ckpt_do_dump(char *node, struct task_struct *tsk, char *path)
{
    int ret;
    ckpt_desc_t desc;
    ckpt_header_t hdr;

    desc = ckpt_open(path, CKPT_WRONLY, CKPT_MODE);
    if (IS_ERR(desc))
        return PTR_ERR(desc);

    ckpt_init_header(&hdr);
    if (ckpt_write(desc, (void *)&hdr, sizeof(ckpt_header_t)) != sizeof(ckpt_header_t)) {
        ret = -EIO;
        goto out;
    }

    ret = ckpt_dump_task(node, tsk, desc);
    if (ret)
        goto out;

    ret = ckpt_write(desc, NULL, 0);
out:
    ckpt_close(desc);
    return ret;
}


int ckpt_do_restore(char *node, pid_t gpid, char *path)
{
    int ret;
    ckpt_desc_t desc;
    ckpt_header_t hdr;

    desc = ckpt_open(path, CKPT_RDONLY, 0);
    if (IS_ERR(desc))
        return PTR_ERR(desc);

    if (ckpt_read(desc, (void *)&hdr, sizeof(ckpt_header_t)) != sizeof(ckpt_header_t)) {
        ret = -EIO;
        goto out;
    }

    ret = ckpt_check_header(&hdr);
    if (ret)
        goto out;

    ret = ckpt_restore_task(node, gpid, desc);
out:
    ckpt_close(desc);
    return ret;
}


int ckpt_suspend(pid_t pid)
{
    struct task_struct *tsk;

    tsk = find_task_by_vpid(pid);
    if (!tsk) {
        log_debug("cannot find task, pid=%d", pid);
        return -ENOENT;
    }

    send_sig(SIGSTOP, tsk, 0);
    log_debug("pid=%d", pid);
    return 0;
}


int ckpt_resume(pid_t pid)
{
    struct task_struct *tsk;

    tsk = find_task_by_vpid(pid);
     if (!tsk) {
        log_debug("cannot find task, pid=%d", pid);
        return -ENOENT;
    }

    send_sig(SIGCONT, tsk, 0);
    log_debug("pid=%d", pid);
    return 0;
}


int ckpt_dump(char *node, pid_t pid, char *path, int flags)
{
    int ret;
    struct task_struct *tsk;

    tsk = find_task_by_vpid(pid);
    if (!tsk) {
        log_debug("cannot find task, pid=%d", pid);
        return -ENOENT;
    }

    ret = ckpt_do_dump(node, tsk, path);
    if (ret) {
        log_err("failed to take checkpoint");
        return ret;
    }

    if (flags & CKPT_KILL)
        send_sig(SIGKILL, tsk, 0);
    else if (flags & CKPT_CONT)
        send_sig(SIGCONT, tsk, 0);

    log_debug("path=%s", path);
    return 0;
}


int ckpt_restore(char *node, pid_t gpid, char *path, int flags)
{
    if (ckpt_do_restore(node, gpid, path)) {
        log_err("failed to restore");
        return -EFAULT;
    }

    if (!(flags & CKPT_STOP))
        send_sig(SIGCONT, current, 0);

    log_debug("path=%s", path);
    return 0;
}


/* Device Declarations */
/* The name for our device, as it will appear in /proc/devices */
static long ckpt_ioctl(struct file *filp, unsigned int cmd, unsigned long val)
{
    ckpt_arg_t arg;

    if ((_IOC_TYPE(cmd) != CKPT_MAGIC) || (_IOC_NR(cmd) > CKPT_MAX_IOC_NR))
        return -EINVAL;

    if (copy_from_user(&arg, (ckpt_arg_t *)val, sizeof(ckpt_arg_t)))
        return -EFAULT;

    switch(cmd) {
    case CKPT_IOCTL_DUMP:
        ckpt_dump(arg.node, arg.id, arg.path, arg.flags);
        break;
    case CKPT_IOCTL_RESTORE:
        ckpt_restore(arg.node, arg.id, arg.path, arg.flags);
        break;
    case CKPT_IOCTL_SUSPEND:
        ckpt_suspend(arg.id);
        break;
    case CKPT_IOCTL_RESUME:
        ckpt_resume(arg.id);
        break;
    default:
        break;
    }

    return 0;
}


/* Module Declarations */
static int major;
struct cdev ckpt_dev;
struct class *ckpt_class;
struct file_operations ckpt_ops = {
    owner: THIS_MODULE,
    unlocked_ioctl: ckpt_ioctl,
};


static int ckpt_init(void)
{
    int ret;
    int devno;
    dev_t dev;

    ret = alloc_chrdev_region(&dev, 0, 1, CKPT_DEVICE_NAME);
    if(ret < 0) {
        log_err("failed to initialize");
        return ret;
    }

    major = MAJOR(dev);
    devno = MKDEV(major, 0);

    cdev_init(&ckpt_dev, &ckpt_ops);
    ckpt_dev.owner = THIS_MODULE;
    ckpt_dev.ops = &ckpt_ops;
    ret = cdev_add(&ckpt_dev, dev, 1);
    if (ret) {
        log_err("failed to initialize");
        goto out;
    }

    ckpt_class = class_create(THIS_MODULE, "cls_ckpt");
    if (IS_ERR(ckpt_class)) {
        log_err("failed to initialize");
        cdev_del(&ckpt_dev);
        ret = -EINVAL;
        goto out;
    }

    device_create(ckpt_class, NULL, devno, NULL, CKPT_DEVICE_NAME);
    return 0;
out:
    unregister_chrdev_region(dev, 1);
    return ret;
}

static void ckpt_cleanup(void)
{
    dev_t dev;

    dev = MKDEV(major, 0);
    cdev_del(&ckpt_dev);
    device_destroy(ckpt_class, dev);
    class_destroy(ckpt_class);
    unregister_chrdev_region(dev, 1);
}

module_init(ckpt_init);
module_exit(ckpt_cleanup);
MODULE_LICENSE("Dual MIT/GPL");
