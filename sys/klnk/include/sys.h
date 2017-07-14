#ifndef _SYS_H
#define _SYS_H

#include <unistd.h>
#include <sys/syscall.h>

#define __NR_shm_rdprotect		351
#define __NR_shm_wrprotect		352
#define __NR_shm_present			353

#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO		(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO		(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)

static inline int sys_shm_rdprotect(key_t key, unsigned long addr, char *buf)
{
	return syscall(__NR_shm_rdprotect, key, addr, buf);
}

static inline int sys_shm_wrprotect(key_t key, unsigned long addr, char *buf)
{
	return syscall(__NR_shm_wrprotect, key, addr, buf);
}

static inline int sys_shm_present(key_t key, pid_t gpid, unsigned long addr, int flags)
{
	return syscall(__NR_shm_present, key, gpid, addr, flags);
}

#endif
