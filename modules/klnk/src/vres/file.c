#include "file.h"

rbtree_t vres_file_dtree;
rbtree_t vres_file_ftree;
pthread_rwlock_t vres_file_dlock;
pthread_rwlock_t vres_file_flock;

int vres_file_stat = 0;
int vres_file_mkdir(char *path);
int vres_file_seek(vres_file_t *filp, long int offset, int origin);
int vres_file_write(const void *ptr, size_t size, size_t count, vres_file_t *filp);

int vres_file_compare(const void *n0, const void *n1)
{
    return strcmp(n0, n1);
}


void vres_file_init()
{
    if (vres_file_stat != FILE_STAT_INIT) {
        rbtree_new(&vres_file_dtree, vres_file_compare);
        rbtree_new(&vres_file_ftree, vres_file_compare);
        pthread_rwlock_init(&vres_file_dlock, NULL);
        pthread_rwlock_init(&vres_file_flock, NULL);
        vres_file_stat = FILE_STAT_INIT;
        vres_file_mkdir(FILE_ROOT_PATH);
    }
}


int vres_file_dirname(char *path, char *dirname)
{
    int lst = strlen(path) - 1;

    if (lst < 0) {
        log_warning("invalid path");
        return -1;
    }
    if (FILE_SEPARATOR == path[lst])
        lst--;
    while ((lst >= 0) && (path[lst] != FILE_SEPARATOR))
        lst--;
    if (lst < 0) {
        log_warning("invalid path");
        return -1;
    }
    memcpy(dirname, path, lst + 1);
    dirname[lst + 1] = '\0';
    return 0;
}


static inline vres_file_name_t * vres_file_alloc_name(const char *path)
{
    vres_file_name_t *name;

    name = (vres_file_name_t *)malloc(sizeof(vres_file_name_t));
    if (name)
        strncpy(name->name, path, FILE_PATH_MAX);
    return name;
}


static inline vres_file_t *vres_file_alloc_file(const char *path)
{
    vres_file_t *file;

    file = (vres_file_t *)malloc(sizeof(vres_file_t));
    if (!file)
        return NULL;
    file->f_pos = 0;
    file->f_size = 0;
    file->f_buf = NULL;
    strncpy(file->f_path, path, FILE_PATH_MAX);
    return file;
}


static inline int vres_file_calloc(vres_file_t *filp, size_t size)
{
    if (filp && (size <= FILE_BUF_SIZE)) {
        char *buf = NULL;

        if (size > 0) {
            buf = calloc(1, size);
            if (!buf) {
                log_warning("no memory");
                return -ENOMEM;
            }
        }
        if (filp->f_buf)
            free(filp->f_buf);
        filp->f_pos = 0;
        filp->f_size = size;
        filp->f_buf = buf;
        return 0;
    } else {
        log_warning("invalid file, filp=0x%lx, size=%ld", (unsigned long)filp, size);
        return -EINVAL;
    }
}


static inline vres_file_t *vres_file_lookup_file(const char *path)
{
    vres_file_t *file = NULL;
    rbtree_node_t *node = NULL;

    pthread_rwlock_rdlock(&vres_file_flock);
    if (!rbtree_find(&vres_file_ftree, path, &node))
        file = tree_entry(node, vres_file_t, f_node);
    pthread_rwlock_unlock(&vres_file_flock);
    return file;
}


static inline vres_file_t *vres_file_create_file(const char *path)
{
    vres_file_t *file;

    file = vres_file_alloc_file(path);
    if (!file)
        return NULL;
    pthread_rwlock_wrlock(&vres_file_flock);
    rbtree_insert(&vres_file_ftree, file->f_path, &file->f_node);
    pthread_rwlock_unlock(&vres_file_flock);
    return file;
}


static inline void vres_file_delete_file(const char *path)
{
    vres_file_t *file = NULL;
    rbtree_node_t *node = NULL;

    pthread_rwlock_wrlock(&vres_file_flock);
    if (!rbtree_find(&vres_file_ftree, path, &node)) {
        file = tree_entry(node, vres_file_t, f_node);
        rbtree_remove_node(&vres_file_ftree, node);
    }
    pthread_rwlock_unlock(&vres_file_flock);
    if (file)
        free(file);
    log_file_delete_file(path);
}


