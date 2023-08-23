/* 
 * This work originally started from Berkeley Lab Checkpoint/Restart (BLCR):
 * https://ftg.lbl.gov/projects/CheckpointRestart/
 */

#include "io.h"
#include "sys.h"
#include "util.h"

void *ckpt_get_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long address)
{
    int cnt = 0;
    pte_t *ptep;
    spinlock_t *ptl;
    void *ptr = NULL;
    struct page *page = NULL;
retry:
    ptep = klnk_get_pte(mm, address, &ptl);
    if (ptep) {
        pte_t pte = *ptep;

        if (pte_present(pte)) {
            page = pte_page(pte);
            if (!pte_dump(pte))
                ptr = page_address(page);
        } else if (cnt <= 1) {
            cnt++;
            if (pte_none(pte))
                cnt++;
        }
        klnk_put_pte(ptep, ptl);
    }
    if (!page && (cnt <= 1)) {
        if (!ckpt_can_map(vma)) {
            int ret = handle_mm_fault(mm, vma, address, 0);

            if (ret & VM_FAULT_ERROR) {
                log_err("failed to get page");
                return NULL;
            }
            goto retry;
        }
    }
    return ptr;
}


int ckpt_check_fpu_state(void)
{
    int ret = 0;
    ckpt_i387_t *i387 = ckpt_get_i387(current);

    /* Invalid FPU states can blow us out of the water so we will do
     * the restore here in such a way that we trap the fault if the
     * restore fails.  This modeled after get_user and put_user. */
    if (cpu_has_fxsr) {
          asm volatile
          ("1: fxrstor %1               \n"
          "2:                          \n"
         ".section .fixup,\"ax\"      \n"
         "3:  movl %2, %0             \n"
         "    jmp 2b                  \n"
             ".previous                   \n"
         ".section __ex_table,\"a\"   \n"
         "    .align 4                \n"
         "    .long 1b, 3b            \n"
         ".previous                   \n"
         : "+r"(ret)
         : "m" (i387->fxsave), "i"(-EFAULT));
    } else {
        asm volatile
        ("1: frstor %1                \n"
         "2:                          \n"
          ".section .fixup,\"ax\"      \n"
         "3:  movl %2, %0             \n"
          "    jmp 2b                  \n"
         ".previous                   \n"
         ".section __ex_table,\"a\"   \n"
         "    .align 4                \n"
         "    .long 1b, 3b            \n"
         ".previous                   \n"
         : "+r"(ret)
         : "m" (i387->fsave), "i"(-EFAULT));
    }
    return ret;
}


static inline int ckpt_is_unlinked_file(struct file *file)
{
    if (!file)
        return 0;
    else {
        struct dentry *dentry = file->f_dentry;

        return ((!IS_ROOT(dentry) && d_unhashed(dentry))
                || !dentry->d_inode->i_nlink
                || (dentry->d_flags & DCACHE_NFSFS_RENAMED));
    }
}


int ckpt_can_dump(struct vm_area_struct *vma)
{
    return !vma->vm_file || (!is_vm_hugetlb_page(vma) && (!ckpt_is_unlinked_file(vma->vm_file) || !(vma->vm_flags & VM_SHARED)));
}


int ckpt_can_map(struct vm_area_struct *vma)
{
#ifdef MAP
    struct file *file = vma->vm_file;

    if (!file) {
        if (!((vma)->vm_flags & VM_GROWSDOWN))
            return 1;
    } else
        return klnk_is_map(file);
#endif
    return 0;
}


int ckpt_check_file_mode(struct file *file)
{
    if (file && file->f_dentry) {
        int mode;
        struct dentry *dentry = file->f_dentry;

        mode = dentry->d_inode->i_mode & S_IFMT;
        if ((S_IFREG == mode) || (S_IFDIR == mode))
            return mode;
    }
    return 0;
}


struct file *ckpt_reopen_file(ckpt_file_t *file, const char *path)
{
    struct file *filp = ERR_PTR(-ENOENT);
    int fd = get_unused_fd_flags(file->flags);

    if (fd >= 0) {
        int ret;

        filp = filp_open(path, file->flags, file->mode);
        if (IS_ERR(filp)) {
            put_unused_fd(fd);
            return filp;
        }
        fd_install(fd, filp);
        ret = sys_lseek(fd, file->pos, SEEK_SET);
        if (ret < 0) {
            filp_close(filp, NULL);
            put_unused_fd(fd);
            return ERR_PTR(ret);
        }
    }
    return filp;
}


static inline int ckpt_get_file_flags(ckpt_vma_t *area)
{
    unsigned long flags = area->flags;

    if ((flags & VM_MAYSHARE) && (flags & VM_MAYWRITE))
        return (flags & (VM_MAYREAD | VM_MAYEXEC)) ? O_RDWR : O_WRONLY;
    else
        return O_RDONLY;
}


unsigned long ckpt_get_vma_flags(ckpt_vma_t *area)
{
    unsigned long ret;

    if (area->arch) {
        ret = MAP_PRIVATE | MAP_ANONYMOUS;
    } else {
        unsigned long flags = area->flags;

        ret = MAP_FIXED | ((flags & VM_MAYSHARE) ? MAP_SHARED : MAP_PRIVATE);
        if (flags & VM_GROWSDOWN)
            ret |= MAP_GROWSDOWN;
        if (flags & VM_DENYWRITE)
            ret |= MAP_DENYWRITE;
    }
    return ret;
}


