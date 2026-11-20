#ifndef XRCORE_VITA_CRITICAL_SECTION_H
#define XRCORE_VITA_CRITICAL_SECTION_H

#include <psp2/kernel/threadmgr.h>

class VitaCriticalSection
{
public:
    VitaCriticalSection();
    ~VitaCriticalSection();

    VitaCriticalSection(const VitaCriticalSection&) = delete;
    VitaCriticalSection& operator=(const VitaCriticalSection&) = delete;

    bool is_valid() const;
    bool enter();
    bool try_enter();
    bool leave();

private:
    SceUID mutex_id_;
};

#endif
