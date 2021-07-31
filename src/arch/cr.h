#ifndef IMP_ARCH_CR
#define IMP_ARCH_CR

#include <ntdef.h>

#define CR4_RESERVED_BITMASK (0xFFFFFFFFFE088000)
#define CR0_RESERVED_BITMASK (0xFFFFFFFF1FFAFFC0)
#define CR0_PE_PG_BITMASK (0x80000001)
#define CR0_CD_NW_BITMASK (0x60000000)

typedef union _X86_CR0
{
	UINT64 Value;

	struct
	{
		UINT64 ProtectedMode : 1;
		UINT64 MonitorCoProcessor : 1;
		UINT64 FpuEmulation : 1;
		UINT64 TaskSwitched : 1;
		UINT64 ExtensionType : 1;
		UINT64 NumericError : 1;
		UINT64 Reserved1 : 10;
		UINT64 WriteProtect : 1;
		UINT64 Reserved2 : 1;
		UINT64 AlignmentMask : 1;
		UINT64 Reserved3 : 10;
		UINT64 NotWriteThrough : 1;
		UINT64 CacheDisable : 1;
		UINT64 Paging : 1;
	};
} X86_CR0, *PX86_CR0;

typedef union _X86_CR4
{
	UINT64 Value;

	struct
	{
		UINT64 Virtual8086Mode : 1;
		UINT64 ProtectedModeVI : 1;
		UINT64 TimeStampDisable : 1;
		UINT64 DebuggingExtensions : 1;
		UINT64 PageSizeExtensions : 1;
		UINT64 PhysicalAddressExtension : 1;
		UINT64 MachineCheckEnable : 1;
		UINT64 GlobalPageEnable : 1;
		UINT64 PMCEnable : 1;
		UINT64 FxSrOsSupport : 1;
		UINT64 SIMDExceptionSupport : 1;
		UINT64 UsermodeInstructionPrevention : 1;
		UINT64 PageTableExtensions : 1;
		UINT64 VmxEnable : 1;
		UINT64 SmxEnable : 1;
		UINT64 Reserved1 : 1;
		UINT64 FsGsBaseEnable : 1;
		UINT64 PCIDEnable : 1;
		UINT64 XSaveEnable : 1;
		UINT64 KeyLockerEnable : 1;
		UINT64 SmepEnable : 1;
		UINT64 SmapEnable : 1;
		UINT64 ProtectionKeyEnable : 1;
		UINT64 ControlFlowEnforcement : 1;
		UINT64 SupervisorProtectionKeys : 1;
	};
} X86_CR4, *PX86_CR4;

#endif
