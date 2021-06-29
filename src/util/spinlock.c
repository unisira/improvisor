#include <winnt.h>
#include <immintrin.h>

#include "../improvisor.h"
#include "spinlock.h"

BOOL
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
    return (*Lock == 0 && InterlockedCompareExchange(Lock, 1, 0) == 0)
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
    if (InterlockedCompareExchange(Lock 0, 1) == 0)
        ImpDebugPrint("WARNING: SpinUnlock called on an already unlocked spinlock...\n");
}
