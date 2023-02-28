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
	PDB_CACHE_MEMBER_OFFSET("ntkrnlmp.pdb", _KPCR, CurrentThread)

	PVCPU Vcpu = VcpuGetActiveVcpu();

	PVOID Thread = NULL;
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA_PTR(WinGetGs(), sCurrentThreadOffset), sizeof(PVOID), &Thread)))
		return NULL;

	return Thread;
}

VMM_API
PVOID
WinGetCurrentProcess(VOID)
{
	PDB_CACHE_MEMBER_OFFSET("ntkrnlmp.pdb", _KTHREAD, Process)

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
	PDB_CACHE_MEMBER_OFFSET("ntkrnlmp.pdb", _EPROCESS, ActiveProcessLinks)

	PVCPU Vcpu = VcpuGetActiveVcpu();

	LIST_ENTRY Links = { 0 };
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA(Process, sActiveProcessLinksOffset), sizeof(LIST_ENTRY), &Links)))
		return NULL;

	return RVA(Links.Flink, -sActiveProcessLinksOffset);
}

VMM_API
PVOID
WinGetPrevProcess(
	_In_ PVOID Process
)
{
	PDB_CACHE_MEMBER_OFFSET("ntkrnlmp.pdb", _EPROCESS, ActiveProcessLinks)

	PVCPU Vcpu = VcpuGetActiveVcpu();

	LIST_ENTRY Links = { 0 };
	if (!NT_SUCCESS(MmReadGuestVirt(Vcpu->SystemDirectoryBase, RVA(Process, sActiveProcessLinksOffset), sizeof(LIST_ENTRY), &Links)))
		return NULL;

	return RVA(Links.Blink, -sActiveProcessLinksOffset);
}

VMM_API
PVOID
WinFindProcess(
	_In_ FNV1A ProcessName
)
{
	PDB_CACHE_MEMBER_OFFSET("ntkrnlmp.pdb", _EPROCESS, ImageFileName)

	PVOID CurrProcess = WinGetCurrentProcess();
	
	while (TRUE)
	{
		if (FNV1A_HASH(RVA_PTR(CurrProcess, sImageFileNameOffset)) == ProcessName)
			return CurrProcess;

		// Advance to the next active process entry
		CurrProcess = WinGetNextProcess(CurrProcess);
	}
}