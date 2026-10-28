/* Included before every doomgeneric source (cl6x --preinclude).
   There is no file system under Doom on the pedal: fopen & co. go to zd_platform.c,
   which knows only the IWAD (kept in memory) and refuses everything else. */
#ifndef ZD_SHIM_H
#define ZD_SHIM_H

#define fopen  zd_fopen
#define fclose zd_fclose
#define remove zd_remove
#define rename zd_rename

#endif
