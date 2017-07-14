#ifndef _LOG_H
#define _LOG_H

#ifndef LOG2FILE
#define log printf
#else
#define log(fmt, ...) do { \
	FILE *filp = fopen(log_name, "a"); \
	fprintf(filp, fmt, ##__VA_ARGS__); \
	fclose(filp); \
} while (0)
#endif

#define log_owner(resource) log("%s@%d: ", __func__, (resource)->owner)

#define log_op(resource) log(", op=%s", log_get_op(vres_get_op(resource)))

#define log_resource(resource) log("%s@%d: key=%d, src=%d, off=%lu, flg=%x", \
	__func__, (resource)->owner, (resource)->key, \
	vres_get_id(resource), vres_get_off(resource), vres_get_flags(resource))

#define vres_log(resource) do { \
	log_resource(resource); \
	log("\n"); \
} while(0)

#ifdef DEBUG
#define log_debug(fmt, ...) log("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define log_debug(...) do {} while(0)
#endif

#ifdef ERROR
#define vres_log_err(resource, fmt, ...) do { \
	log_resource(resource); \
	log(", " fmt "\n", ##__VA_ARGS__); \
} while (0)
#define log_err(fmt, ...) log("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define vres_log_err(...) do {} while (0)
#define log_err(...) do {} while (0)
#endif

char *log_get_op(int op);
char *log_get_shmop(int op);

#endif
