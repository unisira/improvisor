#include <immintrin.h>

#include "../improvisor.h"
#include "spinlock.h"

// TODO: Copy Windows Ex functions

BOOLEAN
SpinTryLock(
	_Inout_ PSPINLOCK Lock
)
/*++
Routine Description:
	Attempts to lock a spinlock by first checking if it is free, then attempting to lock it if so.
	We check if InterlockedCompareExchange's return value was 0 as if so, this means that the exchange
	occurred and we have now locked the spinlock. If it returned something else, another thread beat us 
	to the lock (should never happen in our case).
--*/
{
	SPINLOCK Contents = *Lock;

	// If the lock is already exclusively held, return false
	if (Contents != 0)
		return FALSE;

	return (InterlockedCompareExchange(Lock, 1, 0) == 0);
}

VOID
SpinLock(
	_Inout_ PSPINLOCK Lock
)
/*++
Routine Description:
	Locks a spinlock.
--*/
{
	while (!SpinTryLock(Lock))
		_mm_pause();
}

VOID
SpinUnlock(
	_Inout_ PSPINLOCK Lock
)
/*++
Routine Description:
	Unlocks a spinlock
--*/
{
	*Lock = 0;
}
