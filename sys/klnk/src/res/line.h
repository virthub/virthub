#ifndef _LINE_H
#define _LINE_H

#include <vres.h>
#include <string.h>
#include <stdlib.h>

#ifdef NOMANAGER
#define VRES_LINE_SHIFT				4
#else
#define VRES_LINE_SHIFT				0
#endif

#define VRES_LINE_SIZE				(PAGE_SIZE >> VRES_LINE_SHIFT)
#define VRES_LINE_MAX					(1 << VRES_LINE_SHIFT)
#define VRES_LINE_NR_SAMPLES	16

#ifdef SHOW_LING
#define LOG_NR_LINES					1
#define LOG_NR_BYTES					(sizeof(unsigned long) * 20)

#define LOG_LINE_DIFF
#define LOG_LINES

#ifdef SHOW_MORE
#define LOF_LINE_NUM
#define LOG_LINE_CONTENT
#endif
#endif

#include "log_line.h"

typedef int32_t vres_digest_t;

typedef struct vres_line {
	vres_digest_t digest;
	int num;
	char buf[VRES_LINE_SIZE];
} vres_line_t;

vres_digest_t vres_line_get_digest(char *buf);

#endif
