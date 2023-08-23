#ifndef _DEBUG_H
#define _DEBUG_H

#include <linux/slab.h>
#include "ckpt.h"
#include "util.h"
#include "log.h"
#include "io.h"

unsigned long ckpt_debug_checksum(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr);

#endif
