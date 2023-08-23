#include "klnk.h"

char log_name[256];
char mon_name[256];

void klnk_get_ifname(char *name)
{
#ifdef VETH
    strcpy(name, mds_name);
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
    strcpy(mds_addr, addr);
    sprintf(mds_name, "%08x", in.s_addr);
    sprintf(log_name, "%s/klnk.%s.log", PATH_LOG, mds_name);
    truncate(log_name, 0);
#ifdef ENABLE_MONITOR
    sprintf(mon_name, "%s/klnk.%s.mon", PATH_LOG, mds_name);
    truncate(mon_name, 0);
#endif
    klnk_get_ifname(ifname);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);
    node_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
    sprintf(node_name, "%08x", node_addr.s_addr);
    log_ln("mds=%s (iface=%s)", addr, ifname);
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
    vres_t res;
    vres_t *pres = &res;

    memset(pres, 0, sizeof(vres_t));
    pres->key = id;
    pres->owner = id;
    pres->cls = VRES_CLS_TSK;
    vres_set_id(pres, id);
    if (vres_exists(pres))
        vres_flush(pres);
    if (!vres_create(pres))
        return -EEXIST;
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
        resource_manager_list = (vres_addr_t *)malloc(cnt * sizeof(vres_addr_t));
        if (!resource_manager_list) {
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
                inet_aton(address, &resource_manager_list[cnt]);
                if (resource_manager_list[cnt].s_addr == node_addr.s_addr) {
                    if (klnk_create_manager(cnt + 1)) {
                        log_err("failed to create manager");
                        return -EINVAL;
                    }
                }
                start = i + 1;
                cnt++;
            }
        }
        nr_resource_managers = cnt;
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
    vres_init();
    klnk_mutex_init();
    klnk_barrier_init();
    ret = klnk_load_conf();
    if (ret) {
        log_err("failed to load conf");
        return ret;
    }
    srand(time(NULL));
    return 0;
}


static int klnk_stat(fuse_ino_t ino, struct stat *stbuf)
{
    stbuf->st_ino = ino;
    switch (ino) {
    case 1:
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        break;
    case 2:
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(KLNK_INTERFACE);
        break;
    default:
        return -1;
    }
    return 0;
}


static void klnk_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    struct stat stbuf;
    (void) fi;

    memset(&stbuf, 0, sizeof(stbuf));
    if (klnk_stat(ino, &stbuf) == -1)
        fuse_reply_err(req, ENOENT);
    else
        fuse_reply_attr(req, &stbuf, 1.0);
}


inline int klnk_check_request(vres_t *resource, klnk_request_t *req)
{
    resource->cls = req->cls;
    resource->key = req->key;
    vres_set_op(resource, req->op);
    vres_set_id(resource, req->id);
    vres_set_val1(resource, req->val1);
    vres_set_val2(resource, req->val2);
    if (resource->cls != VRES_CLS_TSK)
        resource->owner = vres_get_id(resource);
    else
        resource->owner = resource->key;
    switch (vres_get_op(resource)) {
#ifndef ENABLE_PGSAVE
    case VRES_OP_PGSAVE:
        return -EOK;
#endif
#ifndef ENABLE_TSKPUT
    case VRES_OP_TSKPUT:
        return -EOK;
#endif
    default:
        return 0;
    }
}


static void klnk_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    if (parent != 1 || strcmp(name, KLNK_INTERFACE) != 0)
        fuse_reply_err(req, ENOENT);
    else {
        struct fuse_entry_param e;

        memset(&e, 0, sizeof(e));
        e.ino = 2;
        e.attr_timeout = 1.0;
        e.entry_timeout = 1.0;
        klnk_stat(e.ino, &e.attr);
        fuse_reply_entry(req, &e);
    }
}


static void klnk_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)fi;

    if (ino != 1)
        fuse_reply_err(req, ENOTDIR);
    else {
        struct dirbuf b;

        memset(&b, 0, sizeof(b));
        dirbuf_add(req, &b, ".", 1);
        dirbuf_add(req, &b, "..", 1);
        dirbuf_add(req, &b, KLNK_INTERFACE, 2);
        reply_buf_limited(req, b.p, b.size, off, size);
        free(b.p);
    }
}


