/* record.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "record.h"

int vres_record_check(char *path, vres_index_t index, vres_rec_hdr_t *hdr)
{
	int ret = 0;
	tmp_file_t *filp;
	char name[VRES_PATH_MAX];

	strcpy(name, path);
	vres_path_append_index(name, index);
	filp = tmp_open(name, "r");
	if (!filp)
		return -ENOENT;

	if (tmp_read(hdr, sizeof(vres_rec_hdr_t), 1, filp) != 1)
		ret = -EIO;

	tmp_close(filp);
	return ret;
}


int vres_record_get(char *path, vres_index_t index, vres_record_t *record)
{
	vres_rec_hdr_t *hdr;
	tmp_entry_t *entry;
	char name[VRES_PATH_MAX];

	strcpy(name, path);
	vres_path_append_index(name, index);
	entry = tmp_get_entry(name, 0, TMP_RDONLY);
	if (!entry)
		return -ENOENT;
	hdr = tmp_get_desc(entry, vres_rec_hdr_t);
	record->entry = entry;
	record->req = (vres_req_t *)&hdr[1];
	return 0;
}


void vres_record_put(vres_record_t *record)
{
	tmp_put_entry(record->entry);
}


int vres_record_save(char *path, vres_req_t *req, vres_index_t *index)
{
	int *curr;
	size_t size;
	char name[VRES_PATH_MAX];
	tmp_entry_t *record;
	tmp_entry_t *checkin;

	strcpy(name, path);
	vres_path_append_checkin(name);
	checkin = tmp_get_entry(name, sizeof(int), TMP_RDWR | TMP_CREAT);
	if (!checkin)
		return -ENOENT;
	curr = tmp_get_desc(checkin, int);
	*index = *curr;

	// record saving
	size = sizeof(vres_rec_hdr_t) + sizeof(vres_req_t) + req->length;
	strcpy(name, path);
	vres_path_append_index(name, *index);
	record = tmp_get_entry(name, size, TMP_RDWR | TMP_CREAT);
	if (!record) {
		log_err("failed to create");
		tmp_put_entry(checkin);
		return -ENOENT;
	} else {
		vres_rec_hdr_t *hdr = tmp_get_desc(record, vres_rec_hdr_t);
		vres_req_t *preq = (vres_req_t *)&hdr[1];

		hdr->flg = VRES_RECORD_NEW;
		memcpy(preq, req, sizeof(vres_req_t));
		if (req->length > 0)
			memcpy(&preq[1], req->buf, req->length);
	}

	// update index
	if (*curr + 1 >= VRES_INDEX_MAX)
		*curr = 0;
	else
		*curr = *curr + 1;

	tmp_put_entry(record);
	tmp_put_entry(checkin);
	log_record_save(req, name);
	return 0;
}


int vres_record_update(char *path, vres_index_t index, vres_rec_hdr_t *hdr)
{
	int ret = 0;
	tmp_file_t *filp;
	char name[VRES_PATH_MAX];

	strcpy(name, path);
	vres_path_append_index(name, index);
	filp = tmp_open(name, "r+");
	if (!filp)
		return -ENOENT;

	if (tmp_write(hdr, sizeof(vres_rec_hdr_t), 1, filp) != 1)
		ret = -EIO;

	tmp_close(filp);
	return ret;
}


int vres_record_first(char *path, vres_index_t *index)
{
	int ret;
	int *curr;
	vres_rec_hdr_t hdr;
	tmp_entry_t *entry;
	char name[VRES_PATH_MAX];

	strcpy(name, path);
	vres_path_append_checkout(name);
	entry = tmp_get_entry(name, sizeof(int), TMP_RDONLY | TMP_CREAT);
	if (!entry)
		return -ENOENT;
	curr = tmp_get_desc(entry, int);
	ret = vres_record_check(path, *curr, &hdr);
	if (!ret) {
		if (!(hdr.flg & VRES_RECORD_NEW))
			ret = -EFAULT;
		else
			*index = *curr;
	}
	tmp_put_entry(entry);
	return ret;
}


int vres_record_next(char *path, vres_index_t *index)
{
	int next = *index;

	if (*index >= VRES_INDEX_MAX)
		return -EINVAL;
	do {
		int ret;
		vres_rec_hdr_t hdr;

		if (++next >= VRES_INDEX_MAX)
			next = 0;
		ret = vres_record_check(path, next, &hdr);
		if (ret)
			return ret;
		if(hdr.flg & VRES_RECORD_NEW) {
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
	tmp_entry_t *entry;
	char name[VRES_PATH_MAX];

	strcpy(name, path);
	vres_path_append_checkout(name);
	entry = tmp_get_entry(name, sizeof(int), TMP_RDONLY);
	if (!entry)
		return -ENOENT;
	curr = tmp_get_desc(entry, int);

	if (*curr != index) {
		hdr.flg = VRES_RECORD_FREE;
		ret = vres_record_update(path, index, &hdr);
	} else {
		int next = *curr;

		strcpy(name, path);
		vres_path_append_index(name, next);
		tmp_remove(name);
		do {
			if (++next >= VRES_INDEX_MAX)
				next = 0;
			ret = vres_record_check(path, next, &hdr);
			if (ret) {
				if (-ENOENT == ret)
					ret = 0;
				break;
			}
			if (!(hdr.flg & VRES_RECORD_NEW)) {
				strcpy(name, path);
				vres_path_append_index(name, next);
				tmp_remove(name);
			} else
				break;
		} while (next != index);
		*curr = next;
	}
	tmp_put_entry(entry);
	return ret;
}


int vres_record_is_empty(char *path)
{
	int ret;
	vres_index_t index;

	ret = vres_record_first(path, &index);
	if (ret) {
		if (-ENOENT == ret)
			return 1;
		return -EINVAL;
	} else
		return 0;
}
