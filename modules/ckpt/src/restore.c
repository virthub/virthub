/* 
 * This work originally started from Berkeley Lab Checkpoint/Restart (BLCR):
 * https://ftg.lbl.gov/projects/CheckpointRestart/
 */

#include "restore.h"

int ckpt_restore_pages(ckpt_vma_t *area, ckpt_desc_t desc)
{
    int count = 0;
    unsigned long addr = 0;

    for (;;) {
        if (ckpt_read(desc, &addr, sizeof(addr)) != sizeof(addr)) {
            log_err("failed to get address");
            return -EIO;
        }
        if (!addr)
            break;
        if ((addr < area->start) || (addr >= area->end)) {
            log_err("invalid address");
            return -EINVAL;
        }
        if (ckpt_uread(desc, (void *)addr, PAGE_SIZE) != PAGE_SIZE) {
            log_err("failed to get page");
            return -EIO;
        }
        count++;
    }
    if (ckpt_get_vma_prot(area) & PROT_EXEC)
        flush_icache_range(area->start, area->end);

    return count;
}


int ckpt_restore_vma(char *node, pid_t gpid, ckpt_desc_t desc)
{
    int ret;
    int nr_pages = -1;
    char *path = NULL;
    ckpt_vma_t area;

    if (ckpt_read(desc, &area, sizeof(ckpt_vma_t)) != sizeof(ckpt_vma_t)) {
        log_err("failed to get area");
        return -EIO;
    }
    if (area.arch) {
        void *vdso;

        current->mm->context.vdso = (void *)(~0UL);
        ret = arch_setup_additional_pages(NULL, 0);
        if (ret < 0) {
            log_err("failed to setup additional pages");
            return ret;
        }
        vdso = current->mm->context.vdso;
        if ((vdso != (void *)(0UL)) && (vdso != (void *)area.start)) {
            ret = ckpt_vma_remap((unsigned long)vdso, &area);
            if (ret < 0) {
                log_err("failed to remap vma");
                return ret;
            }
        }
        current->mm->context.vdso = (void *)area.start;
    } else {
        int sz = area.sz;

        if (sz) {
            path = (char *)kmalloc(CKPT_PATH_MAX, GFP_KERNEL);
            if (!path) {
                log_err("no memory");
                return -ENOMEM;
            }
            if (ckpt_read(desc, path, sz) != sz) {
                log_err("failed to get path");
                kfree(path);
                return -EIO;
            }
        }
        if (ckpt_is_mapped_area(&area)) {
            unsigned long prot = ckpt_get_vma_prot(&area);
            unsigned long flags = ckpt_get_vma_flags(&area);

            ret = ckpt_map_attach(node, gpid, area.start, area.end - area.start, prot, flags);
            if (ret) {
                log_err("failed to attach");
                return ret;
            }
        } else {
            ret = ckpt_vma_map(path, &area);
            if (ret) {
                log_err("failed to map vma");
                return ret;
            }
            nr_pages = ckpt_restore_pages(&area, desc);
            if (nr_pages < 0) {
                log_err("failed to restore pages");
                return nr_pages;
            }
        }
        sys_mprotect(area.start, area.end - area.start, ckpt_get_vma_prot(&area));
    }
    log_restore_vma(area.start, area.end, path, nr_pages, ckpt_debug_checksum(current->mm, NULL, area.start));
    return 0;
}


int ckpt_restore_mem(char *node, pid_t gpid, ckpt_desc_t desc)
{
    int i;
    int ret;
    ckpt_mem_t memory;
    struct mm_struct *mm = current->mm;

    log_restore_mem("restore mem");
    if (ckpt_read(desc, &memory, sizeof(ckpt_mem_t)) != sizeof(ckpt_mem_t)) {
        log_err("failed to get mem");
        return -EIO;
    }
    while (mm->mmap) {
        struct vm_area_struct *vma = mm->mmap;

        if (vma->vm_start >= TASK_SIZE)
            break;
        ret = do_munmap(mm, vma->vm_start, vma->vm_end - vma->vm_start, NULL);
        if (ret) {
            log_err("failed to unmap");
            return ret;
        }
    }
    down_write(&mm->mmap_sem);
    mm->task_size = TASK_SIZE;
    arch_pick_mmap_layout(mm);
    mm->mmap_base = memory.mmap_base;
    mm->start_code = memory.start_code;
    mm->end_code = memory.end_code;
    mm->start_data = memory.start_data;
    mm->end_data = memory.end_data;
    mm->start_brk = memory.start_brk;
    mm->brk = memory.brk;
    mm->start_stack = memory.start_stack;
    mm->arg_start = memory.arg_start;
    mm->arg_end = memory.arg_end;
    mm->env_start = memory.env_start;
    mm->env_end = memory.env_end;
    up_write(&mm->mmap_sem);
    for (i = 0; i < memory.map_count; i++) {
        ret = ckpt_restore_vma(node, gpid, desc);
        if (ret) {
            log_err("failed to restore vma");
            return ret;
        }
    }
    log_restore_pos(desc);
    return 0;
}


