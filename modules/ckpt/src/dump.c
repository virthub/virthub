/* 
 * This work originally started from Berkeley Lab Checkpoint/Restart (BLCR):
 * https://ftg.lbl.gov/projects/CheckpointRestart/
 */

#include "dump.h"

int ckpt_dump_pages(char *node, struct task_struct *tsk, struct vm_area_struct *vma, ckpt_desc_t desc)
{
    int ret = 0;
    int count = 0;
    unsigned long addr;
    int map = ckpt_can_map(vma);
    struct mm_struct *mm = tsk->mm;
    size_t size = vma->vm_end - vma->vm_start;

    for (addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
        char *buf;

        buf = ckpt_get_page(mm, vma, addr);
        if (!buf)
            continue;
        if (!map) {
            if (ckpt_write(desc, &addr, sizeof(addr)) != sizeof(addr)) {
                log_err("failed to write");
                ret = -EIO;
                goto out;
            }
            if (ckpt_write(desc, buf, PAGE_SIZE) != PAGE_SIZE) {
                log_err("failed to write");
                ret = -EIO;
                goto out;
            }
        } else {
            ret = ckpt_map(node, tsk, vma->vm_start, addr, buf, PAGE_SIZE);
            if (ret) {
                log_err("failed to update");
                goto out;
            }
        }
        count++;
    }
    if (!map) {
        addr = 0;
        if (ckpt_write(desc, &addr, sizeof(addr)) != sizeof(addr))
            ret = -EIO;
    } else
        ret = ckpt_map_setattr(node, tsk, vma->vm_start, MAP_ATTR_SIZE, &size, sizeof(size));

    if (!ret)
        ret = count;
    else
        log_err("failed to finalize");
out:
    return ret;
}


int ckpt_dump_vma(char *node, struct task_struct *tsk, struct vm_area_struct *vma, ckpt_desc_t desc)
{
    int ret = 0;
    ckpt_vma_t area;
    int nr_pages = 0;
    char *buf = NULL;
    char *name = NULL;
    struct file *file = vma->vm_file;

    if (!ckpt_can_dump(vma))
        return 0;
    area.start = vma->vm_start;
    area.end = vma->vm_end;
    area.flags = vma->vm_flags;
    area.pgoff = vma->vm_pgoff;
    area.arch = ckpt_is_arch_vma(vma);
    area.sz = 0;
    if (!file || ckpt_can_map(vma)) {
        if (ckpt_write(desc, &area, sizeof(ckpt_vma_t)) != sizeof(ckpt_vma_t)) {
            log_err("failed to dump vma");
            return -EIO;
        }
        if (!area.arch) {
            nr_pages = ckpt_dump_pages(node, tsk, vma, desc);
            if (nr_pages < 0) {
                log_err("failed to dump pages");
                return nr_pages;
            }
        }
    } else {
        buf = kmalloc(CKPT_PATH_MAX, GFP_KERNEL);
        if (!buf) {
            log_err("no memory");
            return -ENOMEM;
        }
        name = d_path(&file->f_path, buf, CKPT_PATH_MAX);
        if (IS_ERR(name)) {
            log_err("invalid path");
            ret = PTR_ERR(name);
            goto out;
        }
        area.sz = strlen(name) + 1;
        if (ckpt_write(desc, &area, sizeof(ckpt_vma_t)) != sizeof(ckpt_vma_t)) {
            log_err("failed to dump vma");
            ret = -EIO;
            goto out;
        }
        if (ckpt_write(desc, name, area.sz) != area.sz) {
            log_err("failed to dump path");
            ret = -EIO;
            goto out;
        }
        nr_pages = ckpt_dump_pages(node, tsk, vma, desc);
        if (nr_pages < 0) {
            log_err("failed to dump pages");
            ret = nr_pages;
            goto out;
        }
    }
    log_dump_vma(vma->vm_start, vma->vm_end, name, nr_pages, ckpt_debug_checksum(tsk->mm, vma, vma->vm_start));
out:
    if (buf)
        kfree(buf);
    return ret;
}


