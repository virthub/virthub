/* metadata.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "metadata.h"

#define CURSOR_MAX  65536
#define CURSOR_SIZE 32

int metadata_stat = 0;
redisContext *metadata_ctx = NULL;

void vres_metadata_init()
{
    if (metadata_stat & VRES_STAT_INIT)
        return;

    metadata_ctx = redisConnect(master_addr, MDS_PORT);
     if (!metadata_ctx || metadata_ctx->err) {
        if (metadata_ctx) {
            log_err("failed to connect, %s", metadata_ctx->errstr);
            redisFree(metadata_ctx);
        } else
            log_err("no contex");
        exit(1);
    }
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
    int ret;
    redisReply *reply;

    if (0 == len)
        return 0;
    ret = vres_metadata_check_path(path);
    if (ret < 0) {
        log_err("invalid path");
        return ret;
    }
    reply = redisCommand(metadata_ctx, "GET %s", path);
    if (reply->len == len) {
        memcpy(buf, reply->str, reply->len);
        ret = 0;
    } else {
        log_err("invalid reply");
        ret = -EINVAL;
    }
    freeReplyObject(reply);
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
        log_err("invalid path");
        return ret;
    }
    reply = redisCommand(metadata_ctx, "SET %s %b", path, buf, len);
    freeReplyObject(reply);
    return 0;
}


int vres_metadata_exists(char *path)
{
    int ret = 0;
    redisReply *reply;

    if (vres_metadata_check_path(path) < 0) {
        log_err("invalid path");
        return ret;
    }
    reply = redisCommand(metadata_ctx, "EXISTS %s", path);
    if ((REDIS_REPLY_INTEGER == reply->type) && (1 == reply->integer))
        ret = 1;
    freeReplyObject(reply);
    return ret;
}


int vres_metadata_create(char *path, char *buf, int len)
{
    int ret;
    char *tmp = NULL;

    if (vres_metadata_exists(path))
        return -EEXIST;
    ret = vres_metadata_write(path, buf, len);
    if (ret) {
        log_err("failed to write");
        return ret;
    }
    tmp = (char *)malloc(len);
    if (!tmp) {
        log_err("no memory");
        return -ENOMEM;
    }
    ret = vres_metadata_read(path, tmp, len);
    if (ret) {
        log_err("failed to read");
        goto out;
    }
    if (memcmp(tmp, buf, len))
        ret = -EEXIST;
out:
    free(tmp);
    return ret;
}


int vres_metadata_remove(char *path)
{
    redisReply *reply;
    int ret = vres_metadata_check_path(path);

    if (ret < 0) {
        log_err("invalid path");
        return ret;
    }
    reply = redisCommand(metadata_ctx, "DEL %s", path);
    freeReplyObject(reply);
    return 0;
}


int vres_metadata_count(char *path)
{
    int count = 0;
    redisReply *reply;
    char cursor[CURSOR_SIZE];
    int ret = vres_metadata_check_path(path);

    if (ret < 0) {
        log_err("invalid path");
        return ret;
    }
    strcpy(cursor, "0");
    do {
        reply = redisCommand(metadata_ctx, "SCAN %s MATCH %s/*", cursor, path);
        if (REDIS_REPLY_ARRAY == reply->type) {
            if (2 == reply->elements) {
                strncpy(cursor, reply->element[0]->str, CURSOR_SIZE);
                count += reply->element[1]->elements;
            } else {
                log_err("failed to get reply");
                freeReplyObject(reply);
                return -EINVAL;
            }
        }
        freeReplyObject(reply);
        if (count > CURSOR_MAX) {
            log_err("too much elements");
            return -EINVAL;
        }
    } while (strcmp(cursor, "0"));

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
        log_err("invalid path");
        return 0;
    }
    off = strlen(path) + 1;
    strcpy(cursor, "0");
    do {
        reply = redisCommand(metadata_ctx, "SCAN %s MATCH %s/*", cursor, path);
        if (REDIS_REPLY_ARRAY == reply->type) {
            if (2 == reply->elements) {
                int elements = reply->element[1]->elements;
                struct redisReply **element = reply->element[1]->element;

                count += elements;
                if (count > CURSOR_MAX) {
                    log_err("too much elements");
                    freeReplyObject(reply);
                    return 0;
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
                log_err("failed to get reply");
                freeReplyObject(reply);
                return 0;
            }
        }
        freeReplyObject(reply);
    } while (strcmp(cursor, "0"));

    return val;
}
