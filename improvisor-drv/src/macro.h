#ifndef IMP_UTIL_MACRO_H
#define IMP_UTIL_MACRO_H

#include <ntdef.h>

#define XBITRANGE(L, H) ((UINT64)(((2ULL << ((H) - (L))) - 1) << (L)))

#define RVA_PTR(Addr, Offs) \
	((PVOID)((PCHAR)Addr + (INT64)(Offs)))

#define RVA_PTR_T(T, Addr, Offs) \
	((T*)RVA_PTR((Addr), (Offs)))

#define RVA(Addr, Offs) \
	((UINT64)RVA_PTR((Addr), (Offs)))


#endif
