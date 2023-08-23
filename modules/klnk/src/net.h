#ifndef _NET_H
#define _NET_H

#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <vres.h>
#include "log.h"

typedef int_t klnk_desc_t;
typedef struct sockaddr_in klnk_addr_t;

void klnk_close(klnk_desc_t desc);
klnk_desc_t klnk_accept(klnk_desc_t desc);
klnk_desc_t klnk_listen(vres_addr_t address, int port);
klnk_desc_t klnk_connect(vres_addr_t address, int port);
int klnk_send(klnk_desc_t desc, char *buf, size_t size);
int klnk_recv(klnk_desc_t desc, char *buf, size_t size);

#endif
