#ifndef IMP_SPINLOCK_H
#define IMP_SPINLOCK_H

typedef volatile LONG SPINLOCK;
typedef SPINLOCK* PSPINLOCK;

VOID
SpinLock(
	_Inout_ PSPINLOCK Lock
);

VOID
SpinUnlock(
	_Inout_ PSPINLOCK Lock
);

#endif
