#ifndef _CKPT_H
#define _CKPT_H

#include <sys/ioctl.h>

#define CKPT_MAGIC            0xcc
#define CKPT_IOCTL_SAVE       _IOW(CKPT_MAGIC, 1, ckpt_arg_t)
#define CKPT_IOCTL_SUSPEND    _IOW(CKPT_MAGIC, 2, ckpt_arg_t)
#define CKPT_IOCTL_RESTORE    _IOW(CKPT_MAGIC, 3, ckpt_arg_t)
#define CKPT_IOCTL_RESUME     _IOW(CKPT_MAGIC, 4, ckpt_arg_t)

#define CKPT_CONT             0x00000001
#define CKPT_KILL             0x00000002
#define CKPT_STOP             0x00000004

#define CKPT_DEV_FILE         "/dev/ckpt"

typedef struct ckpt_arg {
    int id;
    int flags;
    char node[32];
    char path[512];
} ckpt_arg_t;

#endif
