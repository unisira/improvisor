#include "improvisor.h"
#include "vtsc.h"

EXTERN_C
UINT64
__vtsc_estimate_vmexit_latency(VOID);

EXTERN_C
UINT64
__vtsc_estimate_vmentry_latency(VOID);

EXTERN_C
UINT64
__vtsc_estimate_cpuid_latency(VOID);

EXTERN_C
UINT64
__vtsc_estimate_rdtsc_latency(VOID);

EXTERN_C
UINT64
__vtsc_estimate_rdtscp_latency(VOID);

static PTSC_EVENT_ENTRY sTscEventsRaw = NULL;

VOID
VTscEstimateVmExitLatency(
	_Inout_ PTSC_STATUS TscStatus
)
/*++
Routine Description:
	This function estimates the amount of time taken to perform a VM-exit by recording the differences
	between the TSC just before and just after an exiting instruction
--*/
{
	static const SIZE_T sAttemptCount = 100;

	UINT64 Total = 0;
	for (SIZE_T i = 0; i < sAttemptCount; i++) 
		Total += __vtsc_estimate_vmexit_latency();

	TscStatus->VmExitLatency = Total / sAttemptCount;
}

VOID
VTscEstimateVmEntryLatency(
	_Inout_ PTSC_STATUS TscStatus
)
/*++
Routine Description:
	This function estimates the amount of time taken to perform a VM-exit
--*/
{
	static const SIZE_T sAttemptCount = 100;

	UINT64 Total = 0;
	for (SIZE_T i = 0; i < sAttemptCount; i++)
		Total += __vtsc_estimate_vmentry_latency();

	TscStatus->VmEntryLatency = Total / sAttemptCount;
}

VOID 
VTscGetEventLatencies(
	_Inout_ PTSC_STATUS TscStatus
)
{
	static const SIZE_T sAttemptCount = 100;

	UINT64 Total = 0;
	for (SIZE_T i = 0; i < sAttemptCount; i++)
		Total += __vtsc_estimate_cpuid_latency();

	TscStatus->CpuidLatency = Total / sAttemptCount;

	ImpDebugPrint("CPUID latency = %u cycles...\n", TscStatus->CpuidLatency);

	Total = 0;
	for (SIZE_T i = 0; i < sAttemptCount; i++)
		Total += __vtsc_estimate_rdtsc_latency();

	TscStatus->RdtscLatency = Total / sAttemptCount;

	ImpDebugPrint("RDTSC latency = %u cycles...\n", TscStatus->RdtscLatency);

	Total = 0;
	for (SIZE_T i = 0; i < sAttemptCount; i++)
		Total += __vtsc_estimate_rdtscp_latency();

	TscStatus->RdtscpLatency = Total / sAttemptCount;

	ImpDebugPrint("RDTSCP latency = %u cycles...\n", TscStatus->RdtscpLatency);
}

NTSTATUS
VTscInitialise(
	_Inout_ PTSC_STATUS TscStatus
)
/*++
Routine Description:
	This function prepares everything that is necessary to virtualise the TSC
--*/
{
	// First, gather the different event latencies 
	VTscGetEventLatencies(TscStatus);

	return STATUS_SUCCESS;
}
