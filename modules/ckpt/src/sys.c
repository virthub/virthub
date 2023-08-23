#include "sys.h"

int *compat_suid_dumpable = (int *)SUID_DUMPABLE;

asmlinkage long sys_setgroups(int gidsetsize, gid_t __user *grouplist)
{
    asmlinkage long (*syscall)(int, gid_t __user *) = (void *)SYS_SETGROUPS;

    return syscall(gidsetsize, grouplist);
}


asmlinkage long sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
    asmlinkage long (*syscall)(gid_t, gid_t, gid_t) = (void *)SYS_SETRESGID;

    return syscall(rgid, egid, sgid);
}


asmlinkage long sys_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
    asmlinkage long (*syscall)(uid_t, uid_t, uid_t) = (void *)SYS_SETRESUID;

    return syscall(ruid, euid, suid);
}


asmlinkage long sys_prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5)
{
    asmlinkage long (*syscall)(int, unsigned long, unsigned long, unsigned long, unsigned long) = (void *)SYS_PRCTL;

    return syscall(option, arg2, arg3, arg4, arg5);
}


asmlinkage long sys_mprotect(unsigned long start, size_t len, unsigned long prot)
{
    asmlinkage long (*syscall)(unsigned long, size_t, unsigned long) = (void *)SYS_MPROTECT;

    return syscall(start, len, prot);
}


asmlinkage long sys_mremap(unsigned long addr,
                        unsigned long old_len, unsigned long new_len,
                        unsigned long flags, unsigned long new_addr)
{
    asmlinkage long (*syscall)(unsigned long,
                        unsigned long, unsigned long,
                        unsigned long, unsigned long) = (void *)SYS_MREMAP;

    return syscall(addr, old_len, new_len, flags, new_addr);
}


asmlinkage long sys_lseek(unsigned int fd, off_t offset, unsigned int origin)
{
    asmlinkage long (*syscall)(unsigned int, off_t, unsigned int) = (void *)SYS_LSEEK;

    return syscall(fd, offset, origin);
}


unsigned long do_mmap_pgoff(struct file *file, unsigned long addr,
                            unsigned long len, unsigned long prot,
                            unsigned long flags, unsigned long pgoff, unsigned long *populate)
{
    unsigned long (*syscall)(struct file *, unsigned long,
                            unsigned long, unsigned long,
                            unsigned long, unsigned long, unsigned long *) = (void *)DO_MMAP_PGOFF;

    return syscall(file, addr, len, prot, flags, pgoff, populate);
}


struct task_struct *find_task_by_vpid(pid_t vpid)
{
    struct task_struct *(*syscall)(pid_t) = (void *)FIND_TASK_BY_VPID;

    return syscall(vpid);
}


void set_fs_root(struct fs_struct *fs, const struct path *path)
{
    void (*syscall)(struct fs_struct *, const struct path *) = (void *)SET_FS_ROOT;

    syscall(fs, path);
}


void set_fs_pwd(struct fs_struct *fs, const struct path *path)
{
    void (*syscall)(struct fs_struct *, const struct path *) = (void *)SET_FS_PWD;

    syscall(fs, path);
}


void arch_pick_mmap_layout(struct mm_struct *mm)
{
    void (*syscall)(struct mm_struct *) = (void *)ARCH_PICK_MMAP_LAYOUT;

    syscall(mm);
}


void set_dumpable(struct mm_struct *mm, int value)
{
    void (*syscall)(struct mm_struct *, int) = (void *)SET_DUMPABLE;

    syscall(mm, value);
}


int do_sigaction(int sig, struct k_sigaction *act, struct k_sigaction *oact)
{
    int (*syscall)(int, struct k_sigaction *, struct k_sigaction *) = (void *)DO_SIGACTION;

    return syscall(sig, act, oact);
}


int group_send_sig_info(int sig, struct siginfo *info, struct task_struct *p)
{
    int (*syscall)(int sig, struct siginfo *info, struct task_struct *p) = (void *)GROUP_SEND_SIG_INFO;

    return syscall(sig, info, p);
}


int copy_siginfo_to_user(siginfo_t __user *to, siginfo_t *from)
{
    int (*syscall)(siginfo_t __user *, siginfo_t *) = (void *)COPY_SIGINFO_TO_USER;

    return syscall(to, from);
}


int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
    int (*syscall)(struct linux_binprm *, int) = (void *)ARCH_SETUP_ADDITIONAL_PAGES;

    return syscall(bprm, uses_interp);
}


int do_munmap(struct mm_struct *mm, unsigned long start, size_t len)
{
    int (*syscall)(struct mm_struct *, unsigned long, size_t) = (void *)DO_MUNMAP;

    return syscall(mm, start, len);
}


int handle_mm_fault(struct mm_struct *mm, struct vm_area_struct *vma,
                    unsigned long address, unsigned int flags)
{
    int (*syscall)(struct mm_struct *, struct vm_area_struct *,
                    unsigned long, unsigned int) = (void *)HANDLE_MM_FAULT;

    return syscall(mm, vma, address, flags);
}


int groups_search(const struct group_info *group_info, kgid_t grp)
{
    int (*syscall)(const struct group_info *, kgid_t) = (void *)GROUPS_SEARCH;
    return syscall(group_info, grp);
}


int __mm_populate(unsigned long start, unsigned long len, int ignore_errors)
{
    int (*syscall)(unsigned long, unsigned long, int) = (void *)__MM_POPULATE;

    return syscall(start, len, ignore_errors);
}


static inline int compat_on_sig_stack(struct task_struct *tsk, unsigned long sp)
{
    return sp >= tsk->sas_ss_sp &&
        sp - tsk->sas_ss_sp < tsk->sas_ss_size;
}


static inline int compat_sas_ss_flags(struct task_struct *tsk, unsigned long sp)
{
    return (tsk->sas_ss_size == 0 ? SS_DISABLE
        : compat_on_sig_stack(tsk, sp) ? SS_ONSTACK : 0);
}


int compat_sigaltstack(struct task_struct *tsk, const stack_t *ss, stack_t *oss, unsigned long sp)
{
    int ret = 0;
    stack_t tmp;
    mm_segment_t fs = get_fs();

    set_fs(KERNEL_DS);
    memset(&tmp, 0, sizeof(tmp));
    tmp.ss_sp = (void __user*)tsk->sas_ss_sp;
    tmp.ss_size = tsk->sas_ss_size;
    tmp.ss_flags = compat_sas_ss_flags(tsk, sp);

    if (ss) {
        void *ss_sp = ss->ss_sp;
        size_t ss_size = ss->ss_size;
        int ss_flags = ss->ss_flags;

        if (compat_on_sig_stack(tsk, sp)) {
            ret = -EPERM;
            goto out;
        }
        if (ss->ss_flags != SS_DISABLE && ss->ss_flags != SS_ONSTACK && ss->ss_flags != 0) {
            ret = -EINVAL;
            goto out;
        }
        if (ss_flags == SS_DISABLE) {
            ss_size = 0;
            ss_sp = NULL;
        } else {
            if (ss->ss_size < MINSIGSTKSZ) {
                ret = -ENOMEM;
                goto out;
            }
        }
        tsk->sas_ss_sp = (unsigned long)ss_sp;
        tsk->sas_ss_size = ss_size;
    }
    if (oss) {
        oss->ss_sp = tmp.ss_sp;
        oss->ss_size = tmp.ss_size;
        oss->ss_flags = tmp.ss_flags;
    }
out:
    set_fs(fs);
    return ret;
}
