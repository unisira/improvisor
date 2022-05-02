#ifndef IMP_ARCH_FLAGS_H
#define IMP_ARCH_FLAGS_H

#include <ntdef.h>

typedef union _X86_RFLAGS
{
	UINT64 Value;

	struct
	{
		UINT64 Carry : 1;
		UINT64 Reserved1 : 1;
		UINT64 Parity : 1;
		UINT64 Reserved2 : 1;
		UINT64 AuxCarry : 1;
		UINT64 Reserved3 : 1;
		UINT64 Zero : 1;
		UINT64 Sign : 1;
		UINT64 Trap : 1;
		UINT64 InterruptEnable: 1;
		UINT64 Direction : 1;
		UINT64 Overflow : 1;
		UINT64 IOPrivilegeLevel : 2;
		UINT64 NestedTask : 1;
		UINT64 Reserved4 : 1;
		UINT64 Resume : 1;
		UINT64 Virtual8086 : 1;
		UINT64 AlignmentCheck : 1;
		UINT64 VirtualInterrupt : 1;
		UINT64 VirtualInterruptPending: 1;
		UINT64 IdFlag: 1;
	};
} X86_RFLAGS, *PX86_RFLAGS;

#endif
