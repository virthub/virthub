#ifndef _IO_H
#define _IO_H

#include "ckpt.h"
#include "util.h"

#define CKPT_RDONLY	O_RDONLY
#define CKPT_WRONLY	(O_WRONLY | O_CREAT | O_TRUNC)
#define CKPT_MODE	(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

typedef struct file *ckpt_desc_t;

int ckpt_close(ckpt_desc_t desc);
int ckpt_read(ckpt_desc_t desc, void *buf, size_t len);
int ckpt_uread(ckpt_desc_t desc, void *buf, size_t len);
int ckpt_write(ckpt_desc_t desc, void *buf, size_t len);
ckpt_desc_t ckpt_open(const char *path, int flags, int mode);

#endif
