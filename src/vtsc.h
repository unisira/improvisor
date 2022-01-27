#ifndef IMP_TSC_H
#define IMP_TSC_H

#include <ntdef.h>

typedef enum _TSC_EVENT_TYPE
{
    TSC_EVENT_CPUID,
    TSC_EVENT_RDTSC,
    TSC_EVENT_RDTSCP,
    TSC_EVENT_
} TSC_EVENT_TYPE;

typedef struct _TSC_EVENT_ENTRY
{
    BOOLEAN Valid;
    TSC_EVENT_TYPE Type;
    UINT64 Latency;
    UINT64 Timestamp;
} TSC_EVENT_ENTRY, *PTSC_EVENT_ENTRY;

typedef struct _TSC_STATUS
{
    TSC_EVENT_ENTRY PrevEvent;
    BOOLEAN SpoofEnabled;
    UINT64 VmEntryLatency;
    UINT64 VmExitLatency;
    UINT64 RdtscLatency;
    UINT64 RdtscpLatency;
    UINT64 CpuidLatency;
} TSC_STATUS, *PTSC_STATUS;

VOID
VTscEstimateVmExitLatency(
    _Inout_ PTSC_STATUS TscStatus
);

VOID
VTscEstimateVmEntryLatency(
    _Inout_ PTSC_STATUS TscStatus
);

NTSTATUS
VTscInitialise(
    _Inout_ PTSC_STATUS TscStatus
);

#endif