int ckpt_dump_mem(char *node, struct task_struct *tsk, ckpt_desc_t desc)
{
    int ret = 0;
    struct mm_struct *mm = tsk->mm;
    struct vm_area_struct *vma;
    ckpt_mem_t memory;

    log_dump_mem("save mem");
    down_read(&mm->mmap_sem);
    memory.start_code = mm->start_code;
    memory.end_code = mm->end_code;
    memory.start_data = mm->start_data;
    memory.end_data = mm->end_data;
    memory.start_brk = mm->start_brk;
    memory.brk = mm->brk;
    memory.start_stack = mm->start_stack;
    memory.arg_start = mm->arg_start;
    memory.arg_end = mm->arg_end;
    memory.env_start = mm->env_start;
    memory.env_end = mm->env_end;
    memory.mmap_base = mm->mmap_base;
    memory.map_count = mm->map_count;
    if (ckpt_write(desc, &memory, sizeof(ckpt_mem_t)) != sizeof(ckpt_mem_t)) {
        log_err("failed to dump memory");
        ret = -EIO;
        goto out;
    }
    for (vma = mm->mmap; vma; vma = vma->vm_next) {
        ret = ckpt_dump_vma(node, tsk, vma, desc);
        if (ret) {
            log_err("failed to dump vma");
            goto out;
        }
    }
    log_dump_pos(desc);
out:
    up_read(&mm->mmap_sem);
    return ret;
}


int ckpt_dump_fpu(struct task_struct *tsk, ckpt_desc_t desc)
{
    int ret = 0;
    int flag = !!(tsk->flags & PF_USED_MATH);

    log_dump_fpu("save fpu");
    if (ckpt_write(desc, &flag, sizeof(int)) != sizeof(int)) {
        log_err("failed to dump fpu");
        return -EIO;
    }
    if (flag) {
        kernel_fpu_begin();
        if (ckpt_write(desc, ckpt_get_i387(tsk), xstate_size) != xstate_size) {
            log_err("failed to dump fpu");
            ret = -EIO;
            goto out;
        }
        kernel_fpu_end();
    }
    log_dump_pos(desc);
out:
    return ret;
}


int ckpt_dump_cpu(struct task_struct *tsk, ckpt_desc_t desc)
{
    int i;
    int ret = 0;
    ckpt_cpu_t *cpu;
    struct pt_regs *pt;
    struct pt_regs *regs;
    struct thread_struct *thread = &tsk->thread;

    cpu = kmalloc(sizeof(ckpt_cpu_t), GFP_KERNEL);
    if (!cpu) {
        log_err("no memory");
        return -ENOMEM;
    }
    log_dump_cpu("save registers");
    pt = task_pt_regs(tsk);
    regs = &cpu->regs;
    memcpy(regs, pt, sizeof(struct pt_regs));
    regs->cs &= 0xffff;
    regs->ds &= 0xffff;
    regs->es &= 0xffff;
    regs->ss &= 0xffff;
    regs->fs &= 0xffff;
    regs->gs &= 0xffff;
    log_regs(regs);
    log_dump_cpu("save debugregs");
    for (i = 0; i < HBP_NUM; i++) {
        struct perf_event *bp = thread->ptrace_bps[i];

        cpu->debugregs[i] = bp ? bp->hw.info.address : 0;
    }
    cpu->debugregs[HBP_NUM] = thread->debugreg6;
    cpu->debugregs[HBP_NUM + 1] = thread->ptrace_dr7;
    log_dump_cpu("save tls");
    for (i = 0; i < GDT_ENTRY_TLS_ENTRIES; i++)
        cpu->tls_array[i] = thread->tls_array[i];
    cpu->sysenter_return = task_thread_info(tsk)->sysenter_return;
    if (ckpt_write(desc, cpu, sizeof(ckpt_cpu_t)) != sizeof(ckpt_cpu_t)) {
        log_err("failed to dump cpu");
        ret = -EIO;
    }
    kfree(cpu);
    log_dump_pos(desc);
    return ret;
}


