/* path.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "path.h"

char mifs_path[MIFS_PATH_MAX] = {0};

int mifs_path_can_migrate(const char *path)
{
	if (!strcmp(path, MIFS_PATH_FST)
	    || !strcmp(path, MIFS_PATH_LST)
	    || !strcmp(path, MIFS_PATH_NXT)
	    || !strcmp(path, MIFS_PATH_BAC)
	    || !strcmp(path, MIFS_PATH_RND)
	    || !strcmp(path, MIFS_PATH_LOC))
		return 1;
	else
		return 0;
}


int mifs_path_join(const char *src1, const char *src2, char *dest)
{
	int len1 = strlen(src1);
	int len2 = strlen(src2);

	if (len1 + len2 >= MIFS_PATH_MAX) {
		log_err("invalid path");
		return -EINVAL;
	}

	sprintf(dest, "%s%s", src1, src2);
	return 0;
}


int mifs_path_get(const char *src, char *dest)
{
	return mifs_path_join(mifs_path, src, dest);
}
