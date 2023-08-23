#ifndef _RECORD_H
#define _RECORD_H

#include "resource.h"
#include "trace.h"
#include "file.h"

#define VRES_RECORD_NEW    0x00000001
#define VRES_RECORD_FREE   0x00000002
#define VRES_RECORD_CANCEL 0x00000004

#ifdef SHOW_RECORD
#define LOG_RECORD_SAVE
#define LOG_RECORD_REMOVE
#define LOG_RECORD_CHECK_EMPTY
#endif

#include "log_record.h"

typedef struct vres_record_header {
    vres_flg_t flg;
} vres_rec_hdr_t;

typedef struct vres_record {
    vres_file_entry_t *entry;
    vres_req_t *req;
} vres_record_t;

void vres_record_put(vres_record_t *record);
int vres_record_next(char *path, vres_index_t *index);
int vres_record_head(char *path, vres_index_t *index);
int vres_record_remove(char *path, vres_index_t index);
int vres_record_save(char *path, vres_req_t *req, vres_index_t *index);
int vres_record_get(char *path, vres_index_t index, vres_record_t *record);

#endif
