#ifndef _COMMON_H
#define _COMMON_H

#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/prctl.h>
#include <linux/module.h>
#include <linux/fdtable.h>
#include <linux/highmem.h>
#include <linux/syscalls.h>
#include <linux/fs_struct.h>
#include <linux/hugetlb_inline.h>

#define inside(a, b, c) ((c <= b) && (c >= a))

#define CKPT_DEVICE_NAME   "ckpt"
#define CKPT_VERSION       "0.0.1"
#define CKPT_MAJOR         1
#define CKPT_MINOR         6
#define CKPT_MAX_IOC_NR    4
#define CKPT_PATH_MAX      4096
#define CKPT_DEBUGREGS_MAX HBP_NUM + 2

typedef struct {
    uid_t uid,euid,suid,fsuid;
    gid_t gid,egid,sgid,fsgid;
    pid_t gpid;
} ckpt_cred_t;

typedef struct {
    unsigned long flags;
    unsigned long pgoff;
    unsigned long start;
    unsigned long end;
    unsigned long sz;
    int arch;
} ckpt_vma_t;

typedef struct {
    unsigned long start_code, end_code, start_data, end_data;
    unsigned long start_brk, brk, start_stack, start_mmap;
    unsigned long arg_start, arg_end, env_start, env_end;
    unsigned long start_seg, mmap_base;
    unsigned long context;
    int map_count;
} ckpt_mem_t;

typedef struct {
    unsigned int flags;
    mode_t mode;
    loff_t pos;
    int fd;
    int sz;
} ckpt_file_t;

typedef struct {
    int max_fds;
    int next_fd;
} ckpt_fs_t;

typedef struct {
    unsigned long debugregs[CKPT_DEBUGREGS_MAX];
    struct pt_regs pt_regs;
} ckpt_regs_t;

typedef struct {
    struct desc_struct tls_array[GDT_ENTRY_TLS_ENTRIES];
    int __user *clear_child_tid;
    unsigned long personality;
    char comm[TASK_COMM_LEN];
    void *sysenter_return;
    int ngroups;
} ckpt_misc_t;

typedef struct {
    struct siginfo queue_info;
    sigset_t pending_signal;
    int pending_count;
    int queue_flags;
} ckpt_sig_t;

typedef struct {
    char signature[4];
    int major;
    int minor;
} ckpt_header_t;

typedef struct {
} ckpt_fpu_t;

typedef union thread_xstate ckpt_i387_t;

#endif
