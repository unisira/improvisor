#include <improvisor.h>
#include <arch/memory.h>
#include <vcpu/vcpu.h>
#include <os/dbg.h>
#include <os/pe.h>
#include <macro.h>
#include <ldasm.h>
#include <ldr.h>

// Implementation of RtlVirtualUnwind for our driver, only called during host-execution and therefore all functions will be contained within our driver.
// Unwinding currently doesn't restore system state and is only used for stack tracing when crashing. 

#ifdef _DEBUG
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#endif

VMM_API
UINT64
DbgGetReg(
	_In_ PVCPU_CONTEXT VcpuContext,
	_In_ UINT64 RegisterId
)
/*++
Routine Description:
	Converts a register to a pointer to the corresponding register inside a GUEST_STATE structure
--*/
{
	VMM_RDATA static const UINT64 sRegIdToContextOffs[] = {
		/* RAX */ offsetof(VCPU_CONTEXT, Rax),
		/* RCX */ offsetof(VCPU_CONTEXT, Rcx),
		/* RDX */ offsetof(VCPU_CONTEXT, Rdx),
		/* RBX */ offsetof(VCPU_CONTEXT, Rbx),
		/* RSP */ 0,
		/* RBP */ 0,
		/* RSI */ offsetof(VCPU_CONTEXT, Rsi),
		/* RDI */ offsetof(VCPU_CONTEXT, Rdi),
		/* R8  */ offsetof(VCPU_CONTEXT, R8),
		/* R9  */ offsetof(VCPU_CONTEXT, R9),
		/* R10 */ offsetof(VCPU_CONTEXT, R10),
		/* R11 */ offsetof(VCPU_CONTEXT, R11),
		/* R12 */ offsetof(VCPU_CONTEXT, R12),
		/* R13 */ offsetof(VCPU_CONTEXT, R13),
		/* R14 */ offsetof(VCPU_CONTEXT, R14),
		/* R15 */ offsetof(VCPU_CONTEXT, R15)
	};

	return *(PUINT64)RVA_PTR(VcpuContext, sRegIdToContextOffs[RegisterId]);
}

VMM_API
VOID
DbgSetRegFromStackValue(
	_In_ PVCPU_CONTEXT VcpuContext,
	_In_ CHAR RegisterId,
	_In_ PUINT64 Value
)
{
	return;
}

VMM_API
VOID
DbgPopReg(
	_In_ PVCPU_CONTEXT Context,
	_In_ CHAR Register
)
{
	return;
}

VMM_API
PRUNTIME_FUNCTION
DbgLookupFunctionTable(
	_Out_ PUINT64 Length
)
/*++
Routine Description:
	Returns the .PDATA section for the improvisor, if no .PDATA section is present, the function fails.--*/
{
	PVOID Table = NULL;
	SIZE_T Size = 0;

#ifdef _DEBUG
	PeImageDirectoryEntryToData(&__ImageBase, IMAGE_DIRECTORY_ENTRY_EXCEPTION, &Size);
#else
	Table = LdrImageDirectoryEntryToData(IMAGE_DIRECTORY_ENTRY_EXCEPTION, &Size);
#endif

	if (Length != NULL)
		*Length = Size / sizeof(RUNTIME_FUNCTION);

	return Table;
}

VMM_API
PRUNTIME_FUNCTION
DbgLookupFunctionEntry(
	_In_ UINT64 ControlPC
)
/*++
Routine Description:
	Iterates .PDATA for the image containing ControlPC and returns
--*/
{
	SIZE_T Length = 0;
	PRUNTIME_FUNCTION Table = DbgLookupFunctionTable(&Length);
	if (Table == NULL)
		return NULL;

	// TODO: Finish
}

VMM_API
SIZE_T
DbgUnwindOpSlots(
	_In_ UNWIND_CODE UnwindCode
)
{
	VMM_RDATA static const SIZE_T sUnwindOpExtraSlotTable[] =
	{
		0, // UWOP_PUSH_NONVOL
		1, // UWOP_ALLOC_LARGE (or 3, special cased in lookup code)
		0, // UWOP_ALLOC_SMALL
		0, // UWOP_SET_FPREG
		1, // UWOP_SAVE_NONVOL
		2, // UWOP_SAVE_NONVOL_FAR
		0, // UWOP_EPILOG // previously UWOP_SAVE_XMM
		1, // UWOP_SPARE_CODE // previously UWOP_SAVE_XMM_FAR
		1, // UWOP_SAVE_XMM128
		2, // UWOP_SAVE_XMM128_FAR
		0, // UWOP_PUSH_MACHFRAME
		2, // UWOP_SET_FPREG_LARGE
	};

	if ((UnwindCode.u.UnwindOp == UWOP_ALLOC_LARGE) &&
		(UnwindCode.u.OpInfo != 0))
	{
		return 3;
	}
	else
	{
		return sUnwindOpExtraSlotTable[UnwindCode.u.UnwindOp] + 1;
	}
}

