#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "psp2/io/fcntl.h"
#include "psp2/io/stat.h"
int sceIoOpen(const char *p,int f,int m){return open(p,f,m);}
int sceIoRead(SceUID fd,void *v,unsigned int n){return read(fd,v,n);}
int sceIoWrite(SceUID fd,const void *v,unsigned int n){return write(fd,v,n);}
int sceIoClose(SceUID fd){return close(fd);}
int sceIoSyncByFd(SceUID fd,int flags){(void)flags;return fsync(fd);}
int sceIoRemove(const char *p){return unlink(p);}
int sceIoRename(const char *a,const char *b){return rename(a,b);}
int sceIoMkdir(const char *p,int mode){return mkdir(p,mode);}
int sceIoGetstat(const char *p,SceIoStat *s){struct stat st;if(stat(p,&st))return -1;s->st_mode=st.st_mode;s->st_size=st.st_size;return 0;}
