#ifndef _PATH_H
#define _PATH_H

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

#define MATEFS_PATH_FST "/.^"
#define MATEFS_PATH_LST "/.$"
#define MATEFS_PATH_NXT "/.+"
#define MATEFS_PATH_BAC "/.-"
#define MATEFS_PATH_RND "/.?"
#define MATEFS_PATH_LOC "/.@"

#define MATEFS_PATH_MAX 1024

extern char matefs_path[];
extern char matefs_user[];

int matefs_path_can_migrate(const char *path);
int matefs_path_get(const char *src, char *dest);
int matefs_path_join(const char *src1, const char *src2, char *dest);

#endif
