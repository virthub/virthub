#ifndef _LOG_PAGE_H
#define _LOG_PAGE_H

#include "log.h"

#ifdef LOG_PAGE_DIFF
#define log_page_diff(diff) do { \
	int i, j; \
	for (i = 0; i < VRES_PAGE_NR_VERSIONS; i++) { \
		for (j = 0; j < VRES_LINE_MAX; j++) \
			log("%d ", diff[i][j]); \
		log("\n"); \
	} \
} while (0)
#else
#define log_page_diff(...) do {} while (0)
#endif

#ifdef LOG_PAGE_GET
#define log_page_get(resource, path) do { \
	log_resource(resource); \
	log(", path=%s\n", path); \
} while (0)
#else
#define log_page_get(...) do {} while (0)
#endif

#ifdef LOG_PAGE_PUT
#define log_page_put vres_log
#else
#define log_page_put(...) do {} while (0)
#endif

#ifdef LOG_PAGE_LOCK
#define log_page_lock vres_log
#else
#define log_page_lock(...) do {} while (0)
#endif

#ifdef LOG_PAGE_UNLOCK
#define log_page_unlock vres_log
#else
#define log_page_unlock(...) do {} while (0)
#endif

#endif
