#ifndef IMP_UTIL_HASH_H
#define IMP_UTIL_HASH_H

#include <ntdef.h>

#define FNV1A_PRIME (16777619UL)
#define FNV1A_OFFSET (2166136261UL)

#define FNV1A_HASH(Str) FNV1AHashStr(Str, FNV1A_OFFSET) 

// Strongly typed FNV1A type for easier understanding
typedef ULONG FNV1A;

FNV1A FNV1AHashStr(
	_In_ LPCSTR Str,
	_In_ ULONG Hash 
);

#endif
