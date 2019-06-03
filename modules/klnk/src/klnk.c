/* klnk.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "klnk.h"

char log_name[256];

void klnk_get_ifname(char *name)
{
#ifdef VETH
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
        log_err("invlalid address %s", addr);
        return -EINVAL;
    }
    strcpy(master_addr, addr);
    sprintf(master_name, "%08x", in.s_addr);
    sprintf(log_name, "%s/klnk.%s.log", PATH_LOG, master_name);
    truncate(log_name, 0);
    klnk_get_ifname(ifname);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);
    node_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
    sprintf(node_name, "%08x", node_addr.s_addr);
    log_ln("addr=%s (master=%s, iface=%s)", addr, master_name, ifname);
    return 0;
}


void *klnk_run(void *arg)
{
    klnk_desc_t srv = klnk_listen(node_addr, KLNK_PORT);

    if (srv < 0) {
        log_err("failed to create server");
        return NULL;
    }
    for (;;) {
        klnk_desc_t desc = klnk_accept(srv);

        if (desc < 0)
            continue;
        if (klnk_handler_create((void *)desc)) {
            log_err("failed to create handler");
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
    else
        return vres_mkdir(pres);
}


int klnk_load_managers(char *buf)
{
    const char *name = "MANAGERS=";

    if (strlen(buf) > 0 && !strncmp(buf, name, strlen(name))) {
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
                    log_err("invalid address");
                    return -EINVAL;
                }
                memcpy(address, &p[start], n);
                address[n] = '\0';
                if (!inet_aton(address, &addr)) {
                    log_err("invalid address %s", address);
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
            log_err("no memory");
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
                if (vres_managers[cnt].s_addr == node_addr.s_addr) {
                    if (klnk_create_manager(cnt + 1)) {
                        log_err("failed to create manager");
                        return -EINVAL;
                    }
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
        log_err("failed to open");
        return -ENOENT;
    }
    while (fscanf(fp, "%s\n", buf) != EOF) {
        ret = klnk_load_managers(buf);
        if (ret) {
            log_err("failed to get managers");
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
        log_err("failed to check address");
        return ret;
    }
    ret = klnk_create();
    if (ret) {
        log_err("failed to create");
        return ret;
    }
    klnk_mutex_init();
    vres_init();
    ret = klnk_load_conf();
    if (ret) {
        log_err("failed to load conf");
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


inline bool klnk_check_bypass(vres_t *resource)
{
    switch (vres_get_op(resource)) {
#ifdef DISABLE_PGSAVE
    case VRES_OP_PGSAVE:
        return true;
#endif
#ifdef DISABLE_TSKPUT
    case VRES_OP_TSKPUT:
        return true;
#endif
    default:
        return false;
    }
}


int klnk_open(const char *path, struct fuse_file_info *fi)
{
    int ret = 0;
    vres_arg_t arg;
    vres_t resource;
    size_t inlen = 0;
    size_t outlen = 0;
    unsigned long addr;
    vres_arg_t *parg = &arg;
    vres_t *pres = &resource;

    ret = vres_parse(path, pres, &addr, &inlen, &outlen);
    if (ret) {
        log_err("failed to get resource");
        goto err;
    }
    if (klnk_check_bypass(pres)) {
        log_klnk_open(pres, "bypass (op=%s)", log_get_op(vres_get_op(pres)));
        assert(!vres_can_expose(pres));
        return -EOK;
    }
    log_klnk_open(pres, ">-- open (begin) --<");
    vres_barrier_wait_timeout(pres, VRES_BARRIER_TIMEOUT);
    klnk_mutex_lock(pres);
    ret = vres_rpc_get(pres, addr, inlen, outlen, parg);
    if (-EAGAIN == ret) {
        log_resource_warning(pres, "waiting");
        goto wait;
    } else if (ret)
        goto out;
    ret = vres_rpc(parg);
    if (ret) {
        log_resource_err(pres, "failed to invoke rpc, ret=%s", log_get_err(ret));
        goto out;
    }
wait:
    ret = vres_rpc_wait(parg);
    vres_rpc_put(parg);
out:
    klnk_mutex_unlock(pres);
err:
    if (!vres_can_expose(pres))
        ret = ret ? ret : -EOK;
    log_klnk_open(pres, ">-- open (end, ret=%s) --<", log_get_err(ret));
    return ret;
}


static struct fuse_operations klnk_oper = {
    .getattr = klnk_getattr,
    .open = klnk_open,
};


void usage()
{
    printf("usage: klnk [address] [mountpoint]\n");
    printf("  address: the IP address of master\n");
    printf("  mountpoint: the path used for mounting the klnk file system\n");
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
        log_ln("failed to initialize");
        return -1;
    }
    log_ln("node    : %s", node_name);
    log_ln("master  : %s", master_addr);
    log_ln("mount   : %s", argv[2]);
#ifdef UNSHARE
    log_ln("unshare : 1");
    if (unshare(CLONE_NEWIPC) != 0) {
        log_ln("failed to unshare");
        return -1;
    }
#endif
    args[0] = argv[0];
    args[1] = argv[2];
    args[2] = "-f";
    return fuse_main(nr_args, args, &klnk_oper, NULL);
}
