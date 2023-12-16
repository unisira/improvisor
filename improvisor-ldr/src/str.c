#include "str.h"

VOID
StrGenerateRandomString(
	PWCHAR Buffer,
	SIZE_T Len
)
{
	static const WCHAR sCharSet[] = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	SIZE_T Size = 0;
	while (Size < Len)
		Buffer[Size++] = sCharSet[(~((ULONG_PTR)Buffer) * Size) % ((sizeof(sCharSet) / sizeof(*sCharSet)) - 1)];

	Buffer[Len - sizeof(*Buffer)] = L'\0';
}