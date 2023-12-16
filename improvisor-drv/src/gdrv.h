#ifndef IMP_GDRV_H
#define IMP_GDRV_H

#include <ntdef.h>

// GdCleanPDDBCache
// GdCleanCiDllData
// GdCleanUnloadedDrivers

NTSTATUS
GdCleanDDBCache(
	VOID
);

NTSTATUS
GdCleanCiDllData(
	VOID
);

NTSTATUS
GdCleanUnloadedDrivers(
	VOID
);

#endif