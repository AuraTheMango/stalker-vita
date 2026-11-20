#pragma once
#include <cstddef>
#define SCE_O_RDONLY 0
#define SCE_SEEK_SET 0
int sceIoOpen(const char*,int,int);
int sceIoClose(int);
int sceIoRead(int,void*,size_t);
long long sceIoLseek(int,long long,int);
