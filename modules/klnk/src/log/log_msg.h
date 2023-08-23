#ifndef _LOG_MSG_H
#define _LOG_MSG_H

#ifdef LOG_MSG
#define log_msg log_resource_ln
#else
#define log_msg(...) do {} while (0)
#endif

#endif
