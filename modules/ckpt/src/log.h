#ifndef _LOG_H
#define _LOG_H

#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs_struct.h>
#include <linux/hugetlb_inline.h>
#include <linux/highmem.h>
#include <linux/mman.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/prctl.h>
#include <linux/syscalls.h>

#ifdef ERROR
#define log_err(fmt, ...) printk("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define log_err(...) do {} while (0)
#endif

#ifdef DEBUG
#define log_debug(fmt, ...) printk("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define log_debug(...) do {} while (0)
#endif

#define log_pos(desc) log_debug("@%d", (int)ckpt_get_pos(desc))

void log_regs(struct pt_regs *regs);
void log_task_regs(struct task_struct *tsk);
void log_stack(struct mm_struct *mm, struct pt_regs *regs, loff_t len);

#endif