VMM_API
PRUNTIME_FUNCTION
DbgGetChainedFunctionEntry(
	_In_ PRUNTIME_FUNCTION FunctionEntry
)
{
	return NULL;
}

VMM_API
PVOID
DbgUnwindEpilogue(
	_In_ UINT64 ControlPC,
	_In_ PRUNTIME_FUNCTION FunctionEntry,
	_Inout_ PVCPU_CONTEXT VcpuContext
)
{
	
}

VMM_API
PVOID
DbgUnwindBody(
	_In_ UINT64 ControlPC,
	_In_ PRUNTIME_FUNCTION FunctionEntry,
	_Inout_ PVCPU_CONTEXT VcpuContext
)
{

}

VMM_API
PVOID
DbgVirtualUnwind(
	_In_ UINT64 ControlPC,
	_In_ PRUNTIME_FUNCTION FunctionEntry,
	_Inout_ PVCPU_CONTEXT VcpuContext
)
{
	ControlPC -= (UINT64)gLdrLaunchParams.ImageBase;

	if (ControlPC < FunctionEntry->BeginAddress ||
		ControlPC >= FunctionEntry->EndAddress)
		return NULL;

	PUNWIND_INFO UnwindInfo = RVA_PTR(gLdrLaunchParams.ImageBase, FunctionEntry->UnwindData);

	UINT64 CodeOffset = ControlPC - FunctionEntry->BeginAddress;

	// If the first UWOP is UWOP_EPILOG, unwind epilogue instead.
	if (UnwindInfo->UnwindCode[0].u.UnwindOp == UWOP_EPILOG)
	{
		UNWIND_CODE EpilogCode = UnwindInfo->UnwindCode[0];

		SIZE_T i = 0;
		while (i < UnwindInfo->CountOfCodes && UnwindInfo->UnwindCode[i].u.UnwindOp == UWOP_EPILOG)
		{
			UNWIND_CODE Code = UnwindInfo->UnwindCode[i];

			UINT64 EpilogueCodeOffset = FunctionEntry->EndAddress - (Code.u.CodeOffset + (Code.u.OpInfo << 8));
			if (ControlPC - EpilogueCodeOffset < Code.u.CodeOffset)
				return DbgUnwindEpilogue(ControlPC, FunctionEntry, VcpuContext);

			i++;
		}
	}

	SIZE_T i = 0;
	// Skip all ops that we haven't passed yet
	while (i < UnwindInfo->CountOfCodes && UnwindInfo->UnwindCode[i].u.CodeOffset > CodeOffset)
		i += DbgUnwindOpSlots(UnwindInfo->UnwindCode[i]);

	// Process all remaining ops
	while (i < UnwindInfo->CountOfCodes)
	{
		UNWIND_CODE UnwindCode = UnwindInfo->UnwindCode[i];

		// TODO: Support full unwinding, restoration of variables etc.
		switch (UnwindCode.u.UnwindOp)
		{
		case UWOP_PUSH_NONVOL:
			VcpuContext->Rsp += sizeof(UINT64); /* DbgPopReg(VcpuContext, UnwindCode.u.OpInfo); */
			break;

		case UWOP_ALLOC_LARGE:
			VcpuContext->Rsp += (UnwindCode.u.OpInfo != 0 ? *(ULONG*)(&UnwindInfo->UnwindCode[i + 1]) : UnwindInfo->UnwindCode[i + 1].FrameOffset) * sizeof(UINT64);
			break;

		case UWOP_ALLOC_SMALL:
			VcpuContext->Rsp += (UnwindCode.u.OpInfo + 1) * sizeof(UINT64);
			break;

		case UWOP_SET_FPREG:
			VcpuContext->Rsp = DbgGetReg(VcpuContext, UnwindInfo->FrameRegister) - UnwindInfo->FrameOffset * 16;
			break;
		/*
		case UWOP_SAVE_NONVOL:
			DbgSetRegFromStackValue(VcpuContext, UnwindCode.u.OpInfo, RVA_PTR(VcpuContext->Rsp, UnwindInfo->UnwindCode[i + 1].FrameOffset * sizeof(UINT64)));
			break;

		case UWOP_SAVE_NONVOL_FAR:
			DbgSetRegFromStackValue(VcpuContext, UnwindCode.u.OpInfo, RVA_PTR(VcpuContext->Rsp, *(PULONG)(&UnwindInfo->UnwindCode[i + 1]) * sizeof(UINT64)));
			break;

		case UWOP_EPILOG:
		case UWOP_SPARE_CODE:
			break;
		*/
		}

		i += DbgUnwindOpSlots(UnwindInfo->UnwindCode[i]);
	}
}
