#include "metadata.h"

#define CURSOR_MAX  65536
#define CURSOR_SIZE 32

int metadata_stat = 0;
pthread_mutex_t metadata_mutex;
redisContext *metadata_ctx = NULL;

void vres_metadata_init()
{
    if (metadata_stat & VRES_STAT_INIT)
        return;
    metadata_ctx = redisConnect(mds_addr, MDS_PORT);
    if (!metadata_ctx || metadata_ctx->err) {
        if (metadata_ctx) {
            log_warning("failed to connect, %s", metadata_ctx->errstr);
            redisFree(metadata_ctx);
        } else
            log_warning("invalid context");
        exit(1);
    }
    pthread_mutex_init(&metadata_mutex, NULL);
    metadata_stat |= VRES_STAT_INIT;
}


inline int vres_metadata_check_path(char *path)
{
    if (!(metadata_stat & VRES_STAT_INIT) || !path)
        return -EINVAL;
    else
        return 0;
}


int vres_metadata_read(char *path, char *buf, int len)
{
    int ret = -EINVAL;
    redisReply *reply;
    int cnt = METADATA_RETRY_MAX;

    if (0 == len)
        return 0;
    ret = vres_metadata_check_path(path);
    if (ret < 0) {
        log_warning("invalid path");
        return ret;
    }
    log_metadata_read(path);
    pthread_mutex_lock(&metadata_mutex);
    do {
        reply = redisCommand(metadata_ctx, "GET %s", path);
        if ((REDIS_REPLY_STRING == reply->type) && (reply->len == len)) {
            memcpy(buf, reply->str, reply->len);
            ret = 0;
            break;
        } else {
            cnt--;
            vres_sleep(METADATA_RETRY_INTV);
        }
    } while (cnt > 0);
    freeReplyObject(reply);
    pthread_mutex_unlock(&metadata_mutex);
    if (ret)
        log_warning("invalid reply");
    return ret;
}


int vres_metadata_write(char *path, char *buf, int len)
{
    int ret;
    redisReply *reply;

    if (0 == len)
        return 0;
    ret = vres_metadata_check_path(path);
    if (ret < 0) {
        log_warning("invalid path");
        return ret;
    }
    pthread_mutex_lock(&metadata_mutex);
    reply = redisCommand(metadata_ctx, "SET %s %b", path, buf, len);
    freeReplyObject(reply);
    pthread_mutex_unlock(&metadata_mutex);
    return 0;
}


bool vres_metadata_exists(char *path)
{
    int ret = false;
    redisReply *reply;

    if (vres_metadata_check_path(path) < 0) {
        log_warning("invalid path");
        return ret;
    }
    pthread_mutex_lock(&metadata_mutex);
    reply = redisCommand(metadata_ctx, "EXISTS %s", path);
    if ((REDIS_REPLY_INTEGER == reply->type) && (1 == reply->integer))
        ret = true;
    freeReplyObject(reply);
    pthread_mutex_unlock(&metadata_mutex);
    return ret;
}


bool vres_metadata_create(char *path, char *buf, int len)
{
    bool ret = false;
    redisReply *reply;

    pthread_mutex_lock(&metadata_mutex);
    reply = redisCommand(metadata_ctx, "SETNX %s %b", path, buf, len);
    if ((REDIS_REPLY_INTEGER == reply->type) && (1 == reply->integer))
        ret = true;
    freeReplyObject(reply);
    pthread_mutex_unlock(&metadata_mutex);
    return ret;
}


int vres_metadata_remove(char *path)
{
    redisReply *reply;
    int ret = vres_metadata_check_path(path);

    if (ret < 0) {
        log_warning("invalid path");
        return ret;
    }
    pthread_mutex_lock(&metadata_mutex);
    reply = redisCommand(metadata_ctx, "DEL %s", path);
    freeReplyObject(reply);
    pthread_mutex_unlock(&metadata_mutex);
    return 0;
}


int vres_metadata_count(char *path)
{
    int count = 0;
    redisReply *reply;
    char cursor[CURSOR_SIZE];
    int ret = vres_metadata_check_path(path);

    if (ret < 0) {
        log_warning("invalid path");
        return ret;
    }
    strcpy(cursor, "0");
    pthread_mutex_lock(&metadata_mutex);
    do {
        reply = redisCommand(metadata_ctx, "SCAN %s MATCH %s/*", cursor, path);
        if (REDIS_REPLY_ARRAY == reply->type) {
            if (2 == reply->elements) {
                strncpy(cursor, reply->element[0]->str, CURSOR_SIZE);
                count += reply->element[1]->elements;
            } else {
                log_warning("invalid reply");
                freeReplyObject(reply);
                count = -EINVAL;
                goto out;
            }
        }
        freeReplyObject(reply);
        if (count > CURSOR_MAX) {
            log_warning("too much elements");
            count = -EINVAL;
            goto out;
        }
    } while (strcmp(cursor, "0"));
out:
    pthread_mutex_unlock(&metadata_mutex);
    return count;
}


unsigned long vres_metadata_max(char *path)
{
    int i;
    size_t off;
    int count = 0;
    redisReply *reply;
    unsigned long val = 0;
    char cursor[CURSOR_SIZE];

    if (vres_metadata_check_path(path) < 0) {
        log_warning("invalid path");
        return 0;
    }
    off = strlen(path) + 1;
    strcpy(cursor, "0");
    pthread_mutex_lock(&metadata_mutex);
    do {
        reply = redisCommand(metadata_ctx, "SCAN %s MATCH %s/*", cursor, path);
        if (REDIS_REPLY_ARRAY == reply->type) {
            if (2 == reply->elements) {
                int elements = reply->element[1]->elements;
                struct redisReply **element = reply->element[1]->element;

                count += elements;
                if (count > CURSOR_MAX) {
                    log_warning("too much elements");
                    freeReplyObject(reply);
                    val = 0;
                    goto out;
                }
                for (i = 0; i < elements; i++) {
                    char *end;
                    unsigned long tmp;
                    char *start = element[i]->str + off;

                    tmp = strtoul(start, &end, 16);
                    if (start == end)
                        continue;
                    if (tmp > val)
                        val = tmp;
                }
                strncpy(cursor, reply->element[0]->str, CURSOR_SIZE);
            } else {
                log_warning("invalid reply");
                freeReplyObject(reply);
                val = 0;
                goto out;
            }
        }
        freeReplyObject(reply);
    } while (strcmp(cursor, "0"));
out:
    pthread_mutex_unlock(&metadata_mutex);
    return val;
}
