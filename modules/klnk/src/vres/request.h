#ifndef _REQUEST_H
#define _REQUEST_H

#include <vres.h>
#include "member.h"

#ifdef SHOW_REQUEST
#define LOG_REQUEST_JOIN
#endif

#include "log_request.h"

#define VRES_RETRY_INTERVAL 5000000 // usec

typedef struct vres_join_result {
    long retval;
    int total;
    vres_id_t list[0];
} vres_join_result_t;

typedef struct vres_leave_result {
    long retval;
} vres_leave_result_t;

typedef struct vres_cancel_arg {
    vres_op_t op;
} vres_cancel_arg_t;

int vres_request_leave(vres_t *resource);
int vres_request_cancel(vres_arg_t *arg, vres_index_t index);
int vres_request_join(vres_t *resource, vres_join_result_t **result);

#endif
