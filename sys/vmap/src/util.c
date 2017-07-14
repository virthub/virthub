/* util.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "util.h"

unsigned long vmap_strtoul(const char *str)
{
	char *end;

	return strtoul(str, &end, 16);
}


int vmap_is_addr(const char *name)
{
	if ((name[0] == '0') && (name[1] == 'x'))
		return 1;
	else
		return 0;
}
