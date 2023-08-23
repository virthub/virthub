#ifndef _MAP_H
#define _MAP_H

#include <linux/klnk.h>
#include "ckpt.h"
#include "util.h"

#ifdef SHOW_MAP
#define LOG_MAP
#define LOG_MAP_ATTACH
#define LOG_MAP_SETATTR
#endif

#include "log/log_map.h"

#define MAP_FLAG      O_RDONLY | O_LARGEFILE
#define MAP_MODE      S_IRWXU | S_IRWXG | S_IRWXO

#define MAP_PATH_MAX  256
#define MAP_ATTR_SIZE "SIZE"

int ckpt_map_attach(char *node, pid_t gpid, unsigned long area, size_t size, int prot, int flags);
int ckpt_map_setattr(char *node, struct task_struct *tsk, unsigned long area, char *name, void *value, size_t size);
int ckpt_map(char *node, struct task_struct *tsk, unsigned long area, unsigned long address, void *buf, size_t len);

#endif
