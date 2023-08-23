#ifndef _SYS_H
#define _SYS_H

#include "ckpt.h"
#include "sysmap.h"
#include "common.h"

extern int *compat_suid_dumpable;

long sys_setgroups(int gidsetsize, gid_t __user *grouplist);
long sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid);
long sys_setresuid(uid_t ruid, uid_t euid, uid_t suid);
long sys_prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5);
long sys_mprotect(unsigned long start, size_t len, unsigned long prot);
long sys_mremap(unsigned long addr,
                        unsigned long old_len, unsigned long new_len,
                        unsigned long flags, unsigned long new_addr);
long sys_lseek(unsigned int fd, off_t offset, unsigned int origin);
struct task_struct *find_task_by_vpid(pid_t vpid);
void set_fs_root(struct fs_struct *fs, const struct path *path);
void set_fs_pwd(struct fs_struct *fs, const struct path *path);
void arch_pick_mmap_layout(struct mm_struct *mm);
void set_dumpable(struct mm_struct *mm, int value);
int do_sigaction(int sig, struct k_sigaction *act, struct k_sigaction *oact);
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp);
int groups_search(const struct group_info *group_info, kgid_t grp);
int __mm_populate(unsigned long start, unsigned long len, int ignore_errors);
int compat_sigaltstack(struct task_struct *tsk, const stack_t *ss, stack_t *oss, unsigned long sp);

#endif
