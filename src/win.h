#ifndef IMP_WIN_H
#define IMP_WIN_H

#include <ntdef.h>
#include <hash.h>

NTSTATUS
WinInitialise(VOID);

PVOID
WinFindProcess(
	_In_ FNV1A ProcessName
);

#endif
