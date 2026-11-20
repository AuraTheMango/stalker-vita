#ifndef XRCORE_VITA_SYNCRONIZE_H
#define XRCORE_VITA_SYNCRONIZE_H

#include "critical_section.h"

class xrCriticalSectionVita
{
public:
    xrCriticalSectionVita();
    ~xrCriticalSectionVita();

    xrCriticalSectionVita(const xrCriticalSectionVita&) = delete;
    xrCriticalSectionVita& operator=(const xrCriticalSectionVita&) = delete;

    void Enter();
    void Leave();
    bool TryEnter();

    bool IsValid() const;

private:
    VitaCriticalSection mutex_;
};

#endif
