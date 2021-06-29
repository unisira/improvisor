#ifndef IMP_SPINLOCK_H
#define IMP_SPINLOCK_H

typedef SPINLOCK volatile LONG;
typedef PSPINLOCK SPINLOCK*;

VOID
SpinLock(
    _Inout_ PSPINLOCK Lock;
);

VOID
SpinUnlock(
    _Inout_ PSPINLOCK Lock;
);

#endif
