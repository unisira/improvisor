#ifndef IMP_VMX_H
#define IMP_VMX_H

#define VMX_MSR_READ_BITMAP_OFFS 0 
#define VMX_MSR_WRITE_BITMAP_OFFS 2048
#define VMX_MSR_LO_BITMAP_OFFS 0
#define VMX_MSR_HI_BITMAP_OFFS 1024

#define ENCODE_VMX_CONTROL(Field, Position) \
    (UINT64)(((UINT64)Position << 3) | (UINT64)Field)

typedef struct _VMX_REGION
{
    UINT32 VmcsRevisionId;
    UINT32 VmxAbortIndicator;
    UINT8 Data[0x1000 - sizeof(UINT64)];
} VMX_REGION, PVMX_REGION;

typedef enum _VMX_CONTROL_FIELD
{
    VMX_PINBASED_CTLS,
    VMX_PRIM_PROCBASED_CTLS,
    VMX_SEC_PROCBASED_CTLS,
    VMX_EXIT_CTLS,
    VMX_ENTRY_CTLS
} VMX_CONTROL_FIELD, *PVMX_CONTROL_FIELD

typedef enum _VMX_CONTROL
{
    VMX_CTL_EXT_INTERRUPT_EXITING       = ENCODE_VMX_CONTROL(VMX_PINBASED_CTLS, 0),
    VMX_CTL_NMI_EXITING                 = ENCODE_VMX_CONTROL(VMX_PINBASED_CTLS, 3),
    VMX_CTL_VIRTUAL_NMIS                = ENCODE_VMX_CONTROL(VMX_PINBASED_CTLS, 5),
    VMX_CTL_VMX_PREEMPTION_TIMER        = ENCODE_VMX_CONTROL(VMX_PINBASED_CTLS, 6),
    VMX_CTL_PROCESS_POSTED_INTERRUPTS   = ENCODE_VMX_CONTROL(VMX_PINBASED_CTLS, 7),
    VMX_CTL_INTERRUPT_WINDOW_EXITING    = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 2),
    VMX_CTL_USE_TSC_OFFSETTING          = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 3),
    VMX_CTL_HLT_EXITING                 = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 7),
    VMX_CTL_INVLPG_EXITING              = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 9),
    VMX_CTL_MWAIT_EXITING               = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 10),
    VMX_CTL_RDPMC_EXITING               = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 11),
    VMX_CTL_RDTSC_EXITING               = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 12),
    VMX_CTL_CR3_LOAD_EXITING            = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 15),
    VMX_CTL_CR3_STORE_EXITING           = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 16),
    VMX_CTL_CR8_LOAD_EXITING            = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 19),
    VMX_CTL_CR8_STORE_EXITING           = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 20),
    VMX_CTL_USE_TPR_SHADOW              = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 21),
    VMX_CTL_NMI_WINDOW_EXITING          = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 22),
    VMX_CTL_MOV_DR_EXITING              = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 23),
    VMX_CTL_UNCOND_IO_EXITING           = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 24),
    VMX_CTL_USE_IO_BITMAPS              = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 25),
    VMX_CTL_MONITOR_TRAP_FLAG           = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 27),
    VMX_CTL_USE_MSR_BITMAPS             = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 28),
    VMX_CTL_MONITOR_EXITING             = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 29),
    VMX_CTL_PAUSE_EXITING               = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 30),
    VMX_CTL_SECONDARY_CTLS_ACTIVE       = ENCODE_VMX_CONTROL(VMX_PRIM_PROCBASED_CTLS, 31)
} VMX_CONTROL, *PVMX_CONTROL;

typedef _VMCS
{
    CONTROL_VIRTUAL_PROCESSOR_ID = 0x00000000,
    CONTROL_POSTED_INERRUPT_NOTIF = 0x00000002,
    CONTROL_PINBASED_CONTROLS = 0x00004000,
    CONTROL_PRIMARY_PROCBASED_CONTROLS = 0x00004002,
    CONTROL_VMEXIT_CONTROLS = 0x0000400C,
    CONTROL_VMENTRY_CONTROLS = 0x00004012,
    CONTROL_SECONDARY_PROCBASED_CONTROLS = 0x0000401E    
} VMCS, *PVMCS;

typedef struct _VMX_STATE
{
    UINT64 GuestRip;
    UINT16 BasicExitReason;
} VMX_STATE, *PVMX_STATE;

PVMX_REGION
VmxAllocateRegion(VOID);

BOOL
VmxCheckSupport(VOID);

BOOL
VmxEnableVmxon(VOID);

VOID
VmxRestrictControlRegisters(VOID);

UINT64
VmxApplyCr0Restrictions(
    _In_ UINT64 Cr0
);

UINT64
VmxApplyCr4Restrictions(
    _In_ UINT64 Cr4
);

UINT64
VmxRead(
    _In_ VMCS Component
);

VOID
VmxWrite(
    _In_ VMCS Component,
    _In_ UINT64 Value
);

VOID
VmxInjectEvent(
    _In_ APIC_EXCEPTION_VECTOR Vector,
    _In_ X86_INTERRUPT_TYPE Type,
    _In_ ULONG ErrCode
);

VOID
VmxAdvanceGuest(VOID);

#endif
