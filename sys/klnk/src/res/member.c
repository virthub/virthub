/* member.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "member.h"

int vres_member_is_public(vres_t *resource)
{
	return resource->cls == VRES_CLS_SHM;
}


vres_members_t *vres_member_check(void *entry)
{
	tmp_entry_t *ent = (tmp_entry_t *)entry;

	return tmp_get_desc(ent, vres_members_t);
}


void *vres_member_get(vres_t *resource)
{
	tmp_entry_t *entry;
	char path[VRES_PATH_MAX];

	vres_get_member_path(resource, path);
	entry = tmp_get_entry(path, 0, TMP_RDWR);
	log_member_get(resource);
	return entry;
}


void *vres_member_get_entry(vres_t *resource)
{
	tmp_entry_t *entry;
	char path[VRES_PATH_MAX];

	vres_get_member_path(resource, path);
	entry = tmp_get_entry(path, 0, TMP_RDWR);
	if (!entry)
		entry = tmp_get_entry(path, sizeof(vres_members_t), TMP_RDWR | TMP_CREAT);
	log_member_get(resource);
	return entry;
}


void vres_member_put(void *entry)
{
	tmp_put_entry((tmp_entry_t *)entry);
}


int vres_member_create(vres_t *resource)
{
	void *entry;

	entry = vres_member_get_entry(resource);
	if (!entry) {
		vres_log_err(resource, "no entry");
		return -ENOENT;
	}

	vres_member_put(entry);
	return 0;
}


int vres_member_append(void *entry, vres_member_t *member)
{
	return tmp_append(entry, member, sizeof(vres_member_t));
}


int vres_member_get_size(void *entry)
{
	tmp_file_t *filp = ((tmp_entry_t *)entry)->file;

	return tmp_size(filp);
}


int vres_member_add(vres_t *resource)
{
	int i;
	int total;
	int ret = 0;
	void *entry;
	vres_member_t member;
	vres_members_t *members;
	vres_id_t id = vres_get_id(resource);

	entry = vres_member_get_entry(resource);
	if (!entry) {
		vres_log_err(resource, "no entry");
		return -ENOENT;
	}

	members = vres_member_check(entry);
	total = members->total;
	for (i = 0; i < total; i++) {
		if (members->list[i].id == id) {
			vres_log_err(resource, "already exist");
			ret = -EEXIST;
			goto out;
		}
	}

	if ((total >= VRES_MEMBER_MAX) || (total < 0)) {
		vres_log_err(resource, "out of range");
		ret = -EINVAL;
		goto out;
	}

	total++;
	members->total = total;
	memset(&member, 0, sizeof(vres_member_t));
	member.id = id;
	ret = vres_member_append(entry, &member);
	if (ret)
		vres_log_err(resource, "failed to append");
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
		vres_log_err(resource, "no entry");
		return -ENOENT;
	}

	members = vres_member_check(entry);
	if (members->total < 1) {
		vres_log_err(resource, "no entry");
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
		vres_log_err(resource, "no entry");
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
	int pos = -ENOENT;
	vres_members_t *members;

	vres_rwlock_rdlock(resource);
	entry = vres_member_get(resource);
	if (!entry) {
		vres_log_err(resource, "no entry");
		goto out;
	}

	members = vres_member_check(entry);
	for (i = 0; i < members->total; i++) {
		if (members->list[i].id == id) {
			pos = i;
			break;
		}
	}

	vres_member_put(entry);
out:
	vres_rwlock_unlock(resource);
	return pos;
}


int vres_member_notify(vres_req_t *req)
{
	int i;
	int ret = 0;
	void *entry;
	vres_members_t *members;
	vres_t *resource = &req->resource;
	vres_id_t src = vres_get_id(resource);

	entry = vres_member_get(resource);
	if (!entry) {
		vres_log_err(resource, "no entry");
		return -ENOENT;
	}

	members = vres_member_check(entry);
	for (i = 0; i < members->total; i++) {
		vres_id_t id = members->list[i].id;

		if ((id != resource->owner) && (id != src)) {
			ret = klnk_io_sync(resource, req->buf, req->length, NULL, 0, &id);
			if (ret) {
				vres_log_err(resource, "failed to send");
				break;
			}
		}
	}

	vres_member_put(entry);
	return ret;
}


int vres_member_save(vres_t *resource, vres_id_t *list, int total)
{
	int i;
	int ret = 0;
	void *entry;
	vres_member_t member;
	vres_members_t *members;

	if ((total > VRES_MEMBER_MAX) || (total <= 0)) {
		vres_log_err(resource, "invalid member list");
		return -EINVAL;
	}

	entry = vres_member_get_entry(resource);
	if (!entry) {
		vres_log_err(resource, "no entry");
		return -ENOENT;
	}

	members = vres_member_check(entry);
	if (vres_member_get_size(entry) != sizeof(vres_members_t)) {
		vres_log_err(resource, "invalid size");
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


int vres_member_is_active(vres_t *resource)
{
	int i;
	int ret = 0;
	void *entry;
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
				ret = 1;
			break;
		}
	}

	vres_member_put(entry);
	return ret;
}


int vres_member_lookup(vres_t *resource, vres_member_t *member, int *active)
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

	if (!ret && active) {
		int count = 0;

		for (i = 0; i < members->total; i++)
			if (members->list[i].count > 0)
				count++;

		*active = count;
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

	vres_rwlock_wrlock(resource);
	entry = vres_member_get(resource);
	if (!entry) {
		vres_log_err(resource, "no entry");
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
