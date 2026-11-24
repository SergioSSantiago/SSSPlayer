#ifndef TEST_STAT_H
#define TEST_STAT_H
#include <stdint.h>
#include <sys/stat.h>
#undef st_mtime
#undef st_ctime
typedef struct { unsigned int st_mode; int64_t st_size; struct timespec st_mtime, st_ctime; } SceIoStat;
#define SCE_S_ISDIR(m) S_ISDIR(m)
int sceIoMkdir(const char*, int);
int sceIoGetstat(const char*, SceIoStat*);
#endif
