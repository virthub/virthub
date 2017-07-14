/* tmp.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "tmp.h"

rbtree tmp_dir_tree;
rbtree tmp_file_tree;
pthread_rwlock_t tmp_dir_lock;
pthread_rwlock_t tmp_file_lock;

int tmp_stat = 0;
int tmp_mkdir(char *path);
int tmp_seek(tmp_file_t *filp, long int offset, int origin);
int tmp_write(const void *ptr, size_t size, size_t count, tmp_file_t *filp);

void tmp_init()
{
	if (tmp_stat != TMP_STAT_INIT) {
		tmp_dir_tree = rbtree_create();
		tmp_file_tree = rbtree_create();
		pthread_rwlock_init(&tmp_dir_lock, NULL);
		pthread_rwlock_init(&tmp_file_lock, NULL);
		tmp_stat = TMP_STAT_INIT;
		tmp_mkdir(TMP_ROOT_PATH);
	}
}


static inline int tmp_compare(char *left, char *right)
{
	size_t l_len = strlen(left);
	size_t r_len = strlen(right);

	if (l_len < r_len)
		return -1;
	else if(l_len > r_len)
		return 1;
	else {
		int i;

		for (i = l_len - 1; i >= 0; i--) {
			if (left[i] == right[i])
				continue;
			else if (left[i] < right[i])
				return -1;
			else
				return 1;
		}

		return 0;
	}
}


int tmp_dirname(char *path, char *dirname)
{
	int lst = strlen(path) - 1;

	if (lst < 0) {
		log_err("failed");
		return -1;
	}

	if (TMP_SEPARATOR == path[lst])
		lst--;

	while ((lst >= 0) && (path[lst] != TMP_SEPARATOR))
		lst--;

	if (lst < 0) {
		log_err("failed");
		return -1;
	}

	memcpy(dirname, path, lst + 1);
	dirname[lst + 1] = '\0';
	return 0;
}


static inline tmp_name_t * tmp_alloc_name(const char *path)
{
	tmp_name_t *name;

	name = (tmp_name_t *)malloc(sizeof(tmp_name_t));
	if (name)
		strncpy(name->name, path, TMP_PATH_MAX);

	return name;
}


static inline tmp_file_t *tmp_alloc_file(const char *path)
{
	tmp_file_t *file;

	file = (tmp_file_t *)malloc(sizeof(tmp_file_t));
	if (!file)
		return NULL;

	file->f_pos = 0;
	file->f_size = 0;
	file->f_buf = NULL;
	strncpy(file->f_path, path, TMP_PATH_MAX);
	return file;
}


static inline int tmp_calloc(tmp_file_t *filp, size_t size)
{
	if (filp && (size <= TMP_BUF_SIZE)) {
		char *buf = NULL;

		if (size > 0) {
			buf = calloc(1, size);
			if (!buf)
				return -ENOMEM;
		}

		if (filp->f_buf)
			free(filp->f_buf);

		filp->f_pos = 0;
		filp->f_size = size;
		filp->f_buf = buf;
		return 0;
	}

	return -EINVAL;
}


static inline tmp_file_t *tmp_lookup_file(const char *path)
{
	tmp_file_t *file;

	pthread_rwlock_rdlock(&tmp_file_lock);
	file = rbtree_lookup(tmp_file_tree, (void *)path, tmp_compare);
	pthread_rwlock_unlock(&tmp_file_lock);
	return file;
}


static inline tmp_file_t *tmp_create_file(const char *path)
{
	tmp_file_t *file;

	file = tmp_alloc_file(path);
	if (!file)
		return NULL;

	pthread_rwlock_wrlock(&tmp_file_lock);
	rbtree_insert(tmp_file_tree, (void *)file->f_path, (void *)file, tmp_compare);
	pthread_rwlock_unlock(&tmp_file_lock);
	return file;
}


static inline void tmp_delete_file(const char *path)
{
	rbtree_node node;
	tmp_file_t *file = NULL;

	pthread_rwlock_wrlock(&tmp_file_lock);
	node = rbtree_lookup_node(tmp_file_tree, (void *)path, tmp_compare);
	if (node) {
		file = node->value;
		rbtree_delete_node(tmp_file_tree, node);
	}
	pthread_rwlock_unlock(&tmp_file_lock);
	if (file)
		free(file);
	log_tmp_delete_file(path);
}


static inline tmp_dir_t *tmp_alloc_dir(const char *path)
{
	tmp_dir_t *dir;

	dir = (tmp_dir_t *)malloc(sizeof(tmp_dir_t));
	if (!dir)
		return NULL;

	dir->d_count = 0;
	dir->d_tree = rbtree_create();
	strncpy(dir->d_path, path, TMP_PATH_MAX);
	pthread_rwlock_init(&dir->d_lock, NULL);
	return dir;
}


static inline tmp_dir_t *tmp_lookup_dir(const char *path)
{
	void *key;
	tmp_dir_t *dir;
	size_t len = strlen(path);
	char dirname[TMP_PATH_MAX] = {0};

	if (TMP_SEPARATOR == path[len - 1])
		key = (void *)path;
	else {
		if (TMP_PATH_MAX - 1 == len) {
			log_err("invalid path");
			return NULL;
		}
		sprintf(dirname, "%s%c", path, TMP_SEPARATOR);
		key = (void *)dirname;
	}

	pthread_rwlock_rdlock(&tmp_dir_lock);
	dir = rbtree_lookup(tmp_dir_tree, (void *)key, tmp_compare);
	pthread_rwlock_unlock(&tmp_dir_lock);
	return dir;
}


static inline tmp_dir_t *tmp_create_dir(const char *path)
{
	tmp_dir_t *dir;

	dir = tmp_alloc_dir(path);
	if (!dir)
		return NULL;

	pthread_rwlock_wrlock(&tmp_dir_lock);
	rbtree_insert(tmp_dir_tree, (void *)dir->d_path, (void *)dir, tmp_compare);
	pthread_rwlock_unlock(&tmp_dir_lock);
	return dir;
}


static inline void tmp_delete_dir(const char *path)
{
	pthread_rwlock_wrlock(&tmp_dir_lock);
	rbtree_delete(tmp_dir_tree, (void *)path, tmp_compare);
	pthread_rwlock_unlock(&tmp_dir_lock);
}


static inline int tmp_push(char *path)
{
	char *filename;
	char dirname[TMP_PATH_MAX];

	if (tmp_dirname(path, dirname)) {
		log_err("invalid path, path=%s", path);
		return -1;
	}

	filename = path + strlen(dirname);
	if ((strlen(filename) > 0) && (strlen(path) < TMP_PATH_MAX)) {
		tmp_dir_t *dir;
		tmp_name_t *name = tmp_alloc_name(filename);

		if (!name) {
			log_err("failed to allocate, path=%s", path);
			return -1;
		}

		dir = tmp_lookup_dir(dirname);
		if (!dir) {
			log_err("failed to locate, path=%s", path);
			free(name);
			return -1;
		}

		pthread_rwlock_wrlock(&dir->d_lock);
		rbtree_insert(dir->d_tree, (void *)name->name, (void *)name, tmp_compare);
		dir->d_count++;
		pthread_rwlock_unlock(&dir->d_lock);
		log_tmp_dir_push(dirname, filename);
	} else {
		log_err("invalid path, path=%s", path);
		return -1;
	}

	return 0;
}


static inline int tmp_pop(char *path)
{
	size_t len;
	char *filename;
	char dirname[TMP_PATH_MAX];

	if (tmp_dirname(path, dirname)) {
		log_err("invalid path, path=%s", path);
		return -1;
	}

	filename = path + strlen(dirname);
	len = strlen(filename);
	if ((len > 0) && (len + filename - path < TMP_PATH_MAX)) {
		tmp_dir_t *dir = tmp_lookup_dir(dirname);

		if (!dir)
			return -1;

		pthread_rwlock_wrlock(&dir->d_lock);
		if (dir->d_count > 0) {
			rbtree_node node = rbtree_lookup_node(dir->d_tree, (void *)filename, tmp_compare);

			if (node) {
				tmp_name_t *name = node->value;

				rbtree_delete_node(dir->d_tree, node);
				dir->d_count--;
				free(name);
			}
		}
		pthread_rwlock_unlock(&dir->d_lock);
	} else {
		log_err("invalid path, path=%s", path);
		return -1;
	}

	return 0;
}


int tmp_size(tmp_file_t *filp)
{
	if (!filp)
		return -EINVAL;
	else
		return filp->f_size;
}


int tmp_truncate(tmp_file_t *filp, off_t length)
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


int tmp_write(const void *ptr, size_t size, size_t count, tmp_file_t *filp)
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


int tmp_read(void *ptr, size_t size, size_t count, tmp_file_t *filp)
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


int tmp_seek(tmp_file_t *filp, long int offset, int origin)
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


tmp_file_t *tmp_open(char *path, const char *mode)
{
	tmp_file_t *file;
	size_t len = strlen(path);

	if (tmp_stat != TMP_STAT_INIT) {
		log_err("invalid state");
		return NULL;
	}

	if ((len >= TMP_PATH_MAX) || (TMP_SEPARATOR == path[len - 1])) {
		log_err("invalid path");
		return NULL;
	}

	file = tmp_lookup_file(path);
	if (!strcmp(mode, "w")) {
		if (!file) {
			if (!tmp_push(path)) {
				file = tmp_create_file(path);
				if (!file) {
					log_err("failed to create, path=%s", path);
					tmp_pop(path);
					return NULL;
				}
			} else {
				log_err("failed to create, path=%s", path);
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


int tmp_access(char *path, int mode)
{
	size_t len = strlen(path);

	if (tmp_stat != TMP_STAT_INIT) {
		log_err("invalid state");
		return -1;
	}

	if (!len || (len >= TMP_PATH_MAX)) {
		log_err("invalid path");
		return -1;
	}

	if (F_OK == mode)
		if (tmp_lookup_file(path))
			return 0;

	return -1;
}


int tmp_is_dir(char *path)
{
	if (tmp_stat != TMP_STAT_INIT) {
		log_err("invalid state");
		return -1;
	}

	return tmp_lookup_dir(path) != NULL;
}


int tmp_is_root(char *path)
{
	return strcmp(path, TMP_ROOT_PATH) == 0;
}


int tmp_mkdir(char *path)
{
	char *name;
	size_t len = strlen(path);
	char dirname[TMP_PATH_MAX] = {0};

	if (tmp_stat != TMP_STAT_INIT) {
		log_err("invalid state");
		return -1;
	}

	if (!len || (len >= TMP_PATH_MAX)) {
		log_err("invalid path");
		return -1;
	}

	if (!tmp_is_root(path)) {
		tmp_dirname(path, dirname);
		if (!tmp_is_root(dirname)) {
			if (!tmp_is_dir(dirname)) {
				if (tmp_mkdir(dirname) < 0) {
					log_err("faile to create, path=%s", path);
					return -1;
				}
			}
		}
	}

	if (TMP_SEPARATOR == path[len - 1])
		name = path;
	else {
		if (TMP_PATH_MAX - 1 == len) {
			log_err("invalid path");
			return -1;
		}
		sprintf(dirname, "%s%c", path, TMP_SEPARATOR);
		name = dirname;
	}

	if (!tmp_is_root(path)) {
		if (tmp_push(name)) {
			log_err("failed to push, path=%s", name);
			return -1;
		}
	}

	if (!tmp_create_dir(name)) {
		log_err("failed to create, path=%s", name);
		if (!tmp_is_root(path))
			tmp_pop(name);
		return -1;
	}

	log_tmp_mkdir(path);
	return 0;
}


int tmp_do_handle_file(rbtree_node node, tmp_callback_t func, char *path, void *arg)
{
	int ret;
	char name[TMP_PATH_MAX];

    if (!node)
        return 0;

    if (node->right) {
        ret = tmp_do_handle_file(node->right, func, path, arg);
		if (ret)
			return ret;
	}

    if (node->left) {
        ret = tmp_do_handle_file(node->left, func, path, arg);
		if (ret)
			return ret;
	}

	strcpy(name, path);
	strcat(name, (char *)node->key);

	if (tmp_is_dir(name)) {
		tmp_dir_t *dir = tmp_lookup_dir(name);

		if (dir)
			return tmp_do_handle_file(dir->d_tree->root, func, dir->d_path, arg);
		else {
			log_err("invalid path");
			return -1;
		}
	} else {
		ret = func(name, arg);
		log_tmp_handle_file(name);
		return ret;
	}
}


int tmp_handle_file(tmp_callback_t func, char *path, void *arg)
{
	tmp_dir_t *dir;

	if (tmp_stat != TMP_STAT_INIT) {
		log_err("invalid state");
		return 0;
	}

	dir = tmp_lookup_dir(path);
	if (dir)
		return tmp_do_handle_file(dir->d_tree->root, func, dir->d_path, arg);

	return 0;
}


int tmp_do_handle_dir(rbtree_node node, tmp_callback_t func, char *path, void *arg)
{
	int ret;
	char name[TMP_PATH_MAX];

    if (!node)
        return 0;

    if (node->right) {
        ret = tmp_do_handle_dir(node->right, func, path, arg);
		if (ret)
			return ret;
	}

    if (node->left) {
        ret = tmp_do_handle_dir(node->left, func, path, arg);
		if (ret)
			return ret;
	}

	strcpy(name, path);
	strcat(name, (char *)node->key);

	if (tmp_is_dir(name)) {
		tmp_dir_t *dir = tmp_lookup_dir(name);

		if (dir) {
			ret = tmp_do_handle_dir(dir->d_tree->root, func, dir->d_path, arg);
			if (ret)
				return ret;

			ret = func(name, arg);
			log_tmp_handle_dir(name);
			return ret;
		} else {
			log_err("invalid path");
			return -1;
		}
	}

	return 0;
}


int tmp_handle_dir(tmp_callback_t func, char *path, void *arg)
{
	tmp_dir_t *dir;

	if (tmp_stat != TMP_STAT_INIT) {
		log_err("invalid state");
		return 0;
	}

	dir = tmp_lookup_dir(path);
	if (dir)
		return tmp_do_handle_dir(dir->d_tree->root, func, dir->d_path, arg);

	return 0;
}


int tmp_release_dir(tmp_dir_t *dir)
{
	int ret = 0;

	pthread_rwlock_wrlock(&dir->d_lock);
	while (dir->d_count > 0) {
		char *name;
		size_t len;
		tmp_name_t *ptr;
		rbtree_node node;
		char path[TMP_PATH_MAX];

		node = dir->d_tree->root;
		if (!node) {
			log_err("invalid directory");
			ret = -1;
			goto out;
		}

		name = (char *)node->key;
		len = strlen(name);
		if (!len || (len >= TMP_PATH_MAX)) {
			log_err("invalid path");
			ret = -1;
			goto out;
		}

		ptr = node->value;
		sprintf(path, "%s%s", dir->d_path, name);
		if (TMP_SEPARATOR == name[len - 1]) {
			if (tmp_rmdir(path)) {
				log_err("failed to remove %s", path);
				ret = -1;
				goto out;
			}
		} else {
			tmp_delete_file(path);
			rbtree_delete_node(dir->d_tree, node);
			dir->d_count--;
			free(ptr);
		}
	}
	tmp_delete_dir(dir->d_path);
out:
	pthread_rwlock_unlock(&dir->d_lock);
	if (!ret)
		free(dir);
	return ret;
}


int tmp_is_empty_dir(char *path)
{
	tmp_dir_t *dir = tmp_lookup_dir(path);

	if (dir && (dir->d_count > 0))
		return 0;
	else
		return 1;
}


int tmp_rmdir(char *path)
{
	tmp_dir_t *dir;

	if (tmp_stat != TMP_STAT_INIT) {
		log_err("invalid state");
		return -1;
	}

	dir = tmp_lookup_dir(path);
	if (!dir) {
		log_err("no directory, path=%s", path);
		return -1;
	}

	if (tmp_pop(dir->d_path)) {
		log_err("failed to remove, path=%s", path);
		return -1;
	}

	if (tmp_release_dir(dir)) {
		log_err("failed to release, path=%s", path);
		return -1;
	}

	log_tmp_rmdir(path);
	return 0;
}


int tmp_remove(char *path)
{
	size_t len;

	if (tmp_stat != TMP_STAT_INIT) {
		log_err("invalid state");
		return -1;
	}

	len = strlen(path);
	if (!len || (len >= TMP_PATH_MAX)) {
		log_err("invalid path");
		return -1;
	}

	if (TMP_SEPARATOR == path[len - 1]) {
		log_err("invalid path");
		return -1;
	} else {
		if (tmp_pop(path))
			return -1;
		else
			tmp_delete_file(path);
	}

	return 0;
}


tmp_entry_t *tmp_get_entry(char *path, size_t size, int flags)
{
	int prot;
	tmp_entry_t *entry;
	tmp_file_t *filp = NULL;

	entry = (tmp_entry_t *)malloc(sizeof(tmp_entry_t));
	if (!entry)
		return NULL;

	if (flags & TMP_RDWR) {
		filp = tmp_open(path, "r+");
		prot = PROT_READ | PROT_WRITE;
	} else if (flags & TMP_RDONLY) {
		filp = tmp_open(path, "r");
		prot = PROT_READ;
	} else
		goto out;

	if (!filp) {
		if (flags & TMP_CREAT) {
			filp = tmp_open(path, "w");
			if (tmp_calloc(filp, size))
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


void tmp_put_entry(tmp_entry_t *entry)
{
	if (entry) {
		tmp_close(entry->file);
		free(entry);
	}
}


int tmp_append(tmp_entry_t *entry, void *buf, size_t size)
{
	tmp_file_t *filp = entry->file;

	tmp_seek(filp, 0, SEEK_END);
	if (tmp_write(buf, 1, size, filp) != size)
		return -EIO;

	return 0;
}


void tmp_close(tmp_file_t *filp)
{
}
