#ifndef _HANDLER_H
#define _HANDLER_H

#include "net.h"
#include "resource.h"

#define LOG_KLNK_HANDLER

#ifdef LOG_KLNK_HANDLER
#define log_klnk_handler log_resource_info
#else
#define log_klnk_handler(...) do  {} while (0)
#endif

int klnk_handler_create(void *ptr);

#endif
