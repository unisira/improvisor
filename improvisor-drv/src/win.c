#include <improvisor.h>
#include <arch/msr.h>
#include <vcpu/vcpu.h>
#include <pdb/pdb.h>
#include <mm/mm.h>
#include <macro.h>
#include <win.h>

// KUSER_SHARED_DATA - TIB - PDB PARSING

// TODO: Create EPROCESS/KTHREAD etc. types (no structure)

#define WIN_KERNEL_CPL (0)
#define WIN_USER_CPL (3)

NTSTATUS
WinInitialise(VOID)
{    
#if 0
	PMM_VPTE Vpte;
	MmAllocateVpte(&Vpte);

	if (!NT_SUCCESS(MmMapGuestVirt(Vpte, Vcpu->Vmm->SystemCr3, KPCR_ADDRESS)))
	{
		ImpDebugPrint("Failed to map KUSER_SHARED_DATA...\n");
		return STATUS_INVALID_PARAMETER;
	}
#endif

	// TODO: Run KPCR tests
}

PVOID
WinGetGuestGs(VOID)
{
	return VcpuGetGuestCPL(VcpuGetActiveVcpu()) == WIN_KERNEL_CPL ? VmxRead(GUEST_GS_BASE) : __readmsr(IA32_KERNEL_GS_BASE);
}

VMM_API
PVOID
WinGetCurrentThread(VOID)
{
	PVOID Thread = NULL;
	if (!NT_SUCCESS(MmReadGuestVirt(VmxRead(GUEST_CR3), RVA_PTR(WinGetGuestGs(), gCurrentThreadOffset), sizeof(PVOID), &Thread)))
		return NULL;

	return Thread;
}

VMM_API
PVOID
WinGetCurrentProcess(VOID)
{
	PVCPU Vcpu = VcpuGetActiveVcpu();

	PVOID Process = NULL;
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA_PTR(WinGetCurrentThread(), gProcessOffset), sizeof(PVOID), &Process)))
		return NULL;

	return Process;
}

VMM_API
PVOID
WinGetNextProcess(
	_In_ PVOID Process
)
{
	PVCPU Vcpu = VcpuGetActiveVcpu();

	LIST_ENTRY Links = { 0 };
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA(Process, gActiveProcessLinksOffset), sizeof(LIST_ENTRY), &Links)))
		return NULL;

	return RVA(Links.Flink, -gActiveProcessLinksOffset);
}

VMM_API
PVOID
WinGetPrevProcess(
	_In_ PVOID Process
)
{
	PVCPU Vcpu = VcpuGetActiveVcpu();

	LIST_ENTRY Links = { 0 };
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA(Process, gActiveProcessLinksOffset), sizeof(LIST_ENTRY), &Links)))
		return NULL;

	return RVA(Links.Blink, -gActiveProcessLinksOffset);
}

VMM_API
FNV1A
WinGetProcessNameHash(
	_In_ PVOID Process
)
{
	PVCPU Vcpu = VcpuGetActiveVcpu();

	UCHAR ImageFileName[16] = { 0 };
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA_PTR(Process, gImageFileNameOffset), 15, &ImageFileName)))
		return 0;

	return FNV1A_HASH(ImageFileName);
}

VMM_API
BOOLEAN
WinGetProcessName(
	_In_ PVOID Process,
	_Out_ PCHAR Buffer
)
{
	PVCPU Vcpu = VcpuGetActiveVcpu();

	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA_PTR(Process, gImageFileNameOffset), 15, Buffer)))
		return FALSE;

	return TRUE;
}

VMM_API
PVOID
WinFindProcess(
	_In_ FNV1A ProcessName
)
{
	PVOID CurrProcess = WinGetCurrentProcess();

	while (TRUE)
	{
		if (WinGetProcessNameHash(CurrProcess) == ProcessName)
			return CurrProcess;

		// Advance to the next active process entry
		CurrProcess = WinGetNextProcess(CurrProcess);
	}
}

VMM_API
ULONG_PTR
WinGetProcessPID(
	_In_ PVOID Process
)
{
	PVCPU Vcpu = VcpuGetActiveVcpu();

	ULONG_PTR Pid = -1;
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA_PTR(Process, gUniqueProcessIdOffset), sizeof(ULONG_PTR), &Pid)))
		return -1;

	return Pid;
}

VMM_API
PVOID
WinFindProcessById(
	_In_ UINT32 ProcessId
)
{
	PVOID CurrProcess = WinGetCurrentProcess();

	while (TRUE)
	{
		if (WinGetProcessPID(CurrProcess) == ProcessId)
			return CurrProcess;

		// Advance to the next active process entry
		CurrProcess = WinGetNextProcess(CurrProcess);
	}
}
