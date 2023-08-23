#ifndef _METADATA_H
#define _METADATA_H

#include <hiredis/hiredis.h>
#include <stdbool.h>
#include <pthread.h>
#include <vres.h>
#include "util.h"

#ifdef SHOW_METADATA
#define LOG_METADATA_READ
#endif

#include "log_metadata.h"

#define METADATA_RETRY_MAX  3
#define METADATA_RETRY_INTV 5000 // usec

void vres_metadata_init();
int vres_metadata_count(char *path);
int vres_metadata_remove(char *path);
bool vres_metadata_exists(char *path);
unsigned long vres_metadata_max(char *path);
int vres_metadata_read(char *path, char *buf, int len);
int vres_metadata_write(char *path, char *buf, int len);
bool vres_metadata_create(char *path, char *buf, int len);

#endif