static void klnk_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    if (ino != 2)
        fuse_reply_err(req, EISDIR);
    else if ((fi->flags & O_ACCMODE) != O_RDWR)
        fuse_reply_err(req, EACCES);
    else
        fuse_reply_open(req, fi);
}


static void klnk_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi)
{
    int ret;
    vres_arg_t arg;
    vres_t resource;
    vres_arg_t *parg = &arg;
    vres_t *pres = &resource;
    klnk_request_t *preq = (klnk_request_t *)buf;

    (void) fi;
    (void) ino;
    assert(size == sizeof(klnk_request_t));
    ret = klnk_check_request(pres, preq);
    if (ret < 0) {
        if (ret == -EOK) {
            log_klnk_write(pres, "finished (op=%s)", log_get_op(vres_get_op(pres)));
            assert(!vres_can_expose(pres));
        }
        goto err;
    }
    log_klnk_write(pres, ">-- write@start --<");
#ifdef ENABLE_BARRIER
    klnk_barrier_wait_timeout(pres, KLNK_BARRIER_TIMEOUT);
#endif
    klnk_mutex_lock(pres);
    ret = klnk_rpc_get(pres, preq, parg);
    if (ret) {
        if (-EAGAIN == ret)
            goto wait;
        else
            goto err;
    } else {
        ret = klnk_rpc(parg);
        if (ret)
            goto err;
    }
wait:
    ret = klnk_rpc_wait(parg);
    klnk_rpc_put(parg);
err:
    klnk_mutex_unlock(pres);
    if (!ret || -EOK == ret) {
        log_klnk_write(pres, ">-- write@end --<");
        fuse_reply_write(req, size);
    } else {
        log_klnk_write(pres, ">-- write@end (ret=%s) --<", log_get_err(ret));
        fuse_reply_err(req, -ret);
    }
}


static const struct fuse_lowlevel_ops klnk_oper = {
    .lookup  = klnk_lookup,
    .getattr = klnk_getattr,
    .readdir = klnk_readdir,
    .open    = klnk_open,
    .write   = klnk_write,
};


void usage()
{
    printf("usage: klnk [address] [mountpoint]\n");
    printf("> address: the IP address of master\n");
    printf("> mountpoint: the path of klnk file system\n");
}


int main(int argc, char *argv[])
{
    struct fuse_session *se;
    struct fuse_args fuse_args;
    struct fuse_cmdline_opts opts;
    struct fuse_loop_config config;
    const int nr_args = 3;
    char *args[nr_args];
    int ret = -1;

    umask(0);
    if (argc != 3) {
        usage();
        return -1;
    }
    if (klnk_init(argv[1])) {
        log_ln("failed to initialize");
        return -1;
    }
    log_ln("node     : %s", node_name);
    log_ln("mds      : %s", mds_addr);
    log_ln("mount    : %s", argv[2]);
#if MANAGER_TYPE == STATIC_MANAGER
    log_ln("managers : %d", nr_resource_managers);
#endif
#ifdef UNSHARE
    log_ln("unshare  : 1");
    if (unshare(CLONE_NEWIPC) != 0) {
        log_ln("failed to unshare");
        return -1;
    }
#endif
    args[0] = argv[0];
    args[1] = argv[2];
    args[2] = "-f";
    fuse_args.argc = nr_args;
    fuse_args.argv = args;
    fuse_args.allocated = 0;
    if (fuse_parse_cmdline(&fuse_args, &opts) != 0)
        return 1;
    se = fuse_session_new(&fuse_args, &klnk_oper, sizeof(klnk_oper), NULL);
    if (se == NULL)
        goto err_out1;
    if (fuse_set_signal_handlers(se) != 0)
        goto err_out2;
    if (fuse_session_mount(se, opts.mountpoint) != 0)
        goto err_out3;
    fuse_daemonize(opts.foreground);
    config.clone_fd = opts.clone_fd;
    config.max_idle_threads = opts.max_idle_threads;
    ret = fuse_session_loop_mt(se, &config);
err_out3:
    fuse_remove_signal_handlers(se);
err_out2:
    fuse_session_destroy(se);
err_out1:
    free(opts.mountpoint);
    fuse_opt_free_args(&fuse_args);
    return ret ? 1 : 0;
}
