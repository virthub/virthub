#ifndef _CALL_H
#define _CALL_H

#include <vres.h>
#include <eval.h>
#include <errno.h>
#include "util.h"
#include "io.h"

int klnk_call(vres_arg_t *arg);
int klnk_call_broadcast(vres_arg_t *arg);

#endif