static inline vres_file_dir_t *vres_file_alloc_dir(const char *path)
{
    vres_file_dir_t *dir;

    dir = (vres_file_dir_t *)malloc(sizeof(vres_file_dir_t));
    if (!dir)
        return NULL;
    dir->d_count = 0;
    rbtree_new(&dir->d_tree, vres_file_compare);
    strncpy(dir->d_path, path, FILE_PATH_MAX);
    pthread_rwlock_init(&dir->d_lock, NULL);
    return dir;
}


static inline vres_file_dir_t *vres_file_lookup_dir(const char *path)
{
    void *key = NULL;
    size_t len = strlen(path);
    rbtree_node_t *node = NULL;
    vres_file_dir_t *dir = NULL;
    char dirname[FILE_PATH_MAX] = {0};

    if (FILE_SEPARATOR == path[len - 1])
        key = (void *)path;
    else {
        if (FILE_PATH_MAX - 1 == len) {
            log_warning("invalid path");
            return NULL;
        }
        memcpy(dirname, path, len);
        dirname[len] = FILE_SEPARATOR;
        key = (void *)dirname;
    }
    pthread_rwlock_rdlock(&vres_file_dlock);
    if (!rbtree_find(&vres_file_dtree, key, &node))
        dir = tree_entry(node, vres_file_dir_t, d_node);
    pthread_rwlock_unlock(&vres_file_dlock);
    return dir;
}


static inline vres_file_dir_t *vres_file_create_dir(const char *path)
{
    vres_file_dir_t *dir;

    dir = vres_file_alloc_dir(path);
    if (!dir)
        return NULL;
    pthread_rwlock_wrlock(&vres_file_dlock);
    rbtree_insert(&vres_file_dtree, dir->d_path, &dir->d_node);
    pthread_rwlock_unlock(&vres_file_dlock);
    return dir;
}


static inline void vres_file_delete_dir(const char *path)
{
    pthread_rwlock_wrlock(&vres_file_dlock);
    rbtree_remove(&vres_file_dtree, (void *)path);
    pthread_rwlock_unlock(&vres_file_dlock);
}


static inline int vres_file_push(char *path)
{
    char *filename;
    char dirname[FILE_PATH_MAX];

    if (vres_file_dirname(path, dirname)) {
        log_warning("invalid path, path=%s", path);
        return -1;
    }
    filename = path + strlen(dirname);
    if ((strlen(filename) > 0) && (strlen(path) < FILE_PATH_MAX)) {
        vres_file_dir_t *dir;
        vres_file_name_t *name = vres_file_alloc_name(filename);

        if (!name) {
            log_warning("failed to allocate, path=%s", path);
            return -1;
        }
        dir = vres_file_lookup_dir(dirname);
        if (!dir) {
            log_warning("failed to find directory %s", dirname);
            free(name);
            return -1;
        }
        pthread_rwlock_wrlock(&dir->d_lock);
        rbtree_insert(&dir->d_tree, name->name, &name->node);
        dir->d_count++;
        pthread_rwlock_unlock(&dir->d_lock);
        log_file_dir_push(dirname, filename);
    } else {
        log_warning("invalid path, path=%s", path);
        return -1;
    }
    return 0;
}


static inline int vres_file_pop(char *path)
{
    size_t len;
    char *filename;
    char dirname[FILE_PATH_MAX];

    if (vres_file_dirname(path, dirname)) {
        log_warning("invalid path, path=%s", path);
        return -1;
    }
    filename = path + strlen(dirname);
    len = strlen(filename);
    if ((len > 0) && (len + filename - path < FILE_PATH_MAX)) {
        vres_file_dir_t *dir = vres_file_lookup_dir(dirname);

        if (!dir)
            return -1;
        pthread_rwlock_wrlock(&dir->d_lock);
        if (dir->d_count > 0) {
            rbtree_node_t *node = NULL;

            if (!rbtree_find(&dir->d_tree, filename, &node)) {
                vres_file_name_t *name = tree_entry(node, vres_file_name_t, node);

                rbtree_remove_node(&dir->d_tree, node);
                dir->d_count--;
                free(name);
            }
        }
        pthread_rwlock_unlock(&dir->d_lock);
    } else {
        log_warning("invalid path, path=%s", path);
        return -1;
    }
    return 0;
}


