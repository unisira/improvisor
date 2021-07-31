#ifndef IMP_ARCH_MSR_H
#define IMP_ARCH_MSR_H

#include <ntdef.h>

#define IA32_FEATURE_CONTROL 0x3A
#define IA32_MTRR_CAPABILITIES 0xFE
#define IA32_MTRR_DEFAULT_TYPE 0x2FF
#define IA32_MTRR_PHYSBASE_0 0x200
#define IA32_MTRR_PHYSMASK_0 0x201
#define IA32_VMX_BASIC 0x480
#define IA32_VMX_PINBASED_CTLS 0x481
#define IA32_VMX_PROCBASED_CTLS 0x482
#define IA32_VMX_EXIT_CTLS 0x483
#define IA32_VMX_ENTRY_CTLS 0x484
#define IA32_VMX_PROCBASED_CTLS2 0x48B
#define IA32_VMX_EPT_VPID_CAP 0x48C
#define IA32_VMX_TRUE_PINBASED_CTLS 0x48D
#define IA32_VMX_TRUE_PROCBASED_CTLS 0x48E
#define IA32_VMX_TRUE_EXIT_CTLS 0x48F
#define IA32_VMX_TRUE_ENTRY_CTLS 0x490
#define IA32_VMX_CR0_FIXED0 0x486
#define IA32_VMX_CR0_FIXED1 0x487
#define IA32_VMX_CR4_FIXED0 0x488
#define IA32_VMX_CR4_FIXED1 0x489
#define IA32_DEBUGCTL 0x1D9
#define IA32_SYSENTER_CS 0x174
#define IA32_SYSENTER_ESP 0x175
#define IA32_SYSENTER_EIP 0x176
#define IA32_FS_BASE 0xC0000100
#define IA32_GS_BASE 0xC0000101
#define IA32_STAR 0xC0000082
#define IA32_LSTAR 0xC0000082
#define IA32_EFER 0xC0000080
#define IA32_FMASK 0xC0000084

typedef union _IA32_FEATURE_CONTROL_MSR
{
    UINT64 Value;

    struct
    {
        UINT64 Lock : 1;
        UINT64 VmxonInsideSmx : 1; 
        UINT64 VmxonOutsideSmx : 1;
        UINT64 Reserved1 : 5;
        UINT64 SenterLocal : 7;
        UINT64 SenterGlobal : 1;
        UINT64 Reserved2 : 1;
        UINT64 SGXLaunchControlEnable : 1;
        UINT64 SGXGlobalEnable : 1;
        UINT64 Reserved3 : 1;
        UINT64 LMCEOn : 1;
        UINT64 Reserve4 : 43;
    };
} IA32_FEATURE_CONTROL_MSR, *PIA32_FEATURE_CONTROL_MSR;

typedef union _IA32_VMX_BASIC_MSR
{
    UINT64 Value;

    struct
    {
        UINT32 VmcsRevisionId;
        UINT64 VmxRegionSize : 13;
        UINT64 Reserved1 : 3;
        UINT64 VmxonPhysicalAddressWidth : 1;
        UINT64 DualMonitorSmi : 1;
        UINT64 MemoryType : 4;
        UINT64 IOInstructionReporting : 1;
        UINT64 TrueControls : 1;
        UINT64 NoErrCodeRequirement : 1;
        UINT64 Reserved2 : 7;
    };
} IA32_VMX_BASIC_MSR, *PIA32_VMX_BASIC_MSR;

typedef union _IA32_EFER
{
    UINT64 Value;

    struct
    {
        UINT64 SyscallEnable : 1;
        UINT64 Reserved1 : 1;
        UINT64 LongModeEnable : 1;
        UINT64 LongModeActive : 1;
        UINT64 ExecuteDisable : 1;
        UINT64 Reserved3 : 52;
    };
} IA32_EFER, *PIA32_EFER;

#endif