int ckpt_dump_file(int fd, struct file *filp, ckpt_desc_t desc)
{
    int sz;
    char *buf;
    char *name;
    int ret = 0;
    ckpt_file_t file;
    struct dentry *dentry = filp->f_dentry;
    struct inode *inode = dentry->d_inode;

    buf = (char *)kmalloc(CKPT_PATH_MAX, GFP_KERNEL);
    if (!buf) {
        log_err("no memory");
        return -ENOMEM;
    }
    name = d_path(&filp->f_path, buf, CKPT_PATH_MAX);
    if (IS_ERR(name)) {
        log_err("invalid path");
        ret = PTR_ERR(name);
        goto out;
    }
    sz = strlen(name) + 1;
    file.sz = sz;
    file.fd = fd;
    file.pos = filp->f_pos;
    file.flags = filp->f_flags;
    file.mode = inode->i_mode;
    if (ckpt_write(desc, &file, sizeof(ckpt_file_t)) != sizeof(ckpt_file_t)) {
        log_err("failed to dump file");
        ret = -EIO;
        goto out;
    }
    if (ckpt_write(desc, name, sz) != sz) {
        log_err("failed to dump path");
        ret = -EIO;
        goto out;
    }
    log_dump_file("%s", name);
out:
    kfree(buf);
    return ret;
}


int ckpt_dump_files(struct task_struct *tsk, ckpt_desc_t desc)
{
    int fd;
    int mode;
    int ret = 0;
    struct file *filp;
    struct fdtable *fdt;
    ckpt_files_struct_t fs;
    struct files_struct *files = tsk->files;

    log_dump_files("saving files");
    rcu_read_lock();
    fdt = files_fdtable(files);
    fs.max_fds = fdt->max_fds;
    fs.next_fd = files->next_fd;
    if (ckpt_write(desc, &fs, sizeof(ckpt_files_struct_t)) != sizeof(ckpt_files_struct_t)) {
        log_err("failed to write");
        return -EIO;
    }
    for (fd = 0; fd < fdt->max_fds; fd++) {
        filp = fcheck_files(files, fd);
        if (!filp)
            continue;
        get_file(filp);
        mode = ckpt_check_file_mode(filp);
        if (!mode)
            continue;
        if (ckpt_write(desc, &mode, sizeof(int)) != sizeof(int)) {
            log_err("failed to write");
            ret = -EIO;
            goto out;
        }
        switch(mode) {
        case S_IFREG:
        case S_IFDIR:
            ret = ckpt_dump_file(fd, filp, desc);
            break;
        default:
            break;
        }
        fput(filp);
        if (ret) {
            log_err("failed to dump file");
            goto out;
        }
    }
    mode = 0;
    if (ckpt_write(desc, &mode, sizeof(int)) != sizeof(int)) {
        log_err("failed to write");
        ret = -EIO;
        goto out;
    }
    log_dump_pos(desc);
out:
    rcu_read_unlock();
    return ret;
}


