/* klnk.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "klnk.h"

char log_name[256];

void klnk_get_ifname(char *name)
{
#ifdef CONTAINER
	strcpy(name, master_name);
#else
	strcpy(name, IFNAME);
#endif
}


static inline int klnk_check_address(char *addr)
{
	int fd;
	struct ifreq ifr;
	struct in_addr in;
	char ifname[9] = {0};

	if (!inet_aton(addr, &in)) {
		klnk_log_err("invlalid address");
		return -EINVAL;
	}

	strcpy(master_addr, addr);
	sprintf(master_name, "%08x", in.s_addr);
	sprintf(log_name, "%s/klnk.%s", PATH_LOG, master_name);
	truncate(log_name, 0);

	klnk_get_ifname(ifname);
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	ioctl(fd, SIOCGIFADDR, &ifr);
	close(fd);
	node_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
	sprintf(node_name, "%08x", node_addr.s_addr);
	klnk_log("addr=%s, node=%s, master=%s, ifname=%s", addr, node_name, master_name, ifname);
	return 0;
}


void *klnk_run(void *arg)
{
	klnk_desc_t srv = klnk_listen(node_addr, KLNK_PORT);

	if (srv < 0) {
		klnk_log_err("failed to create server");
		return NULL;
	}

	for (;;) {
		klnk_desc_t desc = klnk_accept(srv);

		if (desc < 0)
			continue;

		if (klnk_handler_create((void *)desc)) {
			klnk_log_err("failed to create handler");
			klnk_close(desc);
			break;
		}
	}
	klnk_close(srv);
	return NULL;
}


static inline int klnk_create()
{
	int ret;
	pthread_t tid;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
	ret = pthread_create(&tid, &attr, klnk_run, NULL);
	pthread_attr_destroy(&attr);
	return ret;
}


int klnk_create_manager(vres_id_t id)
{
	int ret;
	vres_t res;
	vres_t *pres = &res;

	memset(pres, 0, sizeof(vres_t));
	pres->key = id;
	pres->owner = id;
	pres->cls = VRES_CLS_TSK;
	vres_set_id(pres, id);

	if (vres_exists(pres))
		vres_flush(pres);

	ret = vres_create(pres);
	if (ret)
		return ret;
	return vres_check_path(pres);
}


int klnk_load_managers(char *buf)
{
	const char *name = "MANAGERS=";

	if (strlen(buf) > 0 && !strncmp(buf, name, strlen(name)))	{
		int i;
		int cnt = 0;
		int start = 0;
		char address[16];
		char *p = &buf[strlen(name)];
		int length = strlen(p);

		for (i = 0; i < length; i++) {
			if ((p[i] == ',') || (i == length - 1)) {
				int n = i - start;
				struct in_addr addr;

				if (i == length - 1)
					n += 1;

				if ((n > 15) || (n <= 0)) {
					klnk_log_err("invalid address");
					return -EINVAL;
				}

				memcpy(address, &p[start], n);
				address[n] = '\0';

				if (!inet_aton(address, &addr)) {
					klnk_log_err("invalid address");
					return -EINVAL;
				}

				start = i + 1;
				cnt++;
			}
		}

		if (!cnt || (cnt > VRES_MANAGER_MAX))
			return -EINVAL;

		vres_managers = (vres_addr_t *)malloc(cnt * sizeof(vres_addr_t));
		if (!vres_managers) {
			klnk_log_err("no memory");
			return -ENOMEM;
		}

		for (i = 0, cnt = 0, start = 0; i < length; i++) {
			if ((p[i] == ',') || (i == length - 1)) {
				int n = i - start;

				if (i == length - 1)
					n += 1;

				memcpy(address, &p[start], n);
				address[n] = '\0';
				inet_aton(address, &vres_managers[cnt]);
				if (vres_managers[cnt].s_addr == node_addr.s_addr)
					if (klnk_create_manager(cnt + 1)) {
						klnk_log_err("failed to create manager");
						return -EINVAL;
					}

				start = i + 1;
				cnt++;
			}
		}
		vres_nr_managers = cnt;
	}

	return 0;
}


int klnk_load_conf()
{
	FILE *fp;
	int ret = 0;
	char buf[512] = {0};

	if (access(PATH_CONF, F_OK))
		return 0;

	fp = fopen(PATH_CONF, "r");
	if (!fp) {
		klnk_log_err("failed to open");
		return -ENOENT;
	}

	while (fscanf(fp, "%s\n", buf) != EOF) {
		ret = klnk_load_managers(buf);
		if (ret) {
			klnk_log_err("failed to load managers");
			break;
		}
	}

	fclose(fp);
	return ret;
}


static inline int klnk_init(char *addr)
{
	int ret;

	ret = klnk_check_address(addr);
	if (ret) {
		klnk_log_err("failed to check address");
		return ret;
	}

	ret = klnk_create();
	if (ret) {
		klnk_log_err("failed to create");
		return ret;
	}

	klnk_mutex_init();
	vres_init();
	ret = klnk_load_conf();
	if (ret) {
		klnk_log_err("failed to load conf");
		return ret;
	}

	return 0;
}


int klnk_getattr(const char *path, struct stat *stbuf)
{
	memset(stbuf, 0, sizeof(struct stat));
	if (!strcmp(path, "/")) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
		stbuf->st_mode = S_IFREG | 0777;
		stbuf->st_nlink = 1;
		stbuf->st_size = 0x7fffffff;
	}

	return 0;
}


int klnk_open(const char *path, struct fuse_file_info *fi)
{
	int ret;
	vres_arg_t arg;
	vres_t resource;
	size_t inlen = 0;
	size_t outlen = 0;
	unsigned long addr;

	ret = vres_parse(path, &resource, &addr, &inlen, &outlen);
	if (ret) {
		klnk_log_err("failed to get resource");
		goto err;
	}

	vres_barrier_wait_timeout(&resource, VRES_BARRIER_TIMEOUT);
	klnk_mutex_lock(&resource);
	ret = vres_rpc_get(&resource, addr, inlen, outlen, &arg);
	if (-EAGAIN == ret)
		goto wait;
	else if (ret)
		goto out;

	ret = vres_rpc(&arg);
	if (ret)
		goto out;
wait:
	ret = vres_rpc_wait(&arg);
	vres_rpc_put(&arg);
out:
	klnk_mutex_unlock(&resource);
err:
	if (!vres_can_expose(&resource))
		ret = ret ? ret : -EOK;
	klnk_log("path=%s, ret=%d", path, ret);
	return ret;
}


static struct fuse_operations klnk_oper = {
	.getattr	= klnk_getattr,
	.open		= klnk_open,
};


void usage()
{
	printf("usage: klnk [address] [mountpoint]\n");
	printf("address: the IP address of master\n");
	printf("mountpoint: a mount point folder to klnk\n");
}


int main(int argc, char *argv[])
{
	const int nr_args = 3;
	char *args[nr_args];

	umask(0);
	if (argc != 3) {
		usage();
		return -1;
	}

	if (klnk_init(argv[1])) {
		printf("failed to initialize");
		return -1;
	}

	log("node=%s\n", node_name);
	log("master=%s\n", master_addr);
	log("mount=%s\n", argv[2]);

	args[0] = argv[0];
	args[1] = argv[2];
	args[2] = "-f";
	return fuse_main(nr_args, args, &klnk_oper, NULL);
}