int vres_file_size(vres_file_t *filp)
{
    if (!filp)
        return -EINVAL;
    else
        return filp->f_size;
}


int vres_file_truncate(vres_file_t *filp, off_t length)
{
    if ((length < 0) || !filp)
        return -EINVAL;
    if (!length) {
        free(filp->f_buf);
        filp->f_buf = NULL;
    } else
        filp->f_buf = realloc(filp->f_buf, length);
    filp->f_size = length;
    return 0;
}


int vres_file_write(const void *ptr, size_t size, size_t count, vres_file_t *filp)
{
    size_t total;
    size_t buflen;

    if (!filp)
        return -EINVAL;
    total = size * count;
    if (!total)
        return 0;
    buflen = filp->f_pos + total;
    if (buflen > filp->f_size) {
        filp->f_buf = realloc(filp->f_buf, buflen);
        if (!filp->f_buf) {
            filp->f_size = 0;
            filp->f_pos = 0;
            return 0;
        }
        filp->f_size = buflen;
    }
    memcpy(filp->f_buf + filp->f_pos, (char *)ptr, total);
    filp->f_pos += total;
    return count;
}


int vres_file_read(void *ptr, size_t size, size_t count, vres_file_t *filp)
{
    size_t total;

    if (!filp)
        return -EINVAL;

    total = size * count;
    if (!total)
        return 0;

    if (filp->f_pos + total > filp->f_size) {
        total = filp->f_size - filp->f_pos;
        if (!total)
            return 0;
        total -= total % size;
        count = total / size;
    }
    memcpy((char *)ptr, filp->f_buf + filp->f_pos, total);
    filp->f_pos += total;
    return count;
}


int vres_file_seek(vres_file_t *filp, long int offset, int origin)
{
    long int pos;

    if ((offset < 0) || !filp)
        return -EINVAL;

    switch (origin) {
    case SEEK_SET:
        pos = offset;
        break;
    case SEEK_END:
        pos = filp->f_size + offset;
        break;
    case SEEK_CUR:
        pos = filp->f_pos + offset;
        break;
    default:
        return -EINVAL;
    }
    if ((pos > filp->f_size) || (pos < 0))
        return -EINVAL;
    filp->f_pos = pos;
    return 0;
}


vres_file_t *vres_file_open(char *path, const char *mode)
{
    vres_file_t *file;
    size_t len = strlen(path);

    if (vres_file_stat != FILE_STAT_INIT) {
        log_warning("invalid state");
        return NULL;
    }
    if ((len >= FILE_PATH_MAX) || (FILE_SEPARATOR == path[len - 1])) {
        log_warning("invalid path");
        return NULL;
    }
    file = vres_file_lookup_file(path);
    if (!strcmp(mode, "w")) {
        if (!file) {
            if (!vres_file_push(path)) {
                file = vres_file_create_file(path);
                if (!file) {
                    log_warning("failed to create, path=%s", path);
                    vres_file_pop(path);
                    return NULL;
                }
            } else {
                log_warning("failed, path=%s", path);
                return NULL;
            }
        }

        if (file && file->f_buf) {
            free(file->f_buf);
            file->f_buf = NULL;
            file->f_size = 0;
        }
    }
    if (file)
        file->f_pos = 0;
    return file;
}


int vres_file_access(char *path, int mode)
{
    size_t len = strlen(path);

    if (vres_file_stat != FILE_STAT_INIT) {
        log_warning("invalid state");
        return -1;
    }
    if (!len || (len >= FILE_PATH_MAX)) {
        log_warning("invalid path");
        return -1;
    }
    if (F_OK == mode)
        if (vres_file_lookup_file(path))
            return 0;
    return -1;
}


bool vres_file_is_dir(char *path)
{
    return vres_file_lookup_dir(path) != NULL;
}


bool vres_file_is_root(char *path)
{
    return strcmp(path, FILE_ROOT_PATH) == 0;
}


