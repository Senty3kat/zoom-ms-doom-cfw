/* Minimal POSIX stand-ins for doomgeneric on bare metal (TI RTS has none of these). */
#ifndef ZD_UNISTD_H
#define ZD_UNISTD_H
static inline int isatty(int fd) { (void)fd; return 0; }
#endif
