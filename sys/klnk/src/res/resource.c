/* resource.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "resource.h"

#if defined(STATIC_MANAGER)
#include "mgr/static.h"
#elif defined(DYNAMIC_MANAGER)
#include "mgr/dynamic.h"
#endif

void vres_init()
{
	tmp_init();
#ifdef NODE
	node_init();
#endif
	vres_event_init();
	vres_lock_init();
	vres_rwlock_init();
	vres_page_lock_init();
	vres_cache_init();
	vres_prio_init();
	vres_barrier_init();
	vres_ckpt_init();
	vres_metadata_init();
}


int vres_check_path(vres_t *resource)
{
	int ret;
	char path[VRES_PATH_MAX] = {0};

	vres_get_root_path(resource, path);
	if (!tmp_is_dir(path)) {
		ret = tmp_mkdir(path);
		if (ret) {
			vres_log_err(resource, "failed to create path");
			return ret;
		}
	}

	vres_get_cls_path(resource, path);
	if (!tmp_is_dir(path)) {
		ret = tmp_mkdir(path);
		if (ret) {
			vres_log_err(resource, "failed to create path");
			return ret;
		}
	}

	vres_get_path(resource, path);
	if (!tmp_is_dir(path)) {
		ret = tmp_mkdir(path);
		if (ret) {
			vres_log_err(resource, "failed to create path");
			return ret;
		}
	}

	return 0;
}


void vres_clear_path(vres_t *resource)
{
	char path[VRES_PATH_MAX] = {0};

	vres_get_path(resource, path);
	if (tmp_is_dir(path)) {
		if (!tmp_is_empty_dir(path))
			return;
		tmp_rmdir(path);
	}

	vres_get_cls_path(resource, path);
	if (tmp_is_dir(path)) {
		if (!tmp_is_empty_dir(path))
			return;
		tmp_rmdir(path);
	}

	vres_get_root_path(resource, path);
	if (tmp_is_dir(path)) {
		if (!tmp_is_empty_dir(path))
			return;
		tmp_rmdir(path);
	}
}


int vres_do_check_resource(vres_t *resource)
{
	char path[VRES_PATH_MAX] = {0};

	vres_get_path(resource, path);
	if (!tmp_is_dir(path)) {
		vres_log_err(resource, "no owner");
		return -ENOOWNER;
	}

	return 0;
}


int vres_check_resource(vres_t *resource)
{
	vres_t res = *resource;

	if (resource->cls == VRES_CLS_TSK)
		return 0;

	if (VRES_OP_REPLY == vres_get_op(resource)) {
		res.cls = VRES_CLS_TSK;
		res.key = vres_get_id(resource);
	}

#if defined(STATIC_MANAGER)
	return static_check_resource(&res);
#elif defined(DYNAMIC_MANAGER)
	return dynamic_check_resource(&res);
#else
	return vres_do_check_resource(&res);
#endif
}


int vres_is_owner(vres_t *resource)
{
	char path[VRES_PATH_MAX] = {0};

	if (resource->cls == VRES_CLS_TSK)
		return 1;
	else {
		vres_get_state_path(resource, path);
		if (tmp_access(path, F_OK))
			return 0;
		else
			return 1;
	}
}


int vres_has_owner_path(char *path)
{
	char name[VRES_PATH_MAX] = {0};

	strcpy(name, path);
	strcat(name, VRES_PATH_SEPERATOR);
	vres_path_append_state(name);
	if (tmp_access(name, F_OK))
		return 0;
	else
		return 1;
}


int vres_get(vres_t *resource, vres_desc_t *desc)
{
	char path[VRES_PATH_MAX] = {0};

	vres_get_key_path(resource, path);
	return vres_metadata_read(path, (char *)desc, sizeof(vres_desc_t));
}


int vres_save_peer(vres_desc_t *desc)
{
	vres_t res;

	if (!desc || (desc->id <= 0)) {
		log_err("invalid peer");
		return -EINVAL;
	}

	res.key = desc->id;
	res.cls = VRES_CLS_TSK;
	return vres_cache_write(&res, (char *)desc, sizeof(vres_desc_t));
}


int vres_get_peer(vres_id_t id, vres_desc_t *desc)
{
	vres_t res;

	res.key = id;
	res.cls = VRES_CLS_TSK;
	return vres_lookup(&res, desc);
}


void vres_get_desc(vres_t *resource, vres_desc_t *desc)
{
	desc->address = node_addr;
	desc->id = vres_get_id(resource);
}


int vres_lookup(vres_t *resource, vres_desc_t *desc)
{
	int ret = vres_cache_read(resource, (char *)desc, sizeof(vres_desc_t));

	if (-ENOENT == ret) {
		ret = vres_get(resource, desc);
		if (!ret)
			ret = vres_cache_write(resource, (char *)desc, sizeof(vres_desc_t));
	}

	return ret;
}


int vres_exists(vres_t *resource)
{
	char path[VRES_PATH_MAX] = {0};

	vres_get_key_path(resource, path);
	return vres_metadata_exists(path);
}


int vres_create(vres_t *resource)
{
	int ret;
	vres_desc_t desc;
	char path[VRES_PATH_MAX] = {0};

	vres_get_desc(resource, &desc);
	vres_get_key_path(resource, path);
	ret = vres_metadata_create(path, (char *)&desc, sizeof(vres_desc_t));
	if (!ret)
		ret = vres_cache_write(resource, (char *)&desc, sizeof(vres_desc_t));
	return ret;
}


int vres_flush(vres_t *resource)
{
	int ret;
	char path[VRES_PATH_MAX] = {0};

	vres_get_key_path(resource, path);
	ret = vres_metadata_remove(path);
	if (ret) {
		vres_log_err(resource, "failed to remove");
		return ret;
	}

	return 0;
}


int vres_remove(vres_t *resource)
{
	char path[VRES_PATH_MAX] = {0};

	if (vres_is_owner(resource))
		vres_flush(resource);

	vres_cache_flush(resource);
	vres_get_path(resource, path);
	tmp_rmdir(path);

	vres_get_cls_path(resource, path);
	if (tmp_is_empty_dir(path))
		tmp_rmdir(path);

	return 0;
}


int vres_get_max_key(vres_cls_t cls)
{
	char path[VRES_PATH_MAX] = {0};

	vres_path_append_cls(path, cls);
	return vres_metadata_max(path);
}


int vres_get_resource_count(vres_cls_t cls)
{
	char path[VRES_PATH_MAX] = {0};

	vres_path_append_cls(path, cls);
	return vres_metadata_count(path);
}


int vres_destroy(vres_t *resource)
{
	int ret = 0;
	int own = vres_is_owner(resource);
	int pub = vres_member_is_public(resource);

	if (own || pub) {
		ret = vres_member_delete(resource);
		if (ret) {
			vres_log_err(resource, "failed to delete this member");
			return ret;
		}

		if (own) {
			switch(resource->cls) {
			case VRES_CLS_SHM:
				ret = vres_shm_destroy(resource);
				break;
			default:
				break;
			}
		}
	}

	return ret;
}


int vres_get_events(vres_t *resource, vres_index_t **events)
{
	int size;
	int ret = 0;
	tmp_file_t *filp;
	char path[VRES_PATH_MAX];

	*events = NULL;
	vres_get_event_path(resource, path);
	filp = tmp_open(path, "r");
	if (!filp)
		return 0;

	size = tmp_size(filp);
	if (size > 0) {
		char *buf;

		buf = malloc(size);
		if (buf) {
			if (tmp_read(buf, 1, size, filp) != size) {
				vres_log_err(resource, "failed to read");
				free(buf);
				ret = -EIO;
			} else {
				*events = (vres_index_t *)buf;
				ret = size / sizeof(vres_index_t);
			}
		} else {
			vres_log_err(resource, "no memory");
			ret = -ENOMEM;
		}
	}

	tmp_close(filp);
	return ret;
}
