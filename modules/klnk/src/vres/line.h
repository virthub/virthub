#ifndef _LINE_H
#define _LINE_H

#include <vres.h>
#include <string.h>
#include <stdlib.h>

#define VRES_LINE_SHIFT      6
#define VRES_LINE_SIZE       (PAGE_SIZE >> VRES_LINE_SHIFT)
#define VRES_LINE_MAX        (1 << VRES_LINE_SHIFT)
#define VRES_LINE_NR_SAMPLES 16

typedef int32_t vres_digest_t;

typedef struct vres_line {
    vres_digest_t digest;
    int num;
    char buf[VRES_LINE_SIZE];
} vres_line_t;

vres_digest_t vres_line_get_digest(char *buf);

#endif
