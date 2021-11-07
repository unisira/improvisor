#include "hash.h"

FNV1A 
FNV1AHashQuanta(
    _In_ CHAR Byte, 
    _In_ ULONG Hash
)
{
    return (ULONG)((Hash ^ Byte) * FNV1A_PRIME);
}

FNV1A
FNV1AHashStr(
    LPCSTR Str, 
    ULONG Hash 
)
{
    if (!*Str) 
        return Offset;

    return FNV1AHashStr(Str + 1, FNV1AHashQuanta(*Str, Hash));
}

