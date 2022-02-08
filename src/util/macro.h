#ifndef IMP_UTIL_MACRO_H
#define IMP_UTIL_MACRO_H

#include <ntdef.h>

#define RVA_PTR(Addr, Offs) \
	((PUCHAR)Addr + (UINT64)Offs)

#define RVA(Addr, Offs) \
	((UINT64)RVA_PTR(Addr, Offs))

#endif