/* ckpt.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "ckpt.h"

int vres_ckpt_stat = 0;
char vres_ckpt_path[VRES_PATH_MAX];

void vres_ckpt_init()
{
	if (vres_ckpt_stat & VRES_STAT_INIT)
		return;

#ifdef CONTAINER
	sprintf(vres_ckpt_path, "/vhub/mnt/%s/var/ckpt", master_name);
#else
	strcpy(vres_ckpt_path, "/vhub/var/ckpt");
#endif
	log_debug("path=%s", vres_ckpt_path);
	if (access(vres_ckpt_path, F_OK)) {
		log_err("failed to initialize");
		exit(-1);
	}

	vres_ckpt_stat |= VRES_STAT_INIT;
}


int vres_ckpt_get_path_by_id(int id, const char *name, char *path)
{
	char tmp[VRES_PATH_MAX];

	if (name)
		sprintf(tmp, "%lx/%s", (unsigned long)id, name);
	else
		sprintf(tmp, "%lx", (unsigned long)id);

	return vres_path_join(vres_ckpt_path, tmp, path);
}


int vres_ckpt_get_path(vres_t *resource, const char *name, char *path)
{
	return vres_ckpt_get_path_by_id(resource->owner, name, path);
}


int vres_ckpt_check_dir(vres_t *resource)
{
	char path[VRES_PATH_MAX];

	vres_ckpt_get_path(resource, NULL, path);
	if (access(path, F_OK)) {
		if (-1 == mkdir(path, 0755)) {
			vres_log_err(resource, "failed to create, path=%s", path);
			return -EINVAL;
		}
		log_ckpt_check_dir(path);
	}
	return 0;
}


int vres_ckpt_clear(vres_t *resource)
{
	int ret;
	char path[VRES_PATH_MAX];

	ret = vres_ckpt_get_path(resource, TSK_NAME, path);
	if (ret) {
		vres_log_err(resource, "invalid path");
		return ret;
	}
	remove(path);

	ret = vres_ckpt_get_path(resource, RES_NAME, path);
	if (ret) {
		vres_log_err(resource, "invalid path");
		return ret;
	}
	remove(path);
	log_ckpt_clear(resource);
	return 0;
}


int vres_ckpt_request(vres_t *resource, vres_id_t dest)
{
	int ret;
	vres_ckpt_result_t result;

	ret = klnk_io_sync(resource, NULL, 0, (char *)&result, sizeof(vres_ckpt_result_t), &dest);
	if (ret || result.retval) {
		vres_log_err(resource, "failed to request");
		return ret ? ret : result.retval;
	}

	return 0;
}


int vres_ckpt_notify_owner(vres_t *resource)
{
	int ret;
	vres_desc_t owner;

	ret = vres_lookup(resource, &owner);
	if (ret) {
		vres_log_err(resource, "failed to get owner");
		return ret;
	}

	ret = vres_ckpt_request(resource, owner.id);
	if (ret) {
		vres_log_err(resource, "failed to send request to owner");
		return ret;
	}

	log_ckpt_notify_owner(resource);
	return 0;
}


int vres_ckpt_do_notify_members(vres_t *resource)
{
	int i;
	int ret = 0;
	tmp_entry_t *entry;
	vres_members_t *members;
	vres_id_t id = vres_get_id(resource);

	entry = vres_member_get(resource);
	if (!entry)
		return 0;

	members = vres_member_check(entry);
	for (i = 0; i < members->total; i++) {
		if ((members->list[i].id != resource->owner) && (members->list[i].id != id)) {
			ret = vres_ckpt_request(resource, members->list[i].id);
			if (ret) {
				vres_log_err(resource, "failed to request");
				break;
			}
		}
	}

	vres_member_put(entry);
	return ret;
}


int vres_ckpt_notify_members(vres_t *resource)
{
	if (!vres_is_owner(resource))
		return vres_ckpt_notify_owner(resource);
	else {
		log_ckpt_notify_members(resource);
		return vres_ckpt_do_notify_members(resource);
	}
}


int vres_ckpt_suspend_members(char *path, void *arg)
{
	int ret;
	vres_t res;

	if (!vres_is_key_path(path))
		return 0;

	vres_get_resource(path, &res);
	if (vres_can_restart(&res)) {
		ret = vres_event_cancel(&res);
		if (ret) {
			vres_log_err(&res, "failed to cancel");
			return ret;
		}
	}
	vres_set_op(&res, VRES_OP_DUMP);
	ret = vres_ckpt_notify_members(&res);
	log_ckpt_suspend_members(path);
	return ret;
}


int vres_ckpt_do_suspend(vres_t *resource)
{
	char path[VRES_PATH_MAX];

	vres_get_root_path(resource, path);
	return tmp_handle_dir(vres_ckpt_suspend_members, path, NULL);
}


int vres_ckpt_suspend(vres_t *resource, int flags)
{
	int ret;

	ckpt_log("suspend task ...");
	ret = vres_tsk_suspend(resource->owner);
	if (ret) {
		vres_log_err(resource, "failed to suspend task");
		return ret;
	}

	if (!(flags & CKPT_LOCAL)) {
		ckpt_log("notify members ...");
		return vres_ckpt_do_notify_members(resource);
	}

	ckpt_log("clear barrier ...");
	ret = vres_barrier_clear(resource);
	if (ret) {
		vres_log_err(resource, "failed to clear barrier");
		return ret;
	}

	ckpt_log("suspend ...");
	ret = vres_ckpt_do_suspend(resource);
	if (ret)
		vres_log_err(resource, "failed to suspend");

	ckpt_log("set barrier ...");
	vres_barrier_set(resource);
	log_ckpt_suspend(resource);
	return ret;
}


int vres_ckpt_resume_members(char *path, void *arg)
{
	int ret;
	vres_t res;

	if (!vres_is_key_path(path))
		return 0;

	vres_get_resource(path, &res);
	vres_set_op(&res, VRES_OP_RESTORE);
	ret = vres_ckpt_notify_members(&res);
	log_ckpt_resume_members(path);
	return ret;
}


int vres_ckpt_do_resume(vres_t *resource)
{
	char path[VRES_PATH_MAX];

	vres_get_root_path(resource, path);
	return tmp_handle_dir(vres_ckpt_resume_members, path, NULL);
}


int vres_ckpt_resume(vres_t *resource, int flags)
{
	int ret;

	if (!(flags & CKPT_LOCAL)) {
		ckpt_log("notify members ...");
		ret = vres_ckpt_do_notify_members(resource);
		if (!ret) {
			ckpt_log("resume task ...");
			vres_tsk_resume(resource->owner);
		} else
			vres_log_err(resource, "failed to notify");
	} else {
		ckpt_log("clear barrier ...");
		ret = vres_barrier_clear(resource);
		if (ret) {
			vres_log_err(resource, "failed to set barrier");
			return ret;
		}

		ckpt_log("resume ...");
		ret = vres_ckpt_do_resume(resource);
		if (ret)
			vres_log_err(resource, "failed to resume");

		ckpt_log("set barrier ...");
		vres_barrier_set(resource);
	}

	return ret;
}


vres_reply_t *vres_ckpt(vres_req_t *req, int flags)
{
	int ret;
	vres_reply_t *reply = NULL;
	vres_t *resource = &req->resource;
	vres_op_t op = vres_get_op(resource);

	if (VRES_OP_DUMP == op)
		ret = vres_dump(resource, 0);
	else if (VRES_OP_RESTORE == op)
		ret = vres_restore(resource, 0);
	else {
		vres_log_err(resource, "invalid operation");
		ret = -EINVAL;
	}

	vres_create_reply(vres_ckpt_result_t, ret, reply);
	return reply;
}
