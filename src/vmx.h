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
} VMX_REGION, *PVMX_REGION;

typedef union _VMX_EXIT_REASON
{
    UINT32 Value;

	struct
	{
        UINT16 BasicExitReason;
        UINT32 Null : 1;
        UINT32 Reserved1 : 7;
        UINT32 EnclaveModeExit : 1;
        UINT32 PendingMTFExit : 1;
        UINT32 VmxRootExit : 1;
        UINT32 Reserved2 : 1;
        UINT32 VmEntryFailure : 1;
	};
} VMX_EXIT_REASON, *PVMX_EXIT_REASON;

typedef struct _VMX_STATE
{
    UINT64 GuestRip;
    VMX_EXIT_REASON ExitReason;

	struct
	{
		UINT64 PinbasedCtls;
		UINT64 PrimaryProcbasedCtls;
		UINT64 SecondaryProcbasedCtls;
		UINT64 VmExitCtls;
		UINT64 VmEntryCtls;
	} Controls;

	struct
	{
		UINT64 PinbasedCtls;
		UINT64 PrimaryProcbasedCtls;
		UINT64 SecondaryProcbasedCtls;
		UINT64 VmExitCtls;
		UINT64 VmEntryCtls;
	} Cap;
} VMX_STATE, * PVMX_STATE;

typedef enum _VMX_CONTROL_FIELD
{
    VMX_PINBASED_CTLS,
    VMX_PRIM_PROCBASED_CTLS,
    VMX_SEC_PROCBASED_CTLS,
    VMX_EXIT_CTLS,
    VMX_ENTRY_CTLS
} VMX_CONTROL_FIELD, * PVMX_CONTROL_FIELD;

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

