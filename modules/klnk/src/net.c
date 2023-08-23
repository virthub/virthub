#include "net.h"

static inline int klnk_bind(struct sockaddr_in *addr)
{
    int ret;
    int optval = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if(fd < 0) {
        log_warning("failed to create socket");
        return fd;
    }
    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof (optval));
    if (ret < 0) {
        log_warning("failed to set socket");
        goto out;
    }
#ifdef SO_REUSEPORT
    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (char *)&optval, sizeof (optval));
    if (ret < 0) {
        log_warning("failed to reuse port");
        goto out;
    }
#endif
    ret = bind(fd, (struct sockaddr *)addr, sizeof(struct sockaddr_in));
out:
    if (ret < 0) {
        perror("klnk");
        close(fd);
        return ret;
    }
    return fd;
}


void klnk_set_addr(vres_addr_t address, int port, klnk_addr_t *p)
{
    memset(p, 0, sizeof(klnk_addr_t));
    p->sin_family = AF_INET;
    p->sin_port = htons(port);
    p->sin_addr = address;
}


klnk_desc_t klnk_listen(vres_addr_t address, int port)
{
    klnk_addr_t addr;
    klnk_desc_t desc;

    klnk_set_addr(address, port, &addr);
    desc = klnk_bind(&addr);
    if (desc < 0) {
        log_warning("failed to bind");
        return -EFAULT;
    }
    if (listen(desc, LEN_LISTEN_QUEUE)) {
        log_warning("failed to listen");
        return -EFAULT;
    }
    return desc;
}


klnk_desc_t klnk_connect(vres_addr_t address, int port)
{
    int ret;
    klnk_addr_t addr;
    klnk_desc_t desc;

    klnk_set_addr(address, port, &addr);
    desc = socket(AF_INET, SOCK_STREAM, 0);
    if (desc < 0) {
        log_warning("no entry");
        return -ENOENT;
    }
    ret = connect(desc, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (ret < 0) {
        log_warning("failed to connect to %s, err=%s", inet_ntoa(addr.sin_addr), strerror(errno));
        klnk_close(desc);
        return -EFAULT;
    }
    return desc;
}


int klnk_send(klnk_desc_t desc, char *buf, size_t size)
{
    if (send(desc, buf, size, 0) != size) {
        log_warning("failed to send");
        return -EIO;
    } else
        return 0;
}


int klnk_recv(klnk_desc_t desc, char *buf, size_t size)
{
    int ret;
    size_t len = size;

    while (len > 0) {
        ret = recv(desc, buf, len, MSG_WAITALL);
        if (ret < 0) {
            log_warning("failed to receive");
            return ret;
        }
        len -= ret;
        buf += ret;
    }
    return 0;
}


klnk_desc_t klnk_accept(klnk_desc_t desc)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(struct sockaddr_in);

    return accept(desc, (struct sockaddr *)&addr, &len);
}


void klnk_close(klnk_desc_t desc)
{
    close(desc);
}
