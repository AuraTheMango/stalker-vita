#include "threading_vita.h"

#include <psp2/kernel/threadmgr.h>

#include <cstdlib>

struct THREAD_STARTUP
{
    thread_t* entry;
    char* name;
    void* args;
};

static int vita_thread_entry(SceSize args, void* argp)
{
    if (argp == nullptr || args < sizeof(THREAD_STARTUP*))
    {
        sceKernelExitDeleteThread(-1);
        return -1;
    }

    THREAD_STARTUP* startup = *static_cast<THREAD_STARTUP**>(argp);

    if (startup == nullptr)
    {
        sceKernelExitDeleteThread(-1);
        return -1;
    }

    thread_t* entry = startup->entry;
    void* argument = startup->args;

    std::free(startup);

    _initialize_cpu_thread();

    entry(argument);

    sceKernelExitDeleteThread(0);
    return 0;
}

void thread_name(const char* name)
{
    // Le nom est fourni directement à sceKernelCreateThread().
    (void)name;
}

void _initialize_cpu_thread()
{
    // Aucun code x86/SSE à initialiser sur ARM.
}

bool thread_spawn_checked(
    thread_t* entry,
    const char* name,
    unsigned stack,
    void* arglist
)
{
    if (entry == nullptr || name == nullptr)
        return false;

    THREAD_STARTUP* startup =
        static_cast<THREAD_STARTUP*>(std::malloc(sizeof(THREAD_STARTUP)));

    if (startup == nullptr)
        return false;

    startup->entry = entry;
    startup->name = const_cast<char*>(name);
    startup->args = arglist;

    SceSize stack_size = stack;

    if (stack_size == 0)
        stack_size = 64 * 1024;

    SceUID thread = sceKernelCreateThread(
        startup->name,
        vita_thread_entry,
        0x10000100,
        stack_size,
        0,
        SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT,
        nullptr
    );

    if (thread < 0)
    {
        std::free(startup);
        return false;
    }

    int result = sceKernelStartThread(
        thread,
        sizeof(startup),
        &startup
    );

    if (result < 0)
    {
        std::free(startup);
        sceKernelDeleteThread(thread);
        return false;
    }

    return true;
}

void thread_spawn(
    thread_t* entry,
    const char* name,
    unsigned stack,
    void* arglist
)
{
    (void)thread_spawn_checked(entry, name, stack, arglist);
}
