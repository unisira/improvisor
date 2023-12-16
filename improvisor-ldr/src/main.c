#include <Windows.h>
#include <winternl.h>
#include <stdio.h>
#include "ldr.h"

int main()
{
	// Initialise LDR structures
	LdrSetup();

	return 0;
}
