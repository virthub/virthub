#include "record.h"

int vres_record_check(char *path, vres_index_t index, vres_rec_hdr_t *hdr)
{
    int ret = 0;
    vres_file_t *filp;
    char name[VRES_PATH_MAX];

    strcpy(name, path);
    vres_path_append_idx(name, index);
    filp = vres_file_open(name, "r");
    if (!filp)
        return -ENOENT;
    if (vres_file_read(hdr, sizeof(vres_rec_hdr_t), 1, filp) != 1)
        ret = -EIO;
    vres_file_close(filp);
    return ret;
}


int vres_record_get(char *path, vres_index_t index, vres_record_t *record)
{
    vres_rec_hdr_t *hdr;
    vres_file_entry_t *entry;
    char name[VRES_PATH_MAX];

    strcpy(name, path);
    vres_path_append_idx(name, index);
    entry = vres_file_get_entry(name, 0, FILE_RDONLY);
    if (!entry)
        return -ENOENT;
    hdr = vres_file_get_desc(entry, vres_rec_hdr_t);
    record->entry = entry;
    record->req = (vres_req_t *)&hdr[1];
    return 0;
}


void vres_record_put(vres_record_t *record)
{
    vres_file_put_entry(record->entry);
}


int vres_record_save(char *path, vres_req_t *req, vres_index_t *index)
{
    int *curr;
    size_t size;
    char name[VRES_PATH_MAX];
    vres_file_entry_t *record;
    vres_file_entry_t *checkin;

    strcpy(name, path);
    vres_path_append_checkin(name);
    checkin = vres_file_get_entry(name, sizeof(int), FILE_RDWR | FILE_CREAT);
    if (!checkin)
        return -ENOENT;
    curr = vres_file_get_desc(checkin, int);
    *index = *curr;
    size = sizeof(vres_rec_hdr_t) + sizeof(vres_req_t) + req->length;
    strcpy(name, path);
    vres_path_append_idx(name, *index);
    record = vres_file_get_entry(name, size, FILE_RDWR | FILE_CREAT);
    if (!record) {
        log_warning("failed to create");
        vres_file_put_entry(checkin);
        return -ENOENT;
    } else {
        vres_rec_hdr_t *hdr = vres_file_get_desc(record, vres_rec_hdr_t);
        vres_req_t *preq = (vres_req_t *)&hdr[1];

        hdr->flg = VRES_RECORD_NEW;
        memcpy(preq, req, sizeof(vres_req_t));
        if (req->length > 0)
            memcpy(&preq[1], req->buf, req->length);
    }
    *curr = vres_idx_inc(*curr);
    vres_file_put_entry(record);
    vres_file_put_entry(checkin);
    log_record_save(req, name);
    return 0;
}


int vres_record_update(char *path, vres_index_t index, vres_rec_hdr_t *hdr)
{
    int ret = 0;
    vres_file_t *filp;
    char name[VRES_PATH_MAX];

    strcpy(name, path);
    vres_path_append_idx(name, index);
    filp = vres_file_open(name, "r+");
    if (!filp)
        return -ENOENT;
    if (vres_file_write(hdr, sizeof(vres_rec_hdr_t), 1, filp) != 1)
        ret = -EIO;
    vres_file_close(filp);
    return ret;
}


int vres_record_head(char *path, vres_index_t *index)
{
    int ret;
    int *curr;
    vres_rec_hdr_t hdr;
    vres_file_entry_t *entry;
    char name[VRES_PATH_MAX];

    strcpy(name, path);
    vres_path_append_checkout(name);
    entry = vres_file_get_entry(name, sizeof(int), FILE_RDONLY | FILE_CREAT);
    if (!entry)
        return -ENOENT;
    curr = vres_file_get_desc(entry, int);
    ret = vres_record_check(path, *curr, &hdr);
    if (!ret) {
        if (!(hdr.flg & VRES_RECORD_NEW))
            ret = -EFAULT;
        else
            *index = *curr;
    }
    vres_file_put_entry(entry);
    return ret;
}


int vres_record_next(char *path, vres_index_t *index)
{
    int next = *index;

    if (*index >= VRES_INDEX_MAX) {
        log_err("failed to get next record, path=%s", path);
        return -EINVAL;
    }
    
    do {
        int ret;
        vres_rec_hdr_t hdr;

        next = vres_idx_inc(next);
        ret = vres_record_check(path, next, &hdr);
        if (ret)
            return ret;
        if (hdr.flg & VRES_RECORD_NEW) {
            *index = (vres_index_t)next;
            return 0;
        }
    } while (next != *index);

    return -ENOENT;
}


int vres_record_remove(char *path, vres_index_t index)
{
    int *curr;
    int ret = 0;
    vres_rec_hdr_t hdr;
    vres_file_entry_t *entry;
    char name[VRES_PATH_MAX];

    strcpy(name, path);
    vres_path_append_checkout(name);
    entry = vres_file_get_entry(name, sizeof(int), FILE_RDONLY);
    if (!entry)
        return -ENOENT;
    curr = vres_file_get_desc(entry, int);
    if (*curr != index) {
        hdr.flg = VRES_RECORD_FREE;
        ret = vres_record_update(path, index, &hdr);
    } else {
        int next = *curr;

        strcpy(name, path);
        vres_path_append_idx(name, next);
        vres_file_remove(name);
        log_record_remove(name, -1, -1);
        do {
            next = vres_idx_inc(next);
            ret = vres_record_check(path, next, &hdr);
            if (ret) {
                if (-ENOENT == ret)
                    ret = 0;
                break;
            }
            if (!(hdr.flg & VRES_RECORD_NEW)) {
                strcpy(name, path);
                vres_path_append_idx(name, next);
                vres_file_remove(name);
                log_record_remove(name, -1, -1);
            } else
                break;
        } while (next != index);
        *curr = next;
    }
    vres_file_put_entry(entry);
    log_record_remove(path, index, *curr);
    return ret;
}
