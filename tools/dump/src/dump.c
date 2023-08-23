#include "dump.h"

int save(char *node, int id)
{
    int ret;

    ret = vres_request(node, VRES_CLS_TSK, id, VRES_OP_DUMP, id, 0);
    if (ret)
        log_err("failed to send request");
    return ret;
}


int clone(char *node, int id)
{
    int fd;
    int ret;
    ckpt_arg_t arg;

    memset(&arg, 0, sizeof(ckpt_arg_t));
    arg.id = id;
    if ('\0' != *node) {
        strcpy(arg.node, node);
        sprintf(arg.path, "%s/%s/ckpt/%lx/tsk", PATH_CACHE, node, (unsigned long)id);
    } else
        sprintf(arg.path, "%s/ckpt/%lx/tsk", PATH_CACHE, (unsigned long)id);

    fd = open(CKPT_DEV_FILE, O_RDONLY);
    if (fd < 0) {
        log_err("failed to open");
        return -1;
    }
    ret = ioctl(fd, CKPT_IOCTL_RESTORE, (int)&arg);
    close(fd);
    return ret;
}


int restore(char *node, int id)
{
    pid_t pid = 0;

#ifdef CLONE
    pid = fork();
#endif
    if (0 == pid) {
        int ret = vres_request(node, VRES_CLS_TSK, id, VRES_OP_RESTORE, getpid(), 0);

        if (ret) {
            log_err("failed to send request");
            return ret;
        }
        ret = clone(node, id);
        if (ret)
            log_err("failed to clone");
        return ret;
    }

    return 0;
}


void usage()
{
    log("usage: dump [-s|-r] [id] [-n] [node]\n");
    log("-s: save\n");
    log("-r: restore\n");
    log("-n: set node\n");
}


int main(int argc, char *argv[])
{
    char ch;
    int id = 0;
    int flg = 0;
    const int NODE_SIZE = 32;
    char node[NODE_SIZE];

    if ((argc != 5) && (argc != 3)) {
        usage();
        exit(-1);
    }

    node[0] = '\0';
    while ((ch = getopt(argc, argv, "s:r:n:h")) != -1) {
        switch(ch) {
        case 's':
            flg = 1;
        case 'r':
            id = atoi(optarg);
            break;
        case 'n':
            if (strlen(optarg) >= NODE_SIZE) {
                log_err("invalid node");
                exit(-1);
            }
            strcpy(node, optarg);
            break;
        default:
            usage();
            exit(-1);
        }
    }

    if (flg) {
        if (save(node, id) < 0) {
            log_err("failed to save %d", id);
            exit(-1);
        }
    } else {
        if (restore(node, id) < 0) {
            log_err("failed to restore %d", id);
            exit(-1);
        }
    }

    return 0;
}
