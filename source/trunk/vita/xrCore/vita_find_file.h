#ifndef XRCORE_VITA_FIND_FILE_H
#define XRCORE_VITA_FIND_FILE_H

#include <cstdint>
#include <ctime>
#include <stdint.h>

struct _finddata_t
{
    unsigned attrib;
    std::time_t time_write;
    long size;
    char name[260];
};

typedef _finddata_t _finddata64i32_t;

#ifndef _A_HIDDEN
#define _A_HIDDEN 0x02
#endif

#ifndef _A_SUBDIR
#define _A_SUBDIR 0x10
#endif

intptr_t _findfirst(const char* pattern, _finddata_t* data);
int _findnext(intptr_t handle, _finddata_t* data);
int _findclose(intptr_t handle);

#endif
