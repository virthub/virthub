#ifndef _CKPT_H
#define _CKPT_H

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include "resource.h"
#include "tsk.h"

#define TSK_NAME   "tsk"
#define RES_NAME   "res"

#define CKPT_LOCAL 0x0001

#ifdef SHOW_CKPT
#define LOG_CKPT
#define LOG_CKPT_CLEAR
#define LOG_CKPT_SUSPEND
#define LOG_CKPT_CHECK_DIR
#define LOG_CKPT_NOTIFY_OWNER
#define LOG_CKPT_NOTIFY_MEMBERS
#define LOG_CKPT_RESUME_MEMBERS
#define LOG_CKPT_SUSPEND_MEMBERS
#endif

#include "log_ckpt.h"

typedef struct vres_ckpt_result {
    long retval;
} vres_ckpt_result_t;

int vres_ckpt_clear(vres_t *resource);
int vres_ckpt_check_dir(vres_t *resource);
int vres_ckpt_resume(vres_t *resource, int flags);
int vres_ckpt_suspend(vres_t *resource, int flags);
int vres_ckpt_get_path_by_id(int id, const char *name, char *path);
int vres_ckpt_get_path(vres_t *resource, const char *name, char *path);

void vres_ckpt_init();
vres_reply_t *vres_ckpt(vres_req_t *req, int flags);

#define CKPT_MAGIC         0xcc
#define CKPT_IOCTL_DUMP    _IOW(CKPT_MAGIC, 1, ckpt_arg_t)
#define CKPT_IOCTL_SUSPEND _IOW(CKPT_MAGIC, 2, ckpt_arg_t)
#define CKPT_IOCTL_RESTORE _IOW(CKPT_MAGIC, 3, ckpt_arg_t)
#define CKPT_IOCTL_RESUME  _IOW(CKPT_MAGIC, 4, ckpt_arg_t)

#define CKPT_CONT          0x00000001
#define CKPT_KILL          0x00000002
#define CKPT_STOP          0x00000004

#define CKPT_DEV_FILE      "/dev/ckpt"

typedef struct ckpt_arg {
    int id;
    int flags;
    char node[32];
    char path[512];
} ckpt_arg_t;

#endif
