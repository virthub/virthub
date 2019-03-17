#ifndef _PATH_H
#define _PATH_H

#include <vres.h>

#define VRES_PATH_MAX       64
#define VRES_PATH_MIG       "@"
#define VRES_PATH_LOC       "l"
#define VRES_PATH_DATA      "d"
#define VRES_PATH_CACHE     "c"
#define VRES_PATH_EVENT     "e"
#define VRES_PATH_STATE     "s"
#define VRES_PATH_ACTION    "a"
#define VRES_PATH_HOLDER    "h"
#define VRES_PATH_MEMBER    "m"
#define VRES_PATH_UPDATE    "u"
#define VRES_PATH_CHECKIN   "i"
#define VRES_PATH_CHECKOUT  "o"
#define VRES_PATH_PRIORITY  "p"
#define VRES_PATH_SEPERATOR "/"

#define vres_path_append_loc(path) strcat(path, VRES_PATH_LOC)
#define vres_path_append_mig(path) strcat(path, VRES_PATH_MIG)
#define vres_path_append_data(path) strcat(path, VRES_PATH_DATA)
#define vres_path_append_cache(path) strcat(path, VRES_PATH_CACHE)
#define vres_path_append_event(path) strcat(path, VRES_PATH_EVENT)
#define vres_path_append_state(path) strcat(path, VRES_PATH_STATE)
#define vres_path_append_holder(path) strcat(path, VRES_PATH_HOLDER)
#define vres_path_append_member(path) strcat(path, VRES_PATH_MEMBER)
#define vres_path_append_action(path) strcat(path, VRES_PATH_ACTION)
#define vres_path_append_update(path) strcat(path, VRES_PATH_UPDATE)
#define vres_path_append_checkin(path) strcat(path, VRES_PATH_CHECKIN)
#define vres_path_append_checkout(path) strcat(path, VRES_PATH_CHECKOUT)
#define vres_path_append_priority(path) strcat(path, VRES_PATH_PRIORITY)

#define vres_path_append_owner(path, owner) sprintf(path + strlen(path), "%lx", (unsigned long)owner)   //1st level
#define vres_path_append_cls(path, cls) sprintf(path + strlen(path), "%lx", (unsigned long)cls)         //2nd level
#define vres_path_append_key(path, key) sprintf(path + strlen(path), "%lx", (unsigned long)key)         //3rd level
#define vres_path_append_queue(path, queue) sprintf(path + strlen(path), "%lx.", (unsigned long)queue)  //4th level
#define vres_path_append_index(path, index) sprintf(path + strlen(path), "%ld", (unsigned long)index)

void vres_get_path(vres_t *resource, char *path);
void vres_get_mig_path(vres_t *resource, char *path);
void vres_get_cls_path(vres_t *resource, char *path);
void vres_get_key_path(vres_t *resource, char *path);
void vres_get_loc_path(vres_t *resource, char *path);
void vres_get_data_path(vres_t *resource, char *path);
void vres_get_root_path(vres_t *resource, char *path);
void vres_get_cache_path(vres_t *resource, char *path);
void vres_get_event_path(vres_t *resource, char *path);
void vres_get_state_path(vres_t *resource, char *path);
void vres_get_action_path(vres_t *resource, char *path);
void vres_get_member_path(vres_t *resource, char *path);
void vres_get_holder_path(vres_t *resource, char *path);
void vres_get_record_path(vres_t *resource, char *path);
void vres_get_update_path(vres_t *resource, char *path);
void vres_get_priority_path(vres_t *resource, char *path);

int vres_is_key_path(char *path);
int vres_get_resource(const char *path, vres_t *resource);
int vres_path_join(const char *p1, const char *p2, char *path);
int vres_parse(const char *path, vres_t *resource, unsigned long *addr, size_t *inlen, size_t *outlen);

#endif
