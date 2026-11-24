#ifndef TEST_FCNTL_H
#define TEST_FCNTL_H
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
typedef int SceUID;
typedef off_t SceOff;
#define SCE_O_RDONLY O_RDONLY
#define SCE_O_WRONLY O_WRONLY
#define SCE_O_CREAT O_CREAT
#define SCE_O_TRUNC O_TRUNC
#define SCE_SEEK_SET SEEK_SET
int sceIoOpen(const char*, int, int);
int sceIoRead(SceUID, void*, unsigned int);
int sceIoWrite(SceUID, const void*, unsigned int);
int sceIoClose(SceUID);
int sceIoSyncByFd(SceUID, int);
int sceIoRemove(const char*);
int sceIoRename(const char*, const char*);
SceOff sceIoLseek(SceUID, SceOff, int);
int sceIoPread(SceUID, void*, unsigned int, SceOff);
int sceIoPwrite(SceUID, const void*, unsigned int, SceOff);
#endif