int vres_file_mkdir(char *path)
{
    char *name;
    size_t len = strlen(path);
    char dirname[FILE_PATH_MAX] = {0};

    if (vres_file_stat != FILE_STAT_INIT) {
        log_warning("invalid state");
        return -1;
    }
    if (!len || (len >= FILE_PATH_MAX)) {
        log_warning("invalid path");
        return -1;
    }
    if (!vres_file_is_root(path)) {
        vres_file_dirname(path, dirname);
        if (!vres_file_is_root(dirname)) {
            if (!vres_file_is_dir(dirname)) {
                if (vres_file_mkdir(dirname) < 0) {
                    log_warning("faile to create, path=%s", path);
                    return -1;
                }
            }
        }
    }
    if (FILE_SEPARATOR == path[len - 1])
        name = path;
    else {
        if (FILE_PATH_MAX - 1 == len) {
            log_warning("invalid path");
            return -1;
        }
        memcpy(dirname, path, len);
        dirname[len] = FILE_SEPARATOR;
        name = dirname;
    }
    if (!vres_file_is_root(path)) {
        if (vres_file_push(name)) {
            log_warning("failed to push, path=%s", name);
            return -1;
        }
    }
    if (!vres_file_create_dir(name)) {
        log_warning("failed to create, path=%s", name);
        if (!vres_file_is_root(path))
            vres_file_pop(name);
        return -1;
    }
    log_file_mkdir(path);
    return 0;
}


int vres_file_do_handle_file(rbtree_node_t *node, vres_file_callback_t func, char *path, void *arg)
{
    int ret;
    char name[FILE_PATH_MAX];

    if (!node)
        return 0;
    if (node->right) {
        ret = vres_file_do_handle_file(node->right, func, path, arg);
        if (ret)
            return ret;
    }
    if (node->left) {
        ret = vres_file_do_handle_file(node->left, func, path, arg);
        if (ret)
            return ret;
    }
    strcpy(name, path);
    strcat(name, (char *)node->key);
    if (vres_file_is_dir(name)) {
        vres_file_dir_t *dir = vres_file_lookup_dir(name);

        if (dir)
            return vres_file_do_handle_file(dir->d_tree.root, func, dir->d_path, arg);
        else {
            log_warning("invalid path");
            return -1;
        }
    } else {
        ret = func(name, arg);
        log_file_handle_file(name);
        return ret;
    }
}


int vres_file_handle_file(vres_file_callback_t func, char *path, void *arg)
{
    vres_file_dir_t *dir;

    if (vres_file_stat != FILE_STAT_INIT) {
        log_warning("invalid state");
        return 0;
    }
    dir = vres_file_lookup_dir(path);
    if (dir)
        return vres_file_do_handle_file(dir->d_tree.root, func, dir->d_path, arg);

    return 0;
}


int vres_file_do_handle_dir(rbtree_node_t *node, vres_file_callback_t func, char *path, void *arg)
{
    int ret;
    char name[FILE_PATH_MAX];

    if (!node)
        return 0;
    if (node->right) {
        ret = vres_file_do_handle_dir(node->right, func, path, arg);
        if (ret)
            return ret;
    }
    if (node->left) {
        ret = vres_file_do_handle_dir(node->left, func, path, arg);
        if (ret)
            return ret;
    }
    strcpy(name, path);
    strcat(name, (char *)node->key);
    if (vres_file_is_dir(name)) {
        vres_file_dir_t *dir = vres_file_lookup_dir(name);

        if (dir) {
            ret = vres_file_do_handle_dir(dir->d_tree.root, func, dir->d_path, arg);
            if (ret)
                return ret;
            ret = func(name, arg);
            log_file_handle_dir(name);
            return ret;
        } else {
            log_warning("invalid path");
            return -1;
        }
    }
    return 0;
}


int vres_file_handle_dir(vres_file_callback_t func, char *path, void *arg)
{
    vres_file_dir_t *dir;

    if (vres_file_stat != FILE_STAT_INIT) {
        log_warning("invalid state");
        return 0;
    }
    dir = vres_file_lookup_dir(path);
    if (dir)
        return vres_file_do_handle_dir(dir->d_tree.root, func, dir->d_path, arg);
    return 0;
}


