#ifndef XRCORE_VITA_THREADING_H
#define XRCORE_VITA_THREADING_H

#include <stdbool.h>

typedef void thread_t(void *);

void thread_name(const char* name);

bool thread_spawn_checked(
    thread_t* entry,
    const char* name,
    unsigned stack,
    void* arglist
);

void thread_spawn(
    thread_t* entry,
    const char* name,
    unsigned stack,
    void* arglist
);

void _initialize_cpu_thread();

#endif
