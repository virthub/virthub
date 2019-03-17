#ifndef _DEBUG_H
#define _DEBUG_H

#include "log.h"

#ifdef ERROR
#define klnk_log_err log_err
#else
#define klnk_log_err(...) do {} while (0)
#endif

#ifdef DEBUG
#define klnk_log log_func
#else
#define klnk_log(...) do {} while (0)
#endif

#endif