int vres_file_release_dir(vres_file_dir_t *dir)
{
    int ret = 0;

    pthread_rwlock_wrlock(&dir->d_lock);
    while (dir->d_count > 0) {
        char *name;
        size_t len;
        rbtree_node_t *node;
        vres_file_name_t *ptr;
        char path[FILE_PATH_MAX];

        node = dir->d_tree.root;
        if (!node) {
            log_warning("invalid directory");
            ret = -1;
            goto out;
        }
        name = (char *)node->key;
        len = strlen(name);
        if (!len || (len >= FILE_PATH_MAX)) {
            log_warning("invalid path");
            ret = -1;
            goto out;
        }
        ptr = tree_entry(node, vres_file_name_t, node);
        sprintf(path, "%s%s", dir->d_path, name);
        if (FILE_SEPARATOR == name[len - 1]) {
            if (vres_file_rmdir(path)) {
                log_warning("failed to remove %s", path);
                ret = -1;
                goto out;
            }
        } else {
            vres_file_delete_file(path);
            rbtree_remove_node(&dir->d_tree, node);
            dir->d_count--;
            free(ptr);
        }
    }
    vres_file_delete_dir(dir->d_path);
out:
    pthread_rwlock_unlock(&dir->d_lock);
    if (!ret)
        free(dir);
    return ret;
}


bool vres_file_is_empty_dir(char *path)
{
    vres_file_dir_t *dir = vres_file_lookup_dir(path);

    if (dir && (dir->d_count > 0))
        return false;
    else
        return true;
}


int vres_file_rmdir(char *path)
{
    vres_file_dir_t *dir;

    if (vres_file_stat != FILE_STAT_INIT) {
        log_warning("invalid state");
        return -1;
    }
    dir = vres_file_lookup_dir(path);
    if (!dir) {
        log_warning("no directory, path=%s", path);
        return -1;
    }
    if (vres_file_pop(dir->d_path)) {
        log_warning("failed to remove, path=%s", path);
        return -1;
    }
    if (vres_file_release_dir(dir)) {
        log_warning("failed to release, path=%s", path);
        return -1;
    }
    log_file_rmdir(path);
    return 0;
}


int vres_file_remove(char *path)
{
    size_t len;

    if (vres_file_stat != FILE_STAT_INIT) {
        log_warning("invalid state");
        return -1;
    }
    len = strlen(path);
    if (!len || (len >= FILE_PATH_MAX)) {
        log_warning("invalid path");
        return -1;
    }
    if (FILE_SEPARATOR == path[len - 1]) {
        log_warning("invalid path");
        return -1;
    } else {
        if (vres_file_pop(path))
            return -1;
        else
            vres_file_delete_file(path);
    }
    return 0;
}


vres_file_entry_t *vres_file_get_entry(char *path, size_t size, int flags)
{
    int prot;
    vres_file_entry_t *entry;
    vres_file_t *filp = NULL;

    entry = (vres_file_entry_t *)malloc(sizeof(vres_file_entry_t));
    if (!entry)
        return NULL;
    if (flags & FILE_RDWR) {
        filp = vres_file_open(path, "r+");
        prot = PROT_READ | PROT_WRITE;
    } else if (flags & FILE_RDONLY) {
        filp = vres_file_open(path, "r");
        prot = PROT_READ;
    } else
        goto out;
    if (!filp) {
        if (flags & FILE_CREAT) {
            filp = vres_file_open(path, "w");
            if (vres_file_calloc(filp, size))
                goto out;
        }
        if (!filp)
            goto out;
    }
    if (size > filp->f_size) {
        if (prot & PROT_WRITE) {
            filp->f_buf = realloc(filp->f_buf, size);
            if (filp->f_buf) {
                memset(&filp->f_buf[filp->f_size], 0, size - filp->f_size);
                filp->f_size = size;
            } else {
                filp->f_size = 0;
                filp->f_pos = 0;
            }
        } else
            goto out;
    }
    if (!size)
        size = filp->f_size;
    if (!filp->f_buf && (size > 0))
        goto out;
    entry->file = filp;
    entry->size = size;
    entry->desc = &filp->f_buf;
    return entry;
out:
    free(entry);
    return NULL;
}


void vres_file_put_entry(vres_file_entry_t *entry)
{
    if (entry) {
        vres_file_close(entry->file);
        free(entry);
    }
}


int vres_file_append(vres_file_entry_t *entry, void *buf, size_t size)
{
    vres_file_t *filp = entry->file;

    vres_file_seek(filp, 0, SEEK_END);
    if (vres_file_write(buf, 1, size, filp) != size)
        return -EIO;
    else
        return 0;
}


void vres_file_close(vres_file_t *filp)
{
}
