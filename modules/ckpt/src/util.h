#ifndef _UTIL_H
#define _UTIL_H

#include <linux/klnk.h>
#include "common.h"
#include "ckpt.h"
#include "log.h"

#define ckpt_get_pos(desc) (desc)->f_pos
#define ckpt_get_i387(tsk) (tsk)->thread.fpu.state
#define ckpt_is_arch_vma(vma) ((vma)->vm_start == (long)(vma)->vm_mm->context.vdso)

#ifdef CKPT_MAP
#define ckpt_is_mapped_area(area) (!(area)->sz && !((area)->flags & VM_GROWSDOWN))
#else
#define ckpt_is_mapped_area(area) 0
#endif

int ckpt_set_cwd(char *name);
int ckpt_set_root(char *name);
int ckpt_check_fpu_state(void);
int ckpt_check_file_mode(struct file *file);
int ckpt_can_map(struct vm_area_struct *vma);
int ckpt_can_dump(struct vm_area_struct *vma);
int ckpt_vma_map(char *path, ckpt_vma_t *area);
int ckpt_vma_remap(unsigned long from, ckpt_vma_t *area);
int ckpt_get_cwd(struct task_struct *tsk, char *buf, unsigned long size);
int ckpt_get_root(struct task_struct *tsk, char *buf, unsigned long size);

struct file *ckpt_reopen_file(ckpt_file_t *file, const char *path);
void *ckpt_check_addr(struct mm_struct *mm, unsigned long address);
void *ckpt_get_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long address);

unsigned long ckpt_get_vma_prot(ckpt_vma_t *area);
unsigned long ckpt_get_vma_flags(ckpt_vma_t *area);

#endif
