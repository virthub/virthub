#ifndef _CALL_H
#define _CALL_H

#include <vres.h>
#include <eval.h>
#include <errno.h>
#include <defaults.h>
#include "util.h"
#include "io.h"

#define KLNK_CALL_MAX 64

int klnk_call(vres_arg_t *arg);
int klnk_broadcast(vres_arg_t *arg);

#endif
