#ifndef _PATH_H
#define _PATH_H

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

#define LBFS_PATH_FST "/.^"
#define LBFS_PATH_LST "/.$"
#define LBFS_PATH_NXT "/.+"
#define LBFS_PATH_BAC "/.-"
#define LBFS_PATH_RND "/.?"
#define LBFS_PATH_LOC "/.@"

#define LBFS_PATH_MAX 1024

extern char lbfs_path[];
extern char lbfs_user[];

int lbfs_path_can_migrate(const char *path);
int lbfs_path_get(const char *src, char *dest);
int lbfs_path_join(const char *src1, const char *src2, char *dest);

#endif
