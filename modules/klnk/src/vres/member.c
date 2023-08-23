#include "member.h"

vres_member_req_queue_t vres_member_reqs;

int vres_member_send_req(vres_req_t *req)
{
    int i;
    int ret = 0;
    void *entry;
    static bool init = false;
    vres_members_t *members;
    vres_t *resource = &req->resource;
    vres_id_t src = vres_get_id(resource);

    log_member_send_req(resource, ">-- send_req@start --<");
    vres_rwlock_wrlock(resource);
    entry = vres_member_get(resource);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    members = vres_member_check(entry);
    for (i = 0; i < members->total; i++) {
        vres_id_t id = members->list[i].id;

        if ((id != resource->owner) && (id != src)) {
            ret = klnk_io_sync(resource, req->buf, req->length, NULL, 0, id);
            if (ret) {
                log_resource_err(resource, "failed to send to %d (ret=%s)", id, log_get_err(ret));
                break;
            }
        }
    }
    vres_member_put(entry);
    vres_rwlock_unlock(resource);
    log_member_send_req(resource, ">-- send_req@end --<");
    return ret;
}


void *vres_member_do_notify(void *arg)
{
    for (;;) {
        int cnt;
        bool lock = true;

        pthread_cond_wait(&vres_member_reqs.cond, &vres_member_reqs.mutex);
        cnt = vres_member_reqs.cnt;
        while (cnt > 0) {
            vres_req_t req;
            vres_member_req_t *p;

            if (!lock)
                pthread_mutex_lock(&vres_member_reqs.mutex);
            p = list_entry(vres_member_reqs.head.next, vres_member_req_t, list);
            req = p->req;
            list_del(&p->list);
            free(p);
            vres_member_reqs.cnt--;
            cnt = vres_member_reqs.cnt;
            pthread_mutex_unlock(&vres_member_reqs.mutex);
            if (vres_member_send_req(&req) < 0) {
                log_err("failed to notify members");
                exit(-1);
            }
            lock = false;
        }
    }
    return NULL;
}


int vres_member_notify(vres_req_t *req)
{
    int ret = 0;

    pthread_mutex_lock(&vres_member_reqs.mutex);
    if (vres_member_reqs.cnt < VRES_MEMBER_REQ_MAX) {
        vres_member_req_t *p = malloc(sizeof(vres_member_req_t));

        if (!p) {
            log_resource_err(&req->resource, "no memory");
            ret = -ENOMEM;
        } else {
            p->req = *req;
            list_add_tail(&p->list, &vres_member_reqs.head);
            vres_member_reqs.cnt++;
            pthread_cond_signal(&vres_member_reqs.cond);
        }
    } else {
        log_resource_err(&req->resource, "too much requests");
        ret = -EINVAL;
    }
    pthread_mutex_unlock(&vres_member_reqs.mutex);
    return ret;
}


void vres_member_init()
{
    int ret;
    pthread_t tid;
    pthread_attr_t attr;

    vres_member_reqs.cnt = 0;
    INIT_LIST_HEAD(&vres_member_reqs.head);
    pthread_cond_init(&vres_member_reqs.cond, NULL);
    pthread_mutex_init(&vres_member_reqs.mutex, NULL);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    ret = pthread_create(&tid, &attr, vres_member_do_notify, NULL);
    pthread_attr_destroy(&attr);
    if (ret) {
        log_err("failed to initialize");
        exit(-1);
    }
}


bool vres_member_is_public(vres_t *resource)
{
    return resource->cls == VRES_CLS_SHM;
}


vres_members_t *vres_member_check(void *entry)
{
    vres_file_entry_t *ent = (vres_file_entry_t *)entry;

    return vres_file_get_desc(ent, vres_members_t);
}


void *vres_member_get(vres_t *resource)
{
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    vres_get_member_path(resource, path);
    entry = vres_file_get_entry(path, 0, FILE_RDWR);
    log_member_get(resource);
    return entry;
}


void *vres_member_get_entry(vres_t *resource)
{
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    vres_get_member_path(resource, path);
    entry = vres_file_get_entry(path, 0, FILE_RDWR);
    if (!entry)
        entry = vres_file_get_entry(path, sizeof(vres_members_t), FILE_RDWR | FILE_CREAT);
    log_member_get(resource);
    return entry;
}


void vres_member_put(void *entry)
{
    vres_file_put_entry((vres_file_entry_t *)entry);
}


int vres_member_create(vres_t *resource)
{
    void *entry;

    log_member_create(resource);
    entry = vres_member_get_entry(resource);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    vres_member_put(entry);
    return 0;
}


int vres_member_append(void *entry, vres_member_t *member)
{
    return vres_file_append(entry, member, sizeof(vres_member_t));
}


int vres_member_get_size(void *entry)
{
    vres_file_t *filp = ((vres_file_entry_t *)entry)->file;

    return vres_file_size(filp);
}


int vres_member_add(vres_t *resource)
{
    int i;
    int total;
    int ret = 0;
    void *entry;
    vres_desc_t peer;
    vres_member_t member;
    vres_members_t *members;
    vres_id_t id = vres_get_id(resource);

    log_member_add(resource);
    if (vres_get_peer(id, &peer)) {
        log_resource_err(resource, "invalid peer %d", id);
        return -EINVAL;
    }
    entry = vres_member_get_entry(resource);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    members = vres_member_check(entry);
    total = members->total;
    for (i = 0; i < total; i++) {
        if (members->list[i].id == id) {
            log_resource_warning(resource, "member %d already exists", id);
            ret = -EEXIST;
            goto out;
        }
    }
    if ((total >= VRES_MEMBER_MAX) || (total < 0)) {
        log_resource_err(resource, "out of range");
        ret = -EINVAL;
        goto out;
    }
    total++;
    members->total = total;
    memset(&member, 0, sizeof(vres_member_t));
    member.id = id;
    ret = vres_member_append(entry, &member);
out:
    vres_member_put(entry);
    return ret ? ret : total;
}


