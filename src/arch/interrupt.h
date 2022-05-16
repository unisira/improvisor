#ifndef IMP_ARCH_INTERRUPT_H
#define IMP_ARCH_INTERRUPT_H

#define INTERRUPT_PENDING_MTF 7

#include <ntdef.h>

typedef union _EXCEPTION_BITMAP
{
	UINT32 Value;

	struct
	{
		UINT32 DividerException : 1;
		UINT32 DebugException : 1;
		UINT32 Ignored1 : 1;
		UINT32 BreakpointException : 1;
		UINT32 Ignored2 : 1;
		UINT32 BoundRangeExceeded : 1;
		UINT32 InvalidOpcode : 1;
		UINT32 Ignored3 : 6;
		UINT32 GeneralProtection : 1;
		UINT32 PageFault : 1;
		UINT32 Ignored4 : 1;
		UINT32 FpuFpException : 1;
		UINT32 AlignmentCheck : 1;
		UINT32 Ignored5 : 1;
		UINT32 SIMDFpException : 1;
		UINT32 Ignored6 : 1;
		UINT32 ControlProtection : 1;
	};
} EXCEPTION_BITMAP, *PEXCEPTION_BITMAP;

typedef union _X86_PENDING_DEBUG_EXCEPTIONS
{
	UINT64 Value;

	struct
	{
		UINT64 BreakpointConditionsMet : 4;
		UINT64 reserved_1 : 8;
		UINT64 BreakpointEnable : 1;
		UINT64 reserved_2 : 1;
		UINT64 SingleStep : 1;
		UINT64 reserved_3 : 1;
		UINT64 RTMEnabled : 1;
		UINT64 reserved_4 : 47;
	};
} X86_PENDING_DEBUG_EXCEPTIONS, *PX86_PENDING_DEBUG_EXCEPTIONS;

typedef struct _X86_INTERRUPT_TRAP_GATE
{
	UINT16 OffsetLow;
	UINT16 SegmentSelector;
	
	struct
	{
		UINT32 InterruptStackTable : 3;
		UINT32 Reserved1 : 5;
		UINT32 Type : 4;
		UINT32 Reserved2 : 1;
		UINT32 Dpl : 2;
		UINT32 Present : 1;
		UINT32 OffsetMid : 16;
	};

	UINT32 OffsetHigh;
	UINT32 Reserved3;
} X86_INTERRUPT_TRAP_GATE, *PX86_INTERRUPT_TRAP_GATE;

typedef enum _X86_EXCEPTION
{
	EXCEPTION_DIVIDE_ERROR,
	EXCEPTION_DEBUG_BREAKPOINT,
	EXCEPTION_NON_MASKABLE_INTERRUPT,
	EXCEPTION_BREAKPOINT,
	EXCEPTION_OVERFLOW,
	EXCEPTION_BOUND_RANGE_EXCEEDED,
	EXCEPTION_UNDEFINED_OPCODE,
	EXCEPTION_NO_MATH_COPROCESSOR,
	EXCEPTION_DF,
	EXCEPTION_TSS_INVALID = 10,
	EXCEPTION_SEGMENT_MISSING,
	EXCEPTION_STACK_SEGMENT_FAULT,
	EXCEPTION_GENERAL_PROTECTION_FAULT,
	EXCEPTION_PAGE_FAULT,
	EXCEPTION_MATH_FAULT = 16,
	EXCEPTION_AC,
	EXCEPTION_MACHINE_CHECK,
	EXCEPTION_SIMD_FLOATING_POINT_NUMERIC_ERROR,
	EXCEPTION_VIRTUAL_EXCEPTION
} X86_EXCEPTION, *PX86_EXCEPTION;

typedef enum _X86_INTERRUPT_TYPE
{
	INTERRUPT_TYPE_EXTERNAL_INTERRUPT,
	INTERRUPT_TYPE_RESERVED,
	INTERRUPT_TYPE_NMI,
	INTERRUPT_TYPE_HARDWARE_EXCEPTION,
	INTERRUPT_TYPE_SOFTWARE_INTERRUPT,
	INTERRUPT_TYPE_PRIVILEGED_SOFTWARE_INTERRUPT,
	INTERRUPT_TYPE_SOFTWARE_EXCEPTION,
	INTERRUPT_TYPE_OTHER_EVENT 
} X86_INTERRUPT_TYPE, *PX86_INTERRUPT_TYPE;

#endif
