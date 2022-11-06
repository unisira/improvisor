#ifndef IMP_UTIL_MACRO_H
#define IMP_UTIL_MACRO_H

#include <ntdef.h>

#define XBITRANGE(L, H) ((UINT64)(((2ULL << ((H) - (L))) - 1) << (L)))

// EL - Error Log macro
//

#define EL(Flags, Function, ...) \
	ELRecordFunction(Flags, Function, __VA_ARGS__)

#endif