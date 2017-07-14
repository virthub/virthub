#ifndef _DEBUG_H
#define _DEBUG_H

#ifdef ERROR
#define klnk_log_err(fmt, ...) printf("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define klnk_log_err(...) do {} while (0)
#endif

#ifdef DEBUG
#define klnk_log(fmt, ...) printf("%s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define klnk_log(...) do {} while (0)
#endif

#endif
