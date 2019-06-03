#ifndef _LOG_MEMBER_H
#define _LOG_MEMBER_H

#include "log.h"

#ifdef LOG_MEMBER_GET
#define log_member_get log_resource_ln
#else
#define log_member_get(...) do {} while (0)
#endif

#ifdef LOG_MEMBER_ADD
#define log_member_add log_resource_ln
#else
#define log_member_add(...) do {} while (0)
#endif

#ifdef LOG_MEMBER_SAVE
#define log_member_save log_resource_ln
#else
#define log_member_save(...) do {} while (0)
#endif

#ifdef LOG_MEMBER_UPDATE
#define log_member_update log_resource_ln
#else
#define log_member_update(...) do {} while (0)
#endif

#ifdef LOG_MEMBER_CREATE
#define log_member_create log_resource_ln
#else
#define log_member_create(...) do {} while (0)
#endif

#ifdef LOG_MEMBER_SEND_REQ
#define log_member_send_req log_resource_info
#else
#define log_member_send_req(...) do {} while (0)
#endif

#endif
