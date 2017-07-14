#ifndef _KLNK_H
#define _KLNK_H

#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/hugetlb.h>
#include <linux/unistd.h>
#include <linux/fs_struct.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/vres.h>

//KLNK SETTINGS///////////////////
#define PATH_MIFS			"/mnt/mifs"
#define PATH_VMAP			"/mnt/vmap"
#define PATH_KLNK			"/mnt/klnk"
#define PATH_ROOT			"/vhub/bin"
//////////////////////////////////

#define KLNK_IO_MAX		8192
#define KLNK_PATH_MAX	128

#define KLNK_DEBUG_MSG
#define KLNK_DEBUG_SEM
#define KLNK_DEBUG_SHM

#define klnk_log(fmt, ...) printk("%s@%d: " fmt "\n", __func__, current->gpid, ##__VA_ARGS__)

#ifdef KLNK_DEBUG_MSG
#define klnk_msg_log klnk_log
#else
#define klnk_msg_log(...) do {} while (0)
#endif

#ifdef KLNK_DEBUG_SEM
#define klnk_sem_log klnk_log
#else
#define klnk_sem_log(...) do {} while (0)
#endif

#ifdef KLNK_DEBUG_SHM
#define klnk_shm_log klnk_log
#else
#define klnk_shm_log(...) do {} while (0)
#endif

static inline void *klnk_malloc(size_t size)
{
	int order = get_order(size);
	unsigned long ret = __get_free_pages(GFP_KERNEL, order);

	if (ret) {
		int i;
		int nr_pages = 1 << order;
		unsigned long addr = ret;

		for (i = 0; i < nr_pages; i++) {
			SetPageReserved(virt_to_page(addr));
			addr += PAGE_SIZE;
		}
	}
	return (void *)ret;
}

static inline void klnk_free(void *ptr, size_t size)
{
	unsigned long addr = (unsigned long)ptr;

	if (addr) {
		int i;
		int order = get_order(size);
		int nr_pages = 1 << order;

		for (i = 0; i < nr_pages; i++) {
			ClearPageReserved(virt_to_page(addr));
			addr += PAGE_SIZE;
		}
		free_pages((unsigned long)ptr, order);
	}
}

static inline pid_t klnk_get_gpid(struct task_struct *tsk)
{
	return tsk->gpid;
}

static inline void klnk_set_gpid(struct task_struct *tsk, pid_t id)
{
	tsk->gpid = id;
}

static inline int klnk_is_global(struct task_struct *tsk)
{
	return tsk->gpid > 0;
}

static inline int klnk_can_enter(struct task_struct *tsk)
{
	char path[VRES_PATH_MAX];
	struct path *pwd = &tsk->fs->pwd;
	char *name = d_path(pwd, path, VRES_PATH_MAX);

	if (IS_ERR(name))
		return 0;

	if (!strncmp(name, PATH_ROOT, strlen(PATH_ROOT)))
		return 1;
	else
		return 0;
}

static inline pte_t *klnk_get_pte(struct mm_struct *mm, unsigned long address, spinlock_t **ptlp)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		return NULL;

	pud = pud_offset(pgd, address);
	if (!pud_present(*pud))
		return NULL;

	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd))
		return NULL;

	return pte_offset_map_lock(mm, pmd, address, ptlp);
}

static inline void klnk_put_pte(pte_t *ptep, spinlock_t *ptl)
{
	pte_unmap_unlock(ptep, ptl);
}

int klnk_load_vma(struct vm_area_struct *vma);

static inline int klnk_request(vres_cls_t cls, vres_key_t key,
                               vres_op_t op, vres_id_t id,
			                   vres_val_t val1, vres_val_t val2,
                               char *buf, size_t inlen, size_t outlen)
{
	int ret = 0;
	struct file *filp;
	char path[KLNK_PATH_MAX];
	unsigned long addr = buf ? __pa(buf) : 0;

	sprintf(path, "%s/%lx_%lx_%lx_%lx_%lx_%lx_%lx_%lx_%lx", PATH_KLNK,
		(unsigned long)cls, (unsigned long)key,
		(unsigned long)op, (unsigned long)id,
		(unsigned long)val1,(unsigned long)val2,
		addr, (unsigned long)inlen, (unsigned long)outlen);

	filp = filp_open(path, O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		return -EOK == ret ? 0 : ret;
	}

	filp_close(filp, NULL);
	return ret;
}

static inline int klnk_is_vmap(struct file *file)
{
	char *buf;
	int ret = 0;

	if (!file)
		return ret;

	buf = (char *)kmalloc(KLNK_PATH_MAX, GFP_KERNEL);
	if (buf) {
		char *name;

		name = d_path(&file->f_path, buf, KLNK_PATH_MAX);
		if (!IS_ERR(name)) {
			const int len = strlen(PATH_VMAP);

			if (!strncmp(name, PATH_VMAP, len))
				if ((strlen(name) == len) || (name[len] == '/'))
					ret = 1;
		}

		kfree(buf);
	}

	return ret;
}

static inline int klnk_migrate(struct filename *filename)
{
	int ret;
	char *buf;
	vres_mig_arg_t *arg;
	const char *path = filename->name;
	pid_t gpid = klnk_get_gpid(current);
	size_t buflen = sizeof(vres_mig_arg_t);

	if (strlen(path) >= VRES_PATH_MAX) {
		klnk_log("invalid path");
		return -EINVAL;
	}

	buf = klnk_malloc(buflen);
	if (!buf) {
		klnk_log("no memory");
		return -ENOMEM;
	}

	arg = (vres_mig_arg_t *)buf;
	strcpy(arg->path, path);
	ret = klnk_request(VRES_CLS_TSK, gpid, VRES_OP_MIGRATE, gpid, 0, 0, buf, buflen, 0);
	if (!ret)
		ret = -EINTR;
	else if (-EAGAIN == ret)
		filename->name = PATH_MIFS;
	klnk_free(buf, buflen);
	klnk_log("path=%s", path);
	return ret;
}

static inline struct file *klnk_filp_open(struct filename *filename)
{
	if (klnk_is_global(current))
		return ERR_PTR(klnk_migrate(filename));
	else
		return ERR_PTR(-ENOENT);
}

#endif
