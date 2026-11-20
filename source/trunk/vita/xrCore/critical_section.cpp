#include "critical_section.h"

#include <psp2/kernel/threadmgr/mutex.h>

VitaCriticalSection::VitaCriticalSection()
    : mutex_id_(-1)
{
    mutex_id_ = sceKernelCreateMutex(
        "STALKER_VitaCS",
        SCE_KERNEL_MUTEX_ATTR_RECURSIVE,
        1,
        nullptr
    );
}

VitaCriticalSection::~VitaCriticalSection()
{
    if (mutex_id_ >= 0) {
        sceKernelDeleteMutex(mutex_id_);
    }
}

bool VitaCriticalSection::is_valid() const
{
    return mutex_id_ >= 0;
}

bool VitaCriticalSection::enter()
{
    if (!is_valid()) {
        return false;
    }

    return sceKernelLockMutex(mutex_id_, 1, nullptr) >= 0;
}

bool VitaCriticalSection::try_enter()
{
    if (!is_valid()) {
        return false;
    }

    return sceKernelTryLockMutex(mutex_id_, 1) >= 0;
}

bool VitaCriticalSection::leave()
{
    if (!is_valid()) {
        return false;
    }

    return sceKernelUnlockMutex(mutex_id_, 1) >= 0;
}