int ckpt_restore_regs(ckpt_desc_t desc)
{
    int i;
    ckpt_cpu_t regs;
    unsigned long fs;
    unsigned long gs;
    struct pt_regs *regs = task_pt_regs(current);

    if (ckpt_read(desc, &cpu, sizeof(ckpt_cpu_t)) != sizeof(ckpt_cpu_t)) {
        log_err("failed to restore cpu");
        return -EIO;
    }
    log_restore_regs("restore regs");
    fs = regs.pt_regs.fs;
    gs = regs.pt_regs.gs;
    regs.pt_regs.fs = 0;
    regs.pt_regs.gs = 0;
    regs.pt_regs.cs = __USER_CS;
    regs.pt_regs.ds = __USER_DS;
    regs.pt_regs.es = __USER_DS;
    regs.pt_regs.ss = __USER_DS;
    regs.pt_regs.flags = 0x200 | (cpu.regs.flags & 0xff);
    *regs = regs.regs;
    /*
    log_restore_regs(" |->restore tls");
    for (i = 0; i < GDT_ENTRY_TLS_ENTRIES; i++) {
        if ((regs.tls_array[i].b & 0x00207000) != 0x00007000) {
            regs.tls_array[i].a = 0;
            regs.tls_array[i].b = 0;
        }
        current->thread.tls_array[i] = cpu.tls_array[i];
    }
    load_TLS(&current->thread, get_cpu());
    put_cpu();
    */
    i = fs >> 3;
    if ((i < GDT_ENTRY_TLS_MIN) || (i > GDT_ENTRY_TLS_MAX) || ((fs & 7) != 3))
        fs = 0;
    i = gs >> 3;
    if ((i < GDT_ENTRY_TLS_MIN) || (i > GDT_ENTRY_TLS_MAX) || ((gs & 7) != 3))
        gs = 0;
    regs->fs = fs;
    regs->gs = gs;
    log_regs(regs);
    current_thread_info()->sysenter_return = cpu.sysenter_return;
    log_restore_pos(desc);
    return 0;
}


int ckpt_restore_fpu(ckpt_desc_t desc)
{
    int ret;
    int flag;

    log_restore_fpu("restore fpu");
    if (ckpt_read(desc, &flag, sizeof(int)) != sizeof(int)) {
        log_err("failed to get file");
        return -EIO;
    }
    kernel_fpu_begin();
    clear_used_math();
    if (flag) {
        if (!ckpt_get_i387(current)) {
            init_fpu(current);
            if (!ckpt_get_i387(current)) {
                log_err("failed to get i387");
                return -EFAULT;
            }
        }
        if (ckpt_read(desc, ckpt_get_i387(current), xstate_size) != xstate_size) {
            log_err("failed to get i387");
            return -EFAULT;
        }
        ret = ckpt_check_fpu_state();
        if (ret) {
            log_err("failed to restore i387");
            return ret;
        }
        set_used_math();
    }
    kernel_fpu_end();
    log_restore_pos(desc);
    return 0;
}


int ckpt_restore_file(ckpt_desc_t desc)
{
    int sz;
    int ret = 0;
    ckpt_file_t file;
    struct file *filp;
    char *path = kmalloc(CKPT_PATH_MAX, GFP_KERNEL);

    if (!path) {
        log_err("no memory");
        return -ENOMEM;
    }
    if (ckpt_read(desc, &file, sizeof(ckpt_file_t)) != sizeof(ckpt_file_t)) {
        log_err("failed to get file");
        ret = -EIO;
        goto out;
    }
    if (!file.sz) {
        log_err("invalid file name");
        ret = -EINVAL;
        goto out;
    }
    sz = file.sz;
    if (ckpt_read(desc, path, sz) != sz) {
        log_err("failed to get path");
        ret = -EIO;
        goto out;
    }
    filp = ckpt_reopen_file(&file, path);
    if (IS_ERR(filp)) {
        log_err("failed to reopen file");
        ret = -EINVAL;
        goto out;
    }
    log_restore_file("%s", path);
out:
    kfree(path);
    return ret;
}


