#ifndef _UTIL_H
#define _UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include "log.h"

#define PAGE_SIZE  4096
#define PAGE_SHIFT 12

#define vmap_strip(val) ((val) >> PAGE_SHIFT)
#define vmap_page_align(addr) (((addr) >> PAGE_SHIFT) << PAGE_SHIFT)

int vmap_is_addr(const char *name);
unsigned long vmap_strtoul(const char *str);

#endif
