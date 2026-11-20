#pragma once
struct SceIoStat { long long st_size; };
int sceIoGetstat(const char*,SceIoStat*);
