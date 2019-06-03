#ifndef _METADATA_H
#define _METADATA_H

#include <hiredis/hiredis.h>
#include <stdbool.h>
#include <vres.h>
#include "log.h"

void vres_metadata_init();
int vres_metadata_count(char *path);
int vres_metadata_remove(char *path);
bool vres_metadata_exists(char *path);
unsigned long vres_metadata_max(char *path);
int vres_metadata_read(char *path, char *buf, int len);
int vres_metadata_write(char *path, char *buf, int len);
int vres_metadata_create(char *path, char *buf, int len);

#endif
