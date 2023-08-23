#include "node.h"

int node_stat = 0;
char node_path[NODE_PATH_MAX];
redisContext *node_ctx = NULL;

void *vres_node_create(void *arg);

void vres_node_init()
{
    int ret;
    pthread_t tid;
    pthread_attr_t attr;

    if (node_stat)
        return;
    node_ctx = redisConnect(mds_addr, MDS_PORT);
    if (!node_ctx || node_ctx->err) {
        if (node_ctx) {
            log_err("failed to connect to %s (port=%d), %s", mds_addr, MDS_PORT, node_ctx->errstr);
            redisFree(node_ctx);
        } else
            log_err("no context");
        exit(1);
    }
    if (strlen(node_name) + strlen(NODE_PREFIX) >= NODE_PATH_MAX) {
        log_err("invalid node name");
        exit(1);
    }
    sprintf(node_path, "%s%s", NODE_PREFIX, node_name);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    ret = pthread_create(&tid, &attr, vres_node_create, NULL);
    pthread_attr_destroy(&attr);
    if (ret) {
        log_err("failed to create thread");
        exit(1);
    }
    node_stat = 1;
    log_node_init(node_path);
}


int vres_node_list(struct in_addr **nodes)
{
    int i;
    int total;
    int ret = 0;
    redisReply *reply;
    struct in_addr *list;

    if (!node_stat) {
        log_err("invalid state");
        return -EINVAL;
    }
    *nodes = NULL;
    reply = redisCommand(node_ctx, "KEYS %s*", NODE_PREFIX);
    if ((REDIS_REPLY_ERROR == reply->type)
        || (reply->type != REDIS_REPLY_ARRAY)) {
        log_err("invalid reply");
        return -EINVAL;
    }
    total = reply->elements;
    if (!total)
        goto out;
    list = (struct in_addr *)malloc(total * sizeof(struct in_addr));
    if (!list) {
        log_err("no memory");
        ret = -ENOMEM;
        goto out;
    }
    for (i = 0; i < total; i++) {
        if ((strlen(reply->element[i]->str) >= NODE_PATH_MAX) ||
            strncmp(reply->element[i]->str, NODE_PREFIX, strlen(NODE_PREFIX))) {
            log_err("invalid node");
            ret = -EINVAL;
        } else {
            char *pend;
            char node[NODE_NAME_SIZE];

            strcpy(node, &reply->element[i]->str[strlen(NODE_PREFIX)]);
            list[i].s_addr = strtoul(node, &pend, 16);
            if (node == pend) {
                log_err("failed to get address");
                ret = -EINVAL;
            }
        }
        if (ret) {
            free(list);
            goto out;
        }
    }
    ret = total;
    *nodes = list;
    log_node_list(list, total);
out:
    freeReplyObject(reply);
    return ret;
}


void vres_node_update()
{
    char timeout[64];
    redisReply *reply;
    time_t t = time(NULL);

    reply = redisCommand(node_ctx, "SET %s %s", node_path, "");
    freeReplyObject(reply);

    sprintf(timeout, "%lu", t + NODE_TIMEOUT);
    reply = redisCommand(node_ctx, "EXPIREAT %s %s", node_path, timeout);
    freeReplyObject(reply);
    log_node_update(node_path);
}


void *vres_node_create(void *arg)
{
    while (1) {
        vres_node_update();
        sleep(NODE_TIMEOUT / 2);
    }
}
