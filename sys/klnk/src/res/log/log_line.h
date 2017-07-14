#ifndef _LOG_LINE_H
#define _LOG_LINE_H

#include "log.h"

#ifdef LOG_LINE_DIFF
#define log_line_diff(line_num, prev, curr) do { \
	int n; \
	const int cnt = LOG_NR_BYTES / sizeof(unsigned long); \
	unsigned long *ptr; \
	ptr = (unsigned long *)(prev); \
	log("%s: line=%d, prev=<", __func__, line_num); \
	for (n = 0; n < cnt - 1; n++) \
		log("%lx, ", *ptr++); \
	log("%lx>", *ptr); \
	ptr = (unsigned long *)(curr); \
	log(", curr=<"); \
	for (n = 0; n < cnt - 1; n++) \
		log("%lx, ", *ptr++); \
	log("%lx>\n", *ptr); \
} while (0)
#else
#define log_line_diff(...) do {} while (0)
#endif

#define log_line_info(resource, nr_lines, total) do { \
	log("ln <new=%d, total=%d>", nr_lines, total); \
} while (0)

#ifdef LOG_LINE_NUM
#define log_line_num(resource, lines, nr_lines) do { \
	log_owner(resource); \
	if (nr_lines > 0) { \
		int i; \
		log(", line=<"); \
		for (i = 0; i < nr_lines - 1; i++) \
			log("%d, ", (lines)[i].num); \
		log("%d>", (lines)[i].num); \
	} else \
		log(", line=<NULL>"); \
} while (0)
#else
#define log_line_num(...) do {} while (0)
#endif

#ifdef LOG_LINE_CONTENT
#define log_line_content(resource, lines, nr_lines) do { \
	int n = LOG_NR_LINES < nr_lines ? LOG_NR_LINES : nr_lines; \
	if (n > 0) { \
		int i; \
		int left = -1; \
		const int cnt = LOG_NR_BYTES / sizeof(unsigned long); \
		log_owner(resource); \
		for (i = 0; i < n; i++) { \
			int j, pos = -1; \
			int min = VRES_LINE_MAX; \
			unsigned long *ptr; \
			for (j = 0; j < nr_lines; j++) { \
				int num = (lines)[j].num; \
				if ((num > left) && (num < min)) { \
					min = num; \
					pos = j; \
				} \
			} \
			if (pos < 0) \
				break; \
			ptr = (unsigned long *)(lines)[pos].buf; \
			log(", ln%d <", (lines)[pos].num); \
			for (j = 0; j < cnt - 1; j++) \
				log("%lx, ", *ptr++); \
			log("%lx>", *ptr); \
			left = min; \
		} \
	} \
} while (0)
#else
#define log_line_content(...) do {} while (0)
#endif

#ifdef LOG_LINES
#define log_lines(resource, lines, nr_lines, total) do { \
	log_owner(resource); \
	log_line_info(resource, nr_lines, total); \
	log_line_num(resource, lines, nr_lines); \
	log_line_content(resource, lines, nr_lines); \
	log("\n"); \
} while (0)
#else
#define log_lines(...) do {} while (0)
#endif

#endif
