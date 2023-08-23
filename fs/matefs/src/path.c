#include "path.h"

char matefs_user[128] = {0};
char matefs_path[MATEFS_PATH_MAX] = {0};

int matefs_path_can_migrate(const char *path)
{
    if (!strcmp(path, MATEFS_PATH_FST)
        || !strcmp(path, MATEFS_PATH_LST)
        || !strcmp(path, MATEFS_PATH_NXT)
        || !strcmp(path, MATEFS_PATH_BAC)
        || !strcmp(path, MATEFS_PATH_RND)
        || !strcmp(path, MATEFS_PATH_LOC))
        return 1;
    else
        return 0;
}


int matefs_path_join(const char *src1, const char *src2, char *dest)
{
    int len1 = strlen(src1);
    int len2 = strlen(src2);

    if (len1 + len2 >= MATEFS_PATH_MAX) {
        log_err("invalid path");
        return -EINVAL;
    }

    sprintf(dest, "%s%s", src1, src2);
    return 0;
}


int matefs_path_get(const char *src, char *dest)
{
    return matefs_path_join(matefs_path, src, dest);
}