int ckpt_restore_files(ckpt_desc_t desc)
{
    int mode;
    int ret = 0;
    ckpt_files_struct_t fs;

    log_restore_files("restore files");
    if (ckpt_read(desc, &fs, sizeof(ckpt_files_struct_t)) != sizeof(ckpt_files_struct_t)) {
        log_err("failed to read");
        return -EIO;
    }
    do {
        if (ckpt_read(desc, &mode, sizeof(int)) != sizeof(int)) {
            log_err("failed to read");
            return -EIO;
        }
        switch (mode) {
        case S_IFREG:
        case S_IFDIR:
            ret = ckpt_restore_file(desc);
            break;
        default:
            break;
        }
        if (ret) {
            log_err("failed to restore file");
            return ret;
        }
    } while (mode);
    log_restore_pos(desc);
    return ret;
}


int ckpt_restore_sigpending(struct sigpending *sigpending, int shared, ckpt_desc_t desc)
{
    int i;
    ckpt_sigqueue_t queue;
    ckpt_sigpending_t pending;
    struct siginfo *info = &queue.info;

    if (ckpt_read(desc, &pending, sizeof(ckpt_sigpending_t)) != sizeof(ckpt_sigpending_t)) {
        log_err("failed to get sigpending");
        return -EIO;
    }
    for (i = 0; i < pending.count; i++) {
        if (ckpt_read(desc, &queue, sizeof(ckpt_sigqueue_t)) != sizeof(ckpt_sigqueue_t)) {
            log_err("failed to get sigqueue");
            return -EIO;
        }
        if (shared) {
            read_lock((rwlock_t *)TASKLIST_LOCK);
            group_send_sig_info(info->si_signo, info, current);
            read_unlock((rwlock_t *)TASKLIST_LOCK);
        } else
            send_sig_info(info->si_signo, info, current);
    }
    spin_lock_irq(&current->sighand->siglock);
    sigorsets(&sigpending->signal, &sigpending->signal, &pending.signal);
    recalc_sigpending();
    spin_unlock_irq(&current->sighand->siglock);
    return 0;
}


int ckpt_restore_signals(ckpt_desc_t desc)
{
    int i;
    int ret;
    stack_t sigstack;
    sigset_t sigblocked;

    log_restore_signals("restore signals");
    if (ckpt_read(desc, &sigstack, sizeof(stack_t)) != sizeof(stack_t)) {
        log_err("failed to get sigstack");
        return -EIO;
    }
    ret = compat_sigaltstack(current, &sigstack, NULL, 0);
    if (ret) {
        log_err("failed to restore sigstack (ret=%d)", ret);
        return ret;
    }
    log_restore_signals(" |->restore sigblocked");
    if (ckpt_read(desc, &sigblocked, sizeof(sigset_t)) != sizeof(sigset_t)) {
        log_err("failed to restore sigstack");
        return -EIO;
    }
    sigdelsetmask(&sigblocked, sigmask(SIGKILL) | sigmask(SIGSTOP));
    spin_lock_irq(&current->sighand->siglock);
    current->blocked = sigblocked;
    recalc_sigpending();
    spin_unlock_irq(&current->sighand->siglock);
    log_restore_signals(" |->restore sigpending");
    ret = ckpt_restore_sigpending(&current->pending, 0, desc);
    if (ret) {
        log_err("failed to restore pending");
        return ret;
    }
    ret = ckpt_restore_sigpending(&current->signal->shared_pending, 1, desc);
    if (ret) {
        log_err("failed to restore shared_pending");
        return ret;
    }
    log_restore_signals(" |->restore sigaction");
    for (i = 0; i < _NSIG; i++) {
        struct k_sigaction sigaction;

        if (ckpt_read(desc, &sigaction, sizeof(struct k_sigaction)) != sizeof(struct k_sigaction)) {
            log_err("failed to get sigaction");
            return -EIO;
        }
        if ((i != SIGKILL - 1) && (i != SIGSTOP - 1)) {
            ret = do_sigaction(i + 1, &sigaction, 0);
            if (ret) {
                log_err("failed to restore sigaction (ret=%d)", ret);
                return ret;
            }
        }
    }
    log_restore_pos(desc);
    return 0;
}