typedef enum _VMCS
{
	CONTROL_VIRTUAL_PROCESSOR_ID = 0x00000000,
	CONTROL_POSTED_INERRUPT_NOTIF = 0x00000002,
	CONTROL_PINBASED_CONTROLS = 0x00004000,
	CONTROL_PRIMARY_PROCBASED_CONTROLS = 0x00004002,
	CONTROL_EXCEPTION_BITMAP = 0x00004004,
	CONTROL_PAGE_FAULT_ERROR_CODE_MASK = 0x00004006,
	CONTROL_PAGE_FAULT_ERROR_CODE_MATCH = 0x00004008,
	CONTROL_CR3_TARGET_COUNT = 0x0000400A,
	CONTROL_VMEXIT_CONTROLS = 0x0000400C,
	CONTROL_VMEXIT_MSR_STORE_COUNT = 0x0000400E,
	CONTROL_VMEXIT_MSR_LOAD_COUNT = 0x00004010,
	CONTROL_VMENTRY_CONTROLS = 0x00004012,
	CONTROL_VMENTRY_MSR_LOAD_COUNT = 0x00004014,
	CONTROL_VMENTRY_INTERRUPT_INFO = 0x00004016,
	CONTROL_VMENTRY_EXCEPTION_ERROR_CODE = 0x00004018,
	CONTROL_VMENTRY_INSTRUCTION_LENGTH = 0x0000401A,
	CONTROL_SECONDARY_PROCBASED_CONTROLS = 0x0000401E,
	CONTROL_PLE_GAP = 0x00004020,
	CONTROL_PLE_WINDOW = 0x00004022,
	CONTROL_IO_BITMAP_ADDRESS_A = 0x00002000,
	CONTROL_IO_BITMAP_ADDRESS_B = 0x00002002,
	CONTROL_MSR_BITMAP_ADDRESS = 0x00002004,
	CONTROL_VMEXIT_MSR_STORE_ADDRESS = 0x00002006,
	CONTROL_VMEXIT_MSR_LOAD_ADDRESS = 0x00002008,
	CONTROL_VMENTRY_MSR_LOAD_ADDRESS = 0x0000200A,
	CONTROL_EXECUTIVE_VMCS_POINTER = 0x0000200C,
	CONTROL_PML_ADDRESS = 0x0000200E,
	CONTROL_TSC_OFFSET = 0x00002010,
	CONTROL_VIRTUAL_APIC_ADDRESS = 0x00002012,
	CONTROL_APIC_ACCESS_ADDRESS = 0x00002014,
	CONTROL_POSTED_INTERRUPT_DESCRIPTOR_ADDRESS = 0x00002016,
	CONTROL_VMFUNC_CONTROLS = 0x00002018,
	CONTROL_EPT_POINTER = 0x0000201A,
	CONTROL_EPTP_LIST_ADDRESS = 0x00002024,
	CONTROL_XSS_EXITING_BITMAP = 0x0000202C,
	CONTROL_ENCLS_EXITING_BITMAP = 0x0000202E,
	CONTROL_TSC_MULTIPLIER = 0x00002032,
	CONTROL_TERTIARY_PROCBASED_CONTROLS = 0x00002034,
	CONTROL_ENCLV_EXITING_BITMAP = 0x00002036,
	CONTROL_CR0_GUEST_MASK = 0x00006000,
	CONTROL_CR4_GUEST_MASK = 0x00006002,
	CONTROL_CR0_READ_SHADOW = 0x00006004,
	CONTROL_CR4_READ_SHADOW = 0x00006006,
	CONTROL_CR3_TARGET_1 = 0x00006008,
	CONTROL_CR3_TARGET_2 = 0x0000600A,
	CONTROL_CR3_TARGET_3 = 0x0000600C,
	CONTROL_CR3_TARGET_4 = 0x0000600E,
	GUEST_ES_SELECTOR = 0x00000800,
	GUEST_CS_SELECTOR = 0x00000802,
	GUEST_SS_SELECTOR = 0x00000804,
	GUEST_DS_SELECTOR = 0x00000806,
	GUEST_FS_SELECTOR = 0x00000808,
	GUEST_GS_SELECTOR = 0x0000080A,
	GUEST_LDTR_SELECTOR = 0x0000080C,
	GUEST_TR_SELECTOR = 0x0000080E,
	GUEST_INTERRUPT_STATUS = 0x00000810,
	GUEST_PML_INDEX = 0x00000812,
	GUEST_ES_LIMIT = 0x00004800,
	GUEST_CS_LIMIT = 0x00004802,
	GUEST_SS_LIMIT = 0x00004804,
	GUEST_DS_LIMIT = 0x00004806,
	GUEST_FS_LIMIT = 0x00004808,
	GUEST_GS_LIMIT = 0x0000480A,
	GUEST_LDTR_LIMIT = 0x0000480C,
	GUEST_TR_LIMIT = 0x0000480E,
	GUEST_GDTR_LIMIT = 0x00004810,
	GUEST_IDTR_LIMIT = 0x00004812,
	GUEST_ES_ACCESS_RIGHTS = 0x00004814,
	GUEST_CS_ACCESS_RIGHTS = 0x00004816,
	GUEST_SS_ACCESS_RIGHTS = 0x00004818,
	GUEST_DS_ACCESS_RIGHTS = 0x0000481A,
	GUEST_FS_ACCESS_RIGHTS = 0x0000481C,
	GUEST_GS_ACCESS_RIGHTS = 0x0000481E,
	GUEST_LDTR_ACCESS_RIGHTS = 0x00004820,
	GUEST_TR_ACCESS_RIGHTS = 0x00004822,
	GUEST_INTERRUPTIBILITY_STATE = 0x00004824,
	GUEST_ACTIVITY_STATE = 0x00004826,
	GUEST_SMBASE = 0x00004828,
	GUEST_SYSENTER_CS = 0x0000482A,
	GUEST_VMX_PREEMPTION_TIMER_VALUE = 0x0000482E,
	GUEST_VMCS_LINK_POINTER = 0x00002800,
	GUEST_DEBUGCTL = 0x00002802,
	GUEST_PAT = 0x00002804,
	GUEST_EFER = 0x00002806,
	GUEST_PERF_GLOBAL_CTL = 0x00002808,
	GUEST_PDPTE_0 = 0x0000280A,
	GUEST_PDPTE_1 = 0x0000280C,
	GUEST_PDPTE_2 = 0x0000280E,
	GUEST_PDPTE_3 = 0x00002810,
	GUEST_BNDCFGS = 0x00002812,
	GUEST_RTIT_CTL = 0x00002814,
	GUEST_PKRS = 0x00002816,
	GUEST_CR0 = 0x00006800,
	GUEST_CR3 = 0x00006802,
	GUEST_CR4 = 0x00006804,
	GUEST_ES_BASE = 0x00006806,
	GUEST_CS_BASE = 0x00006808,
	GUEST_SS_BASE = 0x0000680A,
	GUEST_DS_BASE = 0x0000680C,
	GUEST_FS_BASE = 0x0000680E,
	GUEST_GS_BASE = 0x00006810,
	GUEST_LDTR_BASE = 0x00006812,
	GUEST_TR_BASE = 0x00006814,
	GUEST_GDTR_BASE = 0x00006816,
	GUEST_IDTR_BASE = 0x00006818,
	GUEST_DR7 = 0x0000681A,
	GUEST_RSP = 0x0000681C,
	GUEST_RIP = 0x0000681E,
	GUEST_RFLAGS = 0x00006820,
	GUEST_PENDING_DEBUG_EXCEPTIONS = 0x00006822,
	GUEST_SYSENTER_ESP = 0x00006824,
	GUEST_SYSENTER_EIP = 0x00006826,
	GUEST_S_CET = 0x00006828,
	GUEST_SSP = 0x0000682A,
	GUEST_SSP_TABLE_ADDRESS = 0x0000682C,
	HOST_ES_SELECTOR = 0x00000C00,
	HOST_CS_SELECTOR = 0x00000C02,
	HOST_SS_SELECTOR = 0x00000C04,
	HOST_DS_SELECTOR = 0x00000C06,
	HOST_FS_SELECTOR = 0x00000C08,
	HOST_GS_SELECTOR = 0x00000C0A,
	HOST_TR_SELECTOR = 0x00000C0C,
	HOST_SYSENTER_CS = 0x00004C00,
	HOST_PAT = 0x00002C00,
	HOST_EFER = 0x00002C02,
	HOST_PERF_GLOBAL_CTL = 0x00002C04,
	HOST_PKRS = 0x00002C06,
	HOST_CR0 = 0x00006C00,
	HOST_CR3 = 0x00006C02,
	HOST_CR4 = 0x00006C04,
	HOST_FS_BASE = 0x00006C06,
	HOST_GS_BASE = 0x00006C08,
	HOST_TR_BASE = 0x00006C0A,
	HOST_GDTR_BASE = 0x00006C0C,
	HOST_IDTR_BASE = 0x00006C0E,
	HOST_SYSENTER_ESP = 0x00006C10,
	HOST_SYSENTER_EIP = 0x00006C12,
	HOST_RSP = 0x00006C14,
	HOST_RIP = 0x00006C16,
	HOST_S_CET = 0x00006C18,
	HOST_SSP = 0x00006C1A,
	HOST_SSP_TABLE_ADDRESS = 0x00006C1C,
	VM_INSTRUCTION_ERROR = 0x00004400,
	VM_EXIT_REASON = 0x00004402,
	VM_EXIT_INTERRUPT_INFO = 0x00004404,
	VM_EXIT_INTERRUPT_ERROR_CODE = 0x00004406,
	IDT_VECTORING_INFO = 0x00004408,
	IDT_VECTORING_ERROR_CODE = 0x0000440A,
	VM_EXIT_INSTRUCTION_LEN = 0x0000440C,
	VM_EXIT_INSTRUCTION_INFO = 0x0000440E,
	GUEST_PHYSICAL_ADDRESS = 0x00002400,
	VM_EXIT_QUALIFICATION = 0x00006400,
	IO_RCX = 0x00006402,
	IO_RSI = 0x00006404,
	IO_RDI = 0x00006406,
	IO_RIP = 0x00006408,
	GUEST_LINEAR_ADDRESS = 0x0000640A
} VMCS, *PVMCS;

PVMX_REGION
VmxAllocateRegion(VOID);

BOOLEAN
VmxCheckSupport(VOID);

BOOLEAN
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
VmxToggleControl(
    _Inout_ PVMX_STATE Vmx,
    _In_ VMX_CONTROL Control
);

VOID
VmxAdvanceGuestRip(VOID);

#endif
