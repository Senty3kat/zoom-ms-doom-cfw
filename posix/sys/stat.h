#ifndef ZD_SYS_STAT_H
#define ZD_SYS_STAT_H
static inline int mkdir(const char *path, int mode) { (void)path; (void)mode; return -1; }
#endif
