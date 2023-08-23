#include "mon.h"

pthread_mutex_t mon_lock;
unsigned long mon_shm_req_len; // length of pending queue
unsigned long mon_shm_req_cnt;
unsigned long mon_shm_req_delay;
struct list_head mon_shm_req_head;

void vres_mon_init()
{
    mon_shm_req_cnt = 0;
    mon_shm_req_len = 0;
    mon_shm_req_delay = 0;
    INIT_LIST_HEAD(&mon_shm_req_head);
    pthread_mutex_init(&mon_lock, NULL);
}


inline unsigned long vres_mon_get_pos(vres_t *res)
{
    if (res->cls == VRES_CLS_SHM)
        return vres_get_chunk_start(res);
    else
        return 0;
}


void vres_mon_get_id(vres_t *res, vres_mon_id_t *id)
{
    id->pos = vres_mon_get_pos(res);
    id->cls = res->cls;
    id->key = res->key;
}


vres_mon_entry_t *vres_mon_lookup(vres_t *res, vres_mon_id_t *id)
{
    vres_mon_entry_t *entry;

    if (res->cls == VRES_CLS_SHM) {
        list_for_each_entry(entry, &mon_shm_req_head, list)
            if ((entry->id.cls == id->cls) && (entry->id.key == id->key) && (entry->id.pos == id->pos))
                return entry;
    } else
        log_resource_err(res, "invalid type");
    return NULL;
}


void vres_mon_alloc(vres_t *res, vres_mon_id_t *id)
{
    vres_mon_entry_t *ent = malloc(sizeof(vres_mon_entry_t));

    assert(ent);
    ent->id = *id;
    ent->t = vres_get_time();
    if (res->cls == VRES_CLS_SHM) {
        mon_shm_req_len++;
        assert(mon_shm_req_len <= VRES_MON_REQ_MAX);
        list_add_tail(&ent->list, &mon_shm_req_head);
    } else
        log_resource_err(res, "invalid type");
}


void vres_mon_output(vres_t *res)
{
    FILE *filp = fopen(mon_name, "w");

    if (!filp) {
        log_resource_err(res, "cannot open %s", mon_name);
        return;
    }
    if (res->cls == VRES_CLS_SHM)
        fprintf(filp, "shm_req_cnt=%lu, shm_req_delay=%lu\n", mon_shm_req_cnt, mon_shm_req_delay);
    else
        log_resource_err(res, "invalid type");
    fclose(filp);
}


void vres_mon_release(vres_t *res, vres_mon_entry_t *ent)
{
    vres_time_t t = vres_get_time();
    int delay = t - ent->t;

    assert(mon_shm_req_len > 0);
    assert(!list_empty(&mon_shm_req_head));
    if (delay > 0) {
        if (res->cls == VRES_CLS_SHM) {
            mon_shm_req_cnt++;
            mon_shm_req_delay += delay;
            log_mon_release(res, "start=%lld, now=%lld, delay=%d", ent->t, t, delay);
        } else
            log_resource_err(res, "invalid type");
        vres_mon_output(res);
    }
    mon_shm_req_len--;
    list_del(&ent->list);
    free(ent);
}


void vres_mon_req_get(vres_t *res)
{
    vres_mon_id_t id;
    vres_mon_entry_t *ent;

    log_mon_req_get(res);
    vres_mon_get_id(res, &id);
    pthread_mutex_lock(&mon_lock);
    ent = vres_mon_lookup(res, &id);
    if (!ent)
        vres_mon_alloc(res, &id);
    else {
        log_resource_warning(res, "detect a monitored request");
        ent->t = vres_get_time();
    }
    pthread_mutex_unlock(&mon_lock);
}


void vres_mon_req_put(vres_t *res)
{
    vres_mon_id_t id;
    vres_mon_entry_t *ent;

    vres_mon_get_id(res, &id);
    pthread_mutex_lock(&mon_lock);
    ent = vres_mon_lookup(res, &id);
    if (ent)
        vres_mon_release(res, ent);
    else
        log_resource_warning(res, "no request found");
    pthread_mutex_unlock(&mon_lock);
    log_mon_req_put(res, "shm_req_cnt=%d, shm_req_delay=%d", mon_shm_req_cnt, mon_shm_req_delay);
}
