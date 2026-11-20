#include "stdafx.h"
#pragma hdrstop

#include "xrSyncronize_vita.h"
#include "xrSyncronize.h"

#include <new>
#include <stdlib.h>

xrCriticalSectionVita::xrCriticalSectionVita() = default;

xrCriticalSectionVita::~xrCriticalSectionVita() = default;

void xrCriticalSectionVita::Enter()
{
    mutex_.enter();
}

void xrCriticalSectionVita::Leave()
{
    mutex_.leave();
}

bool xrCriticalSectionVita::TryEnter()
{
    return mutex_.try_enter();
}

bool xrCriticalSectionVita::IsValid() const
{
    return mutex_.is_valid();
}

#ifdef PROFILE_CRITICAL_SECTIONS
xrCriticalSection::xrCriticalSection(LPCSTR id) : pmutex(NULL), m_id(id)
#else
xrCriticalSection::xrCriticalSection() : pmutex(NULL)
#endif
{
    void* storage = malloc(sizeof(VitaCriticalSection));
    if (!storage)
        abort();
    pmutex = new (storage) VitaCriticalSection();
}

xrCriticalSection::~xrCriticalSection()
{
    VitaCriticalSection* mutex = static_cast<VitaCriticalSection*>(pmutex);
    if (mutex) {
        mutex->~VitaCriticalSection();
        free(mutex);
    }
}

void xrCriticalSection::Enter()
{
    static_cast<VitaCriticalSection*>(pmutex)->enter();
}

void xrCriticalSection::Leave()
{
    static_cast<VitaCriticalSection*>(pmutex)->leave();
}

BOOL xrCriticalSection::TryEnter()
{
    return static_cast<VitaCriticalSection*>(pmutex)->try_enter() ? TRUE : FALSE;
}
