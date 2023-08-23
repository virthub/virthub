#ifndef _LOG_DMGR_H
#define _LOG_DMGR_H

#ifdef LOG_DMGR_FORWARD
#define log_dmgr_forward log_resource_info
#else
#define log_dmgr_forward(...) do {} while (0)
#endif

#ifdef LOG_DMGR_CHANGE_OWNER
#define log_dmgr_change_owner log_resource_info
#else
#define log_dmgr_change_owner(...) do {} while (0)
#endif

#ifdef LOG_DMGR_UPDATE_OWNER
#define log_dmgr_update_owner log_resource_info
#else
#define log_dmgr_update_owner(...) do {} while (0)
#endif

#endif