int vres_member_delete(vres_t *resource)
{
    int i;
    int ret = 0;
    void *entry;
    vres_members_t *members;
    vres_member_t *member = NULL;
    vres_id_t id = vres_get_id(resource);

    entry = vres_member_get(resource);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    members = vres_member_check(entry);
    if (members->total < 1) {
        log_resource_err(resource, "no entry");
        ret = -ENOENT;
        goto out;
    }
    for (i = 0; i < members->total; i++) {
        if (members->list[i].id == id) {
            member = &members->list[i];
            break;
        }
    }
    if (member) {
        member->count = -1;
        members->total--;
    }
out:
    vres_member_put(entry);
    return ret;
}


int vres_member_list(vres_t *resource, vres_id_t *list)
{
    int i;
    void *entry;
    vres_members_t *members;

    entry = vres_member_get(resource);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    members = vres_member_check(entry);
    for (i = 0; i < members->total; i++)
        list[i] = members->list[i].id;
    vres_member_put(entry);
    return 0;
}


int vres_member_get_pos(vres_t *resource, vres_id_t id)
{
    int i;
    void *entry;
    int ret = -ENOENT;
    vres_members_t *members;

    vres_rwlock_rdlock(resource);
    entry = vres_member_get(resource);
    if (!entry) {
        log_resource_err(resource, "no entry");
        goto out;
    }
    members = vres_member_check(entry);
    for (i = 0; i < members->total; i++) {
        if (members->list[i].id == id) {
            ret = i;
            break;
        }
    }
    vres_member_put(entry);
    if (ret < 0)
        log_resource_debug(resource, "cannot find id %d from %d members", id, members->total);
out:
    vres_rwlock_unlock(resource);
    return ret;
}


int vres_member_save(vres_t *resource, vres_id_t *list, int total)
{
    int i;
    int ret = 0;
    void *entry;
    vres_member_t member;
    vres_members_t *members;

    log_member_save(resource);
    if ((total > VRES_MEMBER_MAX) || (total <= 0)) {
        log_resource_err(resource, "invalid member list");
        return -EINVAL;
    }
    entry = vres_member_get_entry(resource);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    members = vres_member_check(entry);
    if (vres_member_get_size(entry) != sizeof(vres_members_t)) {
        log_resource_err(resource, "invalid size, vres_member_get_size(entry)=%d, sizeof(vres_members_t)=%ld", vres_member_get_size(entry), sizeof(vres_members_t));
        ret = -EINVAL;
        goto out;
    }
    members->total = total;
    memset(&member, 0, sizeof(vres_member_t));
    for (i = 0; i < total; i++) {
        member.id = list[i];
        ret = vres_member_append(entry, &member);
        if (ret)
            break;
    }
out:
    vres_member_put(entry);
    return ret;
}


bool vres_member_is_active(vres_t *resource)
{
    int i;
    void *entry;
    bool ret = false;
    vres_members_t *members;
    vres_id_t src = vres_get_id(resource);

    entry = vres_member_get(resource);
    if (!entry)
        return ret;
    members = vres_member_check(entry);
    for (i = 0; i < members->total; i++) {
        vres_member_t *member = &members->list[i];

        if (member->id == src) {
            if (member->count >= 0)
                ret = true;
            break;
        }
    }
    vres_member_put(entry);
    return ret;
}


int vres_member_lookup(vres_t *resource, vres_member_t *member, int *active_members)
{
    int i;
    void *entry;
    int ret = -ENOENT;
    vres_members_t *members;
    vres_id_t src = vres_get_id(resource);

    vres_rwlock_rdlock(resource);
    entry = vres_member_get(resource);
    if (!entry)
        goto out;
    members = vres_member_check(entry);
    for (i = 0; i < members->total; i++) {
        if (members->list[i].id == src) {
            memcpy(member, &members->list[i], sizeof(vres_member_t));
            ret = 0;
            break;
        }
    }
    if (!ret && active_members) {
        int count = 0;

        for (i = 0; i < members->total; i++)
            if (members->list[i].count > 0)
                count++;
        *active_members = count;
    }
    vres_member_put(entry);
out:
    vres_rwlock_unlock(resource);
    return ret;
}


int vres_member_update(vres_t *resource, vres_member_t *member)
{
    int i;
    void *entry;
    int ret = -ENOENT;
    vres_members_t *members;
    vres_id_t src = vres_get_id(resource);

    log_member_update(resource);
    vres_rwlock_wrlock(resource);
    entry = vres_member_get(resource);
    if (!entry) {
        log_resource_err(resource, "no entry");
        goto out;
    }
    members = vres_member_check(entry);
    for (i = 0; i < members->total; i++) {
        if (members->list[i].id == src) {
            memcpy(&members->list[i], member, sizeof(vres_member_t));
            ret = 0;
            break;
        }
    }
    vres_member_put(entry);
out:
    vres_rwlock_unlock(resource);
    return ret;
}
