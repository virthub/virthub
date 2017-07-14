#ifndef _PATH_H
#define _PATH_H

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

#define MIFS_PATH_FST	"/.^"
#define MIFS_PATH_LST	"/.$"
#define MIFS_PATH_NXT	"/.+"
#define MIFS_PATH_BAC	"/.-"
#define MIFS_PATH_RND	"/.?"
#define MIFS_PATH_LOC	"/.@"

#define MIFS_PATH_MAX	1024

extern char mifs_path[];

int mifs_path_can_migrate(const char *path);
int mifs_path_get(const char *src, char *dest);
int mifs_path_join(const char *src1, const char *src2, char *dest);

#endif
