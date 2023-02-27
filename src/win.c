#include <improvisor.h>
#include <arch/memory.h>
#include <arch/msr.h>
#include <vcpu/vcpu.h>
#include <pdb/pdb.h>
#include <mm/mm.h>
#include <win.h>

// KUSER_SHARED_DATA - TIB - PDB PARSING

// TODO: Create EPROCESS/KTHREAD etc. types (no structure)

#define WIN_KERNEL_CPL (3)
#define WIN_USER_CPL (0)

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
	VMM_DATA static SIZE_T sThreadOffset = -1;
	
	if (sThreadOffset == -1)
	{
		sThreadOffset = PdbFindMemberOffset(
			FNV1A_HASH("ntkrnlmp.pdb"),
			FNV1A_HASH("_KPCR"),
			FNV1A_HASH("CurrentThread")
		);
	}

	PVCPU Vcpu = VcpuGetActiveVcpu();

	PVOID Thread = NULL;
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA_PTR(WinGetGs(), sThreadOffset), sizeof(PVOID), &Thread)))
		return NULL;

	return Thread;
}

VMM_API
PVOID
WinGetCurrentProcess(VOID)
{
	VMM_DATA static SIZE_T sProcessOffset = -1;
	
	if (sProcessOffset == -1)
	{
		sProcessOffset = PdbFindMemberOffset(
			FNV1A_HASH("ntkrnlmp.pdb"),
			FNV1A_HASH("_KTHREAD"),
			FNV1A_HASH("Process")
		);
	}

	PVCPU Vcpu = VcpuGetActiveVcpu();

	PVOID Process = NULL;
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA_PTR(WinGetCurrentThread(), sProcessOffset), sizeof(PVOID), &Process)))
		return NULL;

	return Process;
}

VMM_API
PVOID
WinGetNextProcess(
	_In_ PVOID Process
)
{
	VMM_DATA static SIZE_T sProcessLinksOffset = -1;

	if (sProcessLinksOffset == -1)
	{
		sProcessLinksOffset = PdbFindMemberOffset(
			FNV1A_HASH("ntkrnlmp.pdb"),
			FNV1A_HASH("_EPROCESS"),
			FNV1A_HASH("ActiveProcessLinks")
		);
	}

	PVCPU Vcpu = VcpuGetActiveVcpu();

	LIST_ENTRY Links = { 0 };
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA(Process, sProcessLinksOffset), sizeof(LIST_ENTRY), &Links)))
		return NULL;

	return RVA(Links.Flink, -sProcessLinksOffset);
}

VMM_API
PVOID
WinGetPrevProcess(
	_In_ PVOID Process
)
{
	VMM_DATA static SIZE_T sProcessLinksOffset = -1;

	if (sProcessLinksOffset == -1)
	{
		sProcessLinksOffset = PdbFindMemberOffset(
			FNV1A_HASH("ntkrnlmp.pdb"),
			FNV1A_HASH("_EPROCESS"),
			FNV1A_HASH("ActiveProcessLinks")
		);
	}

	PVCPU Vcpu = VcpuGetActiveVcpu();

	LIST_ENTRY Links = { 0 };
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA(Process, sProcessLinksOffset), sizeof(LIST_ENTRY), &Links)))
		return NULL;

	return RVA(Links.Blink, -sProcessLinksOffset);
}

VMM_API
PVOID
WinFindProcess(
	_In_ FNV1A ProcessName
)
{
	VMM_DATA static SIZE_T sNameOffset = -1;

	if (sNameOffset == -1)
	{
		sNameOffset = PdbFindMemberOffset(
			FNV1A_HASH("ntkrnlmp.pdb"),
			FNV1A_HASH("_EPROCESS"),
			FNV1A_HASH("ImageFileName")
		);
	}

	PVOID CurrProcess = WinGetCurrentProcess();
	
	while (TRUE)
	{
		if (FNV1A_HASH(RVA_PTR(CurrProcess, sNameOffset)) == ProcessName)
			return CurrProcess;

		// Advance to the next active process entry
		CurrProcess = WinGetNextProcess(CurrProcess);
	}
}