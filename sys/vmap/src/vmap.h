#ifndef _VMAP_H
#define _VMAP_H

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include "path.h"
#include "file.h"
#include "util.h"

#ifdef DEBUG
#define LOG_VMAP_READ
#define LOG_VMAP_SETXATTR
#endif

#include "log_vmap.h"

#define VMAP_ATTR_SIZE			"SIZE"

#endif