unsigned long ckpt_get_vma_prot(ckpt_vma_t *area)
{
    unsigned long ret = 0;

    if (area->arch) {
        ret = PROT_NONE;
    } else {
        unsigned long flags = area->flags;

        if (!area->sz) {
            if (flags & VM_READ)
                ret |= PROT_READ;
            if (flags & VM_EXEC)
                ret |= PROT_EXEC;
            ret |= PROT_WRITE;
        } else {
            if (flags & VM_MAYSHARE) {
                if (flags & VM_MAYWRITE)
                    ret |= PROT_WRITE;
                if (flags & VM_MAYREAD)
                    ret |= PROT_READ;
                if (flags & VM_MAYEXEC)
                    ret |= PROT_EXEC;
            } else
                ret = PROT_WRITE | PROT_READ | PROT_EXEC;
        }
    }
    return ret;
}


int ckpt_vma_map(char *path, ckpt_vma_t *area)
{
    int ret;
    unsigned long populate;
    struct file *filp = NULL;
    int prot = ckpt_get_vma_prot(area);
    int flags = ckpt_get_vma_flags(area);
    unsigned long pgoff = path ? area->pgoff : 0;

    if (path) {
        filp = filp_open(path, ckpt_get_file_flags(area), 0);
        if (IS_ERR(filp)) {
            log_err("failed to open %s", path);
            return PTR_ERR(filp);
        }
    }
    down_write(&current->mm->mmap_sem);
    ret = do_mmap_pgoff(filp, area->start, area->end - area->start, prot, flags, pgoff, &populate);
    up_write(&current->mm->mmap_sem);
    if (populate)
        mm_populate(ret, populate);
    if (filp)
        fput(filp);
    if (ret != area->start) {
        log_err("[%lx, %lx], failed (%d)", area->start, area->end, ret);
        return -EINVAL;
    }
    return 0;
}


int ckpt_vma_remap(unsigned long from, ckpt_vma_t *area)
{
    unsigned long addr;
    unsigned long to = area->start;
    unsigned long diff = from > to ? from - to : to - from;
    unsigned long len = area->end - area->start;

    if (diff < len) {
        unsigned long populate;
        unsigned long ret = do_mmap_pgoff(NULL, 0, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, 0, &populate);

        if (populate)
            mm_populate(ret, populate);
        if (IS_ERR_VALUE(ret)) {
            log_err("failed to map (%d)", (int)ret);
            return ret;
        }
        addr = sys_mremap(from, len, len, MREMAP_FIXED | MREMAP_MAYMOVE, ret);
        if (addr != ret) {
            log_err("failed to remap (%d)", (int)addr);
            return -EINVAL;
        }
        from = addr;
    }
    addr = sys_mremap(from, len, len, MREMAP_FIXED | MREMAP_MAYMOVE, to);
    if (addr != to) {
        log_err("failed to remap, %lx=>%lx (%d)", from, to, (int)addr);
        return -EINVAL;
    }
    return 0;
}


int ckpt_get_cwd(struct task_struct *tsk, char *buf, unsigned long size)
{
    char *cwd;
    char *page = (char *)__get_free_page(GFP_USER);
    struct path path;

    if (!page) {
        log_err("no memory");
        return -ENOMEM;
    }
    get_fs_pwd(tsk->fs, &path);
    cwd = d_path(&path, page, PAGE_SIZE);
    if (IS_ERR(cwd)) {
        log_err("failed to get cwd");
        return PTR_ERR(cwd);
    }
    memcpy(buf, cwd, PAGE_SIZE + page - cwd);
    path_put(&path);
    free_page((unsigned long)page);
    return 0;
}


int ckpt_get_root(struct task_struct *tsk, char *buf, unsigned long size)
{
    char *root;
    char *page = (char *)__get_free_page(GFP_USER);
    struct path path;

    if (!page) {
        log_err("no memory");
        return -ENOMEM;
    }
    get_fs_root(tsk->fs, &path);
    root = d_path(&path, page, PAGE_SIZE);
    if (IS_ERR(root)) {
        log_err("failed to get root");
        return PTR_ERR(root);
    }
    memcpy(buf, root, PAGE_SIZE + page - root);
    path_put(&path);
    free_page((unsigned long)page);
    return 0;
}


int ckpt_set_cwd(char *name)
{
    int ret;
    struct path path;

    ret = kern_path(name, LOOKUP_DIRECTORY, &path);
    if (ret) {
        log_err("invalid path, %s", name);
        return ret;
    }
    ret = inode_permission(path.dentry->d_inode, MAY_EXEC | MAY_CHDIR);
    if (!ret)
        set_fs_pwd(current->fs, &path);
    path_put(&path);
    return ret;
}


int ckpt_set_root(char *name)
{
    int ret;
    struct path path;

    ret = kern_path(name, LOOKUP_DIRECTORY, &path);
    if (ret) {
        log_err("invalid path, %s", name);
        return ret;
    }
    ret = inode_permission(path.dentry->d_inode, MAY_EXEC | MAY_CHDIR);
    if (!ret)
        set_fs_root(current->fs, &path);
    path_put(&path);
    return ret;
}
