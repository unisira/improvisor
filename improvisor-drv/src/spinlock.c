#include <improvisor.h>
#include <spinlock.h>

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
	return (*Lock == 0 && InterlockedCompareExchange(Lock, 1, 0) == 0);

	// Testing a new hook that also contains the value of how much a lock is being challenged.
	// return InterlockedIncrement(Lock) == 1;
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
