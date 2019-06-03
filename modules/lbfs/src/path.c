/* path.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "path.h"

char lbfs_user[1024] = {0};
char lbfs_path[LBFS_PATH_MAX] = {0};

int lbfs_path_can_migrate(const char *path)
{
    if (!strcmp(path, LBFS_PATH_FST)
        || !strcmp(path, LBFS_PATH_LST)
        || !strcmp(path, LBFS_PATH_NXT)
        || !strcmp(path, LBFS_PATH_BAC)
        || !strcmp(path, LBFS_PATH_RND)
        || !strcmp(path, LBFS_PATH_LOC))
        return 1;
    else
        return 0;
}


int lbfs_path_join(const char *src1, const char *src2, char *dest)
{
    int len1 = strlen(src1);
    int len2 = strlen(src2);

    if (len1 + len2 >= LBFS_PATH_MAX) {
        log_err("invalid path");
        return -EINVAL;
    }

    sprintf(dest, "%s%s", src1, src2);
    return 0;
}


int lbfs_path_get(const char *src, char *dest)
{
    return lbfs_path_join(lbfs_path, src, dest);
}
