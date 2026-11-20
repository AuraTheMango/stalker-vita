#pragma once
#include <psp2/types.h>
int sceKernelCreateSema(const char*,int,int,int,void*);
int sceKernelSignalSema(int,int);
int sceKernelWaitSema(int,int,void*);
int sceKernelDeleteSema(int);
int sceKernelCreateThread(const char*,int(*)(SceSize,void*),int,int,int,int,void*);
int sceKernelStartThread(int,unsigned,void*);
int sceKernelDeleteThread(int);
int sceKernelWaitThreadEnd(int,void*,void*);