int ckpt_restore_cred(ckpt_desc_t desc)
{
    int ret = 0;
    ckpt_cred_t cred;
    gid_t *groups = NULL;
    const struct cred *curr_cred = current->cred;

    log_restore_cred("restore cred info");
    if (ckpt_read(desc, &cred, sizeof(ckpt_cred_t)) != sizeof(ckpt_cred_t)) {
        log_err("failed to get cred");
        return -EIO;
    }
    if (cred.ngroups > NGROUPS_MAX) {
        log_err("ngroups (%d) > NGROUPS_MAX", cred.ngroups);
        return -EINVAL;
    }
    if (cred.ngroups > 0) {
        int i;
        size_t size = cred.ngroups * sizeof(gid_t);
        struct group_info *gi = curr_cred->group_info;

        groups = vmalloc(size);
        if (!groups) {
            log_err("no memory");
            return -ENOMEM;
        }
        if (ckpt_read(desc, groups, size) != size) {
            log_err("failed to get groups");
            ret = -EIO;
            goto out;
        }
        for (i = 0; i < cred.ngroups; i++) {
            if (!groups_search(gi, groups[i])) {
                mm_segment_t fs = get_fs();
                set_fs(KERNEL_DS);
                ret = sys_setgroups(cred.ngroups, groups);
                set_fs(fs);
                if (ret < 0) {
                    log_err("failed to set groups");
                    goto out;
                }
                break;
            }
        }
    }
    current->gpid = cred.gpid;
    ret = sys_setresgid(cred.gid, cred.egid, cred.sgid);
    if (ret < 0) {
        log_err("failed to restore gid");
        goto out;
    }
    ret = sys_setresuid(cred.uid, cred.euid, cred.suid);
    if (ret < 0) {
        log_err("failed to restore uid");
        goto out;
    }
    if ((curr_cred->euid == curr_cred->uid) && (curr_cred->egid == curr_cred->gid))
        set_dumpable(current->mm, 1);
    else
        set_dumpable(current->mm, *compat_suid_dumpable);
    log_restore_pos(desc);
out:
    if (groups)
        vfree(groups);
    return ret;
}


int ckpt_restore_misc(ckpt_desc_t desc)
{
    int ret;
    ckpt_misc_t misc;
    mm_segment_t fs;

    log_restore_misc("restore misc info");
    if (ckpt_read(desc, &misc, sizeof(ckpt_misc_t)) != sizeof(ckpt_misc_t)) {
        log_err("failed to read misc info");
        return -EIO;
    }
    set_personality(misc.personality);
    current->clear_child_tid = misc.clear_child_tid;
    fs = get_fs();
    set_fs(KERNEL_DS);
    ret = sys_prctl(PR_SET_NAME, (unsigned long)misc.comm, 0, 0, 0);
    set_fs(fs);
    log_restore_pos(desc);
    return ret;
}


int ckpt_restore_fs(ckpt_desc_t desc)
{
    int length;
    int ret = 0;
    char *path = (char *)kmalloc(CKPT_PATH_MAX, GFP_KERNEL);

    log_restore_fs("restore fs");
    if (!path) {
        log_err("no memory");
        return -ENOMEM;
    }
    if (ckpt_read(desc, &length, sizeof(int)) != sizeof(int)) {
        log_err("invalid cwd");
        ret = -EIO;
        goto out;
    }
    if (length <= 0) {
        log_err("invalid cwd");
        ret = -EINVAL;
        goto out;
    }
    if (ckpt_read(desc, path, length) != length) {
        log_err("invalid cwd");
        ret = -EIO;
        goto out;
    }
    log_restore_fs(" |->set cwd (%s)", path);
    ret = ckpt_set_cwd(path);
    if (ret) {
        log_err("failed to set cwd");
        goto out;
    }
    if (ckpt_read(desc, &length, sizeof(int)) != sizeof(int)) {
        log_err("invalid root");
        ret = -EIO;
        goto out;
    }
    if (length <= 0) {
        log_err("invalid root");
        ret = -EINVAL;
        goto out;
    }
    if (ckpt_read(desc, path, length) != length) {
        log_err("invalid root");
        ret = -EIO;
        goto out;
    }
    log_restore_fs(" |-> set root (%s)", path);
    ret = ckpt_set_root(path);
    if (ret) {
        log_err("failed to set root");
        goto out;
    }
    log_restore_pos(desc);
out:
    kfree(path);
    return ret;
}


int ckpt_restore_task(char *node, pid_t gpid, ckpt_desc_t desc)
{
    int ret;

    ret = ckpt_restore_fs(desc);
    if(ret)
        return ret;
    ret = ckpt_restore_cred(desc);
    if (ret)
        return ret;
    ret = ckpt_restore_misc(desc);
    if (ret)
        return ret;
    ret = ckpt_restore_regs(desc);
    if (ret)
        return ret;
    ret = ckpt_restore_fpu(desc);
    if (ret)
        return ret;
    ret = ckpt_restore_files(desc);
    if (ret)
        return ret;
    ret = ckpt_restore_signals(desc);
    if (ret)
        return ret;
    return ckpt_restore_mem(node, gpid, desc);
}
