#include "path.h"
#include "util.h"

static inline unsigned long vres_get_queue(vres_entry_t *entry)
{
    unsigned long queue;
    vres_op_t op = vres_entry_op(entry);

    switch(op) {
    case VRES_OP_MSGRCV:
        queue = 1;
        break;
    case VRES_OP_SHMFAULT:
        queue = vres_entry_chunk(entry);
        break;
    default:
        queue = 0;
    }
    return queue;
}


void vres_get_key_path(vres_t *resource, char *path)
{
    strcpy(path, VRES_PATH_SEPERATOR);
    vres_path_append_cls(path, resource->cls);
    strcat(path, VRES_PATH_SEPERATOR);
    vres_path_append_key(path, resource->key);
}


void vres_get_path(vres_t *resource, char *path)
{
    vres_get_root_path(resource, path);
    vres_get_key_path(resource, path + strlen(path));
    strcat(path, VRES_PATH_SEPERATOR);
}


void vres_get_cls_path(vres_t *resource, char *path)
{
    vres_get_root_path(resource, path);
    strcat(path, VRES_PATH_SEPERATOR);
    vres_path_append_cls(path, resource->cls);
}


void vres_get_loc_path(vres_t *resource, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_loc(path);
}


void vres_get_mig_path(vres_t *resource, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_mig(path);
}


void vres_get_root_path(vres_t *resource, char *path)
{
    strcpy(path, VRES_PATH_SEPERATOR);
    vres_path_append_own(path, resource->owner);
}


void vres_get_event_path(vres_t *resource, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_event(path);
}


void vres_get_state_path(vres_t *resource, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_state(path);
}


void vres_get_action_path(vres_t *resource, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_action(path);
}


void vres_get_member_path(vres_t *resource, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_member(path);
}


void vres_get_record_path(vres_t *resource, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_que(path, vres_get_queue(resource->entry));
}


void vres_get_region_path(vres_t *resource, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_que(path, vres_get_region(resource));
    vres_path_append_data(path);
}


void vres_get_temp_path(vres_t *resource, char *path)
{
    vres_get_record_path(resource, path);
    vres_path_append_temp(path);
}


void vres_get_update_path(vres_t *resource, int chunk_id, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_que(path, vres_get_slice(resource) * VRES_CHUNK_MAX + chunk_id);
    vres_path_append_update(path);
}


void vres_get_checker_path(vres_t *resource, int slice_id, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_checker(path, slice_id);
    strcat(path, VRES_PATH_SEPERATOR);
}


void vres_get_priority_path(vres_t *resource, int slice_id, char *path)
{
    vres_get_path(resource, path);
    vres_path_append_priority(path, slice_id);
}


int vres_path_join(const char *p1, const char *p2, char *path)
{
    if (strlen(p1) + strlen(p2) + 1 >= VRES_PATH_MAX)
        return -EINVAL;
    sprintf(path, "%s/%s", p1, p2);
    return 0;
}


int vres_get_resource(const char *path, vres_t *resource)
{
    char *end;
    vres_id_t id = 0;
    vres_cls_t cls = 0;
    vres_key_t key = 0;
    const char *start = path;

    memset(resource, 0, sizeof(vres_t));
    if (*start++ != '/')
        return -EINVAL;
    id = (vres_id_t)strtoul(start, &end, 16);
    if (start == end)
        return -EINVAL;
    start = &end[1];
    cls = (vres_cls_t)strtoul(start, &end, 16);
    if (start == end)
        return -EINVAL;
    start = &end[1];
    key = (vres_key_t)strtoul(start, &end, 16);
    if (start == end)
        return -EINVAL;
    resource->cls = cls;
    resource->key = key;
    resource->owner = id;
    vres_set_id(resource, id);
    return 0;
}


bool vres_is_key_path(char *path)
{
    char *end;
    const char *start = path;

    if (*start++ != '/')
        return false;
    strtoul(start, &end, 16);
    if (start == end)
        return false;
    start = &end[1];
    strtoul(start, &end, 16);
    if (start == end)
        return false;
    start = &end[1];
    strtoul(start, &end, 16);
    if (start == end)
        return false;
    if ((end[0] == '/') && (end[1] == '\0'))
        return true;
    else
        return false;
}


int vres_mkdir(vres_t *resource)
{
    int ret;
    char path[VRES_PATH_MAX] = {0};

    vres_get_root_path(resource, path);
    if (!vres_file_is_dir(path)) {
        ret = vres_file_mkdir(path);
        if (ret) {
            log_resource_warning(resource, "failed to create root directory");
            return ret;
        }
    }
    vres_get_cls_path(resource, path);
    if (!vres_file_is_dir(path)) {
        ret = vres_file_mkdir(path);
        if (ret) {
            log_resource_warning(resource, "failed to create class directory");
            return ret;
        }
    }
    vres_get_path(resource, path);
    if (!vres_file_is_dir(path)) {
        ret = vres_file_mkdir(path);
        if (ret) {
            log_resource_warning(resource, "failed to create resource directory");
            return ret;
        }
    }
    if (VRES_CLS_SHM == resource->cls) {
        unsigned long i;

        for (i = 0; i < VRES_SLICE_MAX; i++) {
            vres_get_checker_path(resource, i, path);
            if (!vres_file_is_dir(path)) {
                ret = vres_file_mkdir(path);
                if (ret) {
                    log_resource_warning(resource, "failed to create checker directory");
                    return ret;
                }
            }
        }
    }
    return 0;
}


void vres_clear_path(vres_t *resource)
{
    char path[VRES_PATH_MAX] = {0};

    vres_get_path(resource, path);
    if (vres_file_is_dir(path)) {
        if (!vres_file_is_empty_dir(path))
            return;
        vres_file_rmdir(path);
    }
    vres_get_cls_path(resource, path);
    if (vres_file_is_dir(path)) {
        if (!vres_file_is_empty_dir(path))
            return;
        vres_file_rmdir(path);
    }
    vres_get_root_path(resource, path);
    if (vres_file_is_dir(path)) {
        if (!vres_file_is_empty_dir(path))
            return;
        vres_file_rmdir(path);
    }
}