int ckpt_dump_sigpending(struct task_struct *tsk, struct sigpending *sigpending, ckpt_desc_t desc)
{
    int ret = 0;
    int count = 0;
    struct list_head *entry;
    ckpt_sigpending_t pending;

    spin_lock_irq(&tsk->sighand->siglock);
    list_for_each(entry, &sigpending->list)
        count++;
    memcpy(&pending.signal, &sigpending->signal, sizeof(sigset_t));
    pending.count = count;
    spin_unlock_irq(&tsk->sighand->siglock);
    if (ckpt_write(desc, &pending, sizeof(ckpt_sigpending_t)) != sizeof(ckpt_sigpending_t)) {
        log_err("failed to dump pending");
        ret = -EIO;
    }
    spin_lock_irq(&tsk->sighand->siglock);
    list_for_each(entry, &sigpending->list) {
        struct siginfo info;
        ckpt_sigqueue_t queue;
        mm_segment_t fs = get_fs();
        struct sigqueue *sq = list_entry(entry, struct sigqueue, list);

        if (!count) {
            spin_lock_irq(&tsk->sighand->siglock);
            log_err("out of range");
            return -EINVAL;
        }
        memset(&queue, 0, sizeof(ckpt_sigqueue_t));
        queue.flags = sq->flags;
        info = sq->info;
        spin_unlock_irq(&tsk->sighand->siglock);
        set_fs(KERNEL_DS);
        ret = copy_siginfo_to_user(&queue.info, &info);
        set_fs(fs);
        if (ret) {
            log_err("failed to copy siginfo");
            return ret;
        }
        if (ckpt_write(desc, &queue, sizeof(ckpt_sigqueue_t)) != sizeof(ckpt_sigqueue_t)) {
            log_err("failed to dump sigqueue");
            return -EIO;
        }
        count--;
        spin_lock_irq(&tsk->sighand->siglock);
    }
    spin_unlock_irq(&tsk->sighand->siglock);
    return ret;
}


int ckpt_dump_signals(struct task_struct *tsk, ckpt_desc_t desc)
{
    int i;
    int ret;
    stack_t sigstack;
    sigset_t sigblocked;
    struct k_sigaction action;

    log_dump_signals("save sigstack");
    ret = compat_sigaltstack(tsk, NULL, &sigstack, 0);
    if (ret) {
        log_err("failed to get sigstack (ret=%d)", ret);
        return ret;
    }
    if (ckpt_write(desc, &sigstack, sizeof(stack_t)) != sizeof(stack_t)) {
        log_err("failed to dump sigstack");
        return -EIO;
    }
    log_dump_signals("save sigblocked");
    spin_lock_irq(&tsk->sighand->siglock);
    memcpy(&sigblocked, &tsk->blocked, sizeof(sigset_t));
    spin_unlock_irq(&tsk->sighand->siglock);
    if (ckpt_write(desc, &sigblocked, sizeof(sigset_t)) != sizeof(sigset_t)) {
        log_err("failed to dump sigblocked");
        return -EIO;
    }
    log_dump_signals("save pending");
    ret = ckpt_dump_sigpending(tsk, &tsk->pending, desc);
    if (ret) {
        log_err("failed to dump pending");
        return ret;
    }
    log_dump_signals("save shared_pending");
    ret = ckpt_dump_sigpending(tsk, &tsk->signal->shared_pending, desc);
    if (ret) {
        log_err("failed to dump shared_pending");
        return ret;
    }
    log_dump_signals("save sigaction");
    for (i = 0; i < _NSIG; i++) {
        spin_lock_irq(&tsk->sighand->siglock);
        memcpy(&action, &tsk->sighand->action[i], sizeof(struct k_sigaction));
        spin_unlock_irq(&tsk->sighand->siglock);
        if (ckpt_write(desc, &action, sizeof(struct k_sigaction)) != sizeof(struct k_sigaction)) {
            log_err("failed to dump sigaction");
            return -EIO;
        }
    }
    log_dump_pos(desc);
    return 0;
}


