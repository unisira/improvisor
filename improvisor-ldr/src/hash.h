#pragma once

#include <windows.h>

#define FNV1A_PRIME (16777619UL)
#define FNV1A_OFFSET (2166136261UL)

#define FNV1A_HASH(Str) FNV1AHashStr(Str, FNV1A_OFFSET) 

// Strongly typed FNV1A type for easier understanding
typedef ULONG FNV1A;

FORCEINLINE
FNV1A 
FNV1AHashQuanta(
	CHAR Byte, 
	ULONG Hash
)
{
	return (ULONG)((Hash ^ Byte) * FNV1A_PRIME);
}

FORCEINLINE
FNV1A
FNV1AHashStr(
	LPCSTR Str, 
	ULONG Hash 
)
{
	if (!*Str) 
		return Hash;

	return FNV1AHashStr(Str + 1, FNV1AHashQuanta(*Str, Hash));
}

