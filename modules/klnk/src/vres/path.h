#ifndef _PATH_H
#define _PATH_H

#include <vres.h>
#include <stdbool.h>

#define VRES_PATH_MAX       64
#define VRES_PATH_MIG       "@"
#define VRES_PATH_LOC       "l"
#define VRES_PATH_DATA      "d"
#define VRES_PATH_TEMP      "t"
#define VRES_PATH_EVENT     "e"
#define VRES_PATH_STATE     "s"
#define VRES_PATH_ACTION    "a"
#define VRES_PATH_MEMBER    "m"
#define VRES_PATH_UPDATE    "u"
#define VRES_PATH_CHECKER   "c"
#define VRES_PATH_CHECKIN   "i"
#define VRES_PATH_CHECKOUT  "o"
#define VRES_PATH_PRIORITY  "p"
#define VRES_PATH_SEPERATOR "/"

#define vres_path_append_loc(path)      strcat(path, VRES_PATH_LOC)
#define vres_path_append_mig(path)      strcat(path, VRES_PATH_MIG)
#define vres_path_append_data(path)     strcat(path, VRES_PATH_DATA)
#define vres_path_append_temp(path)     strcat(path, VRES_PATH_TEMP)
#define vres_path_append_event(path)    strcat(path, VRES_PATH_EVENT)
#define vres_path_append_state(path)    strcat(path, VRES_PATH_STATE)
#define vres_path_append_member(path)   strcat(path, VRES_PATH_MEMBER)
#define vres_path_append_action(path)   strcat(path, VRES_PATH_ACTION)
#define vres_path_append_update(path)   strcat(path, VRES_PATH_UPDATE)
#define vres_path_append_checkin(path)  strcat(path, VRES_PATH_CHECKIN)
#define vres_path_append_checkout(path) strcat(path, VRES_PATH_CHECKOUT)


#define vres_path_append_num(path, num) sprintf(path + strlen(path), "%lx", (unsigned long)num)
#define vres_path_append_idx(path, idx) sprintf(path + strlen(path), "%ld", (unsigned long)idx)
#define vres_path_append_que(path, que) sprintf(path + strlen(path), "%lx_", (unsigned long)que)

#define vres_path_append_checker(path, slice_id) sprintf(path + strlen(path), "%s_%lx", VRES_PATH_CHECKER, (unsigned long)slice_id)
#define vres_path_append_priority(path, slice_id) sprintf(path + strlen(path), "%s_%lx", VRES_PATH_PRIORITY, (unsigned long)slice_id)

#define vres_path_append_own vres_path_append_num // 1st level
#define vres_path_append_cls vres_path_append_num // 2nd level
#define vres_path_append_key vres_path_append_num // 3rd level

void vres_clear_path(vres_t *resource);
void vres_get_path(vres_t *resource, char *path);
void vres_get_mig_path(vres_t *resource, char *path);
void vres_get_cls_path(vres_t *resource, char *path);
void vres_get_key_path(vres_t *resource, char *path);
void vres_get_loc_path(vres_t *resource, char *path);
void vres_get_root_path(vres_t *resource, char *path);
void vres_get_temp_path(vres_t *resource, char *path);
void vres_get_event_path(vres_t *resource, char *path);
void vres_get_state_path(vres_t *resource, char *path);
void vres_get_region_path(vres_t *resource, char *path);
void vres_get_action_path(vres_t *resource, char *path);
void vres_get_member_path(vres_t *resource, char *path);
void vres_get_record_path(vres_t *resource, char *path);

void vres_get_update_path(vres_t *resource, int chunk_id, char *path);
void vres_get_checker_path(vres_t *resource, int slice_id, char *path);
void vres_get_priority_path(vres_t *resource, int slice_id, char *path);

int vres_mkdir(vres_t *resource);
int vres_get_resource(const char *path, vres_t *resource);
int vres_path_join(const char *p1, const char *p2, char *path);

bool vres_is_key_path(char *path);

#endif
