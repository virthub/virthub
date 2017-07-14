/* dynamic.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "dynamic.h"

int vres_dmgr_get_owner(vres_t *resource)
{
	return (vres_get_off(resource) % vres_nr_managers) + 1;
}


int vres_dmgr_is_owner(vres_t *resource)
{
	return resource->owner == vres_dmgr_get_owner(resource);
}


int vres_dmgr_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers)
{
	int flags = vres_get_flags(resource);

	if (!vres_pg_own(page)) {
		vres_id_t owner;

		if (!page->owner)
			owner = vres_dmgr_get_owner(resource);
		else
			owner = page->owner;

		peers->list[0] = owner;
		peers->total = 1;

		if (flags & VRES_RDWR) {
			vres_set_flags(resource, VRES_OWN);
			vres_pg_mkown(page);
		}
	} else {
		if (flags & VRES_RDWR) {
			int i;
			int cnt = 0;

			for (i = 0; i < page->nr_holders; i++) {
				if (page->holders[i] != resource->owner) {
					peers->list[cnt] = page->holders[i];
					cnt++;
				}
			}
			peers->total = cnt;
		} else
			return -EINVAL;
	}

	return 0;
}


int vres_dmgr_request_holders(vres_page_t *page, vres_req_t *req)
{
	int i;
	int ret = 0;
	int cnt = 0;
	int nr_threads;
	pthread_t *threads;
	vres_t *resource = &req->resource;
	int flags = vres_get_flags(resource);
	vres_id_t src = vres_get_id(resource);

	if (flags & VRES_RDWR)
		nr_threads = page->nr_holders;
	else
		return 0;

	threads = malloc(nr_threads * sizeof(pthread_t));
	if (!threads) {
		vres_log_err(resource, "no memory");
		return -ENOMEM;
	}

	for (i = 0; i < page->nr_holders; i++) {
		if ((page->holders[i] != src) && (page->holders[i] != resource->owner)) {
			ret = klnk_io_async(resource, req->buf, req->length, NULL, 0, &page->holders[i], &threads[cnt]);
			if (ret) {
				vres_log_err(resource, "failed to send request");
				break;
			}
			cnt++;
		}
	}

	for (i = 0; i < cnt; i++)
		pthread_join(threads[i], NULL);
	free(threads);
	return ret;
}


int vres_dmgr_create(vres_t *resource)
{
	tmp_file_t *filp;
	struct shmid_ds shmid_ds;
	char path[VRES_PATH_MAX];

	memset(&shmid_ds, 0, sizeof(struct shmid_ds));
	vres_get_state_path(resource, path);
	filp = tmp_open(path, "w");
	if (!filp) {
		vres_log_err(resource, "failed to create");
		return -ENOENT;
	}

	if (tmp_write((char *)&shmid_ds, sizeof(struct shmid_ds), 1, filp) != 1) {
		tmp_close(filp);
		return -EIO;
	}

	tmp_close(filp);
	return 0;
}


int vres_dmgr_check_resource(vres_t *resource)
{
	char path[VRES_PATH_MAX] = {0};

	vres_get_path(resource, path);
	if (!tmp_is_dir(path)) {
		if (VRES_CLS_SHM == resource->cls) {
			vres_check_path(resource);
			vres_dmgr_create(resource);
		} else
			return -ENOOWNER;
	}

	return 0;
}


void vres_dmgr_check_reply(vres_t *resource, vres_page_t *page, int total, int *head, int *tail, int *reply)
{
	int flags = vres_get_flags(resource);

	*reply = flags & VRES_RDWR;
	if (resource->owner == page->owner) {
		*head = 1;
		*tail = 1;
	} else {
		*head = 0;
		*tail = 0;
	}
}


int vres_dmgr_check_coverage(vres_t *resource, vres_page_t *page, int line)
{
	return resource->owner == page->owner;
}


int vres_dmgr_forward(vres_page_t *page, vres_req_t *req)
{
	int ret;
	vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
	int cmd = arg->cmd;

	arg->cmd = VRES_SHM_NOTIFY_OWNER;
	ret = klnk_io_sync(&req->resource, req->buf, req->length, NULL, 0, &page->owner);
	arg->cmd = cmd;
	return ret;
}


int vres_dmgr_update_owner(vres_page_t *page, vres_req_t *req)
{
	vres_t *resource = &req->resource;
	int flags = vres_get_flags(resource);

	if (flags & VRES_OWN)
		page->owner = vres_get_id(resource);

	return 0;
}