int ckpt_dump_cred(struct task_struct *tsk, ckpt_desc_t desc)
{
    int ret = 0;
    ckpt_cred_t cred;
    const struct cred *tsk_cred = tsk->cred;
    struct group_info *gi = tsk_cred->group_info;

    log_dump_cred("save cred info");
    cred.gpid = tsk->gpid;
    cred.uid = tsk_cred->uid;
    cred.euid = tsk_cred->euid;
    cred.suid = tsk_cred->suid;
    cred.fsuid = tsk_cred->fsuid;
    cred.gid = tsk_cred->gid;
    cred.egid = tsk_cred->egid;
    cred.sgid = tsk_cred->sgid;
    cred.fsgid = tsk_cred->fsgid;
    cred.ngroups = gi->ngroups;
    if (ckpt_write(desc, &cred, sizeof(ckpt_cred_t)) != sizeof(ckpt_cred_t)) {
        log_err("failed to dump cred info");
        return -EIO;
    }
    if (cred.ngroups) {
        int i;
        size_t size = cred.ngroups * sizeof(gid_t);
        gid_t *groups = vmalloc(size);

        if (!groups) {
            log_err("no memory");
            return -ENOMEM;
        }
        for (i = 0; i < gi->ngroups; i++)
            groups[i] = GROUP_AT(gi, i);
        if (ckpt_write(desc, groups, size) != size) {
            log_err("failed to dump cred groups");
            ret = -EIO;
        }
        vfree(groups);
    }
    log_dump_pos(desc);
    return ret;
}


int ckpt_dump_misc(struct task_struct *tsk, ckpt_desc_t desc)
{
    ckpt_misc_t misc;

    log_dump_misc("save misc info");
    misc.personality = tsk->personality;
    misc.clear_child_tid = tsk->clear_child_tid;
    strncpy(misc.comm, tsk->comm, sizeof(misc.comm));
    if (ckpt_write(desc, &ext, sizeof(ckpt_misc_t)) != sizeof(ckpt_misc_t)) {
        log_err("failed to dump misc info");
        return -EIO;
    }
    log_dump_pos(desc);
    return 0;
}


int ckpt_dump_fs(struct task_struct *tsk, ckpt_desc_t desc)
{
    int len;
    int ret = 0;
    char *path = (char *)kmalloc(CKPT_PATH_MAX, GFP_KERNEL);

    log_dump_fs("save fs");
    if (!path) {
        log_err("no memory");
        return -ENOMEM;
    }
    ret = ckpt_get_cwd(tsk, path, CKPT_PATH_MAX);
    if (ret < 0) {
        log_err("failed to get cwd");
        goto out;
    }
    log_dump_fs("cwd=%s", path);
    len = strlen(path) + 1;
    if (ckpt_write(desc, &len, sizeof(int)) != sizeof(int)) {
        log_err("failed to dump cwd");
        ret = -EIO;
        goto out;
    }
    if (ckpt_write(desc, path, len) != len) {
        log_err("failed to dump cwd");
        ret = -EIO;
        goto out;
    }
    ret = ckpt_get_root(tsk, path, CKPT_PATH_MAX);
    if (ret < 0) {
        log_err("failed to get root");
        goto out;
    }
    log_dump_fs("root=%s", path);
    len = strlen(path) + 1;
    if (ckpt_write(desc, &len, sizeof(int)) != sizeof(int)) {
        log_err("failed to dump root");
        ret = -EIO;
        goto out;
    }
    if (ckpt_write(desc, path, len) != len) {
        log_err("failed to dump root");
        ret = -EIO;
        goto out;
    }
    log_dump_pos(desc);
out:
    kfree(path);
    return ret;
}


int ckpt_dump_task(char *node, struct task_struct *tsk, ckpt_desc_t desc)
{
    int ret;

    ret = ckpt_dump_fs(tsk, desc);
    if (ret)
        return ret;
    ret = ckpt_dump_cred(tsk, desc);
    if (ret)
        return ret;
    ret = ckpt_dump_misc(tsk, desc);
    if (ret)
        return ret;
    ret = ckpt_dump_cpu(tsk, desc);
    if (ret)
        return ret;
    ret = ckpt_dump_fpu(tsk, desc);
    if (ret)
        return ret;
    ret = ckpt_dump_files(tsk, desc);
    if (ret)
        return ret;
    ret = ckpt_dump_signals(tsk, desc);
    if (ret)
        return ret;
    return ckpt_dump_mem(node, tsk, desc);
}
