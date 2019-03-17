#ifndef _TYPES_H
#define _TYPES_H

#include <asm/i387.h>
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

typedef struct ckpt_cred {
    pid_t gpid;
    uid_t uid,euid,suid,fsuid;
    gid_t gid,egid,sgid,fsgid;
    int ngroups;
} ckpt_cred_t;

typedef struct ckpt_vma {
    unsigned long flags;
    unsigned long pgoff;
    unsigned long start;
    unsigned long end;
    unsigned long sz;
    int arch;
} ckpt_vma_t;

typedef struct ckpt_mm {
    unsigned long context;
    unsigned long start_code, end_code, start_data, end_data;
    unsigned long start_brk, brk, start_stack, start_mmap;
    unsigned long arg_start, arg_end, env_start, env_end;
    unsigned long start_seg, mmap_base;
    int map_count;
} ckpt_mm_t;

typedef struct ckpt_file {
    int fd;
    int sz;
    loff_t pos;
    mode_t mode;
    unsigned int flags;
} ckpt_file_t;

typedef struct ckpt_files_struct {
    int max_fds;
    int next_fd;
} ckpt_files_struct_t;

typedef struct ckpt_cpu {
    struct pt_regs regs;
    unsigned long debugregs[CKPT_DEBUGREGS_MAX];
    struct desc_struct tls_array[GDT_ENTRY_TLS_ENTRIES];
    void *sysenter_return;
} ckpt_cpu_t;

typedef struct ckpt_sigpending {
    sigset_t signal;
    int count;
} ckpt_sigpending_t;

typedef struct ckpt_sigqueue {
    int flags;
    struct siginfo info;
} ckpt_sigqueue_t;

typedef struct ckpt_ext {
    unsigned long personality;
    int __user *clear_child_tid;
    char comm[TASK_COMM_LEN];
} ckpt_ext_t;

typedef struct ckpt_header {
    char signature[4];
    int major;
    int minor;
} ckpt_header_t;

typedef union thread_xstate ckpt_i387_t;

#endif
