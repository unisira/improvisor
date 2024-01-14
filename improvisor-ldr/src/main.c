#include <Windows.h>
#include <winternl.h>
#include <winnt.h>
#include <stdio.h>
#include "vmcall.h"
#include "macro.h"
#include "hash.h"

#pragma comment(lib, "ntdll.lib")

#define REG_DRV_SERVICE_NAME L"impv"
#define REG_SERVICES_NT_PATH (L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\" REG_DRV_SERVICE_NAME)
#define REG_SERVICES_PATH (L"SYSTEM\\CurrentControlSet\\Services\\" REG_DRV_SERVICE_NAME)

typedef enum _ARGV_INDICES
{
	ARGV_EXE_PATH,
	ARGV_DRV_PATH,
	ARGV_COUNT
} ARGV_INDICES;

INT
LdrPrintUsage(
	VOID
)
{
	printf("\nUsage:\n\n\timprovisor-ldr.exe <path.sys>\n\n");
	return EXIT_FAILURE;
}

BOOLEAN
LdrEnablePrivilege(
	LPCSTR PrivilegeName
)
{
	TOKEN_PRIVILEGES Tp;
	HANDLE TokenHandle;
	LUID Luid;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &TokenHandle))
		return FALSE;

	if (!LookupPrivilegeValue(NULL, PrivilegeName, &Luid))
		return FALSE;

	Tp.PrivilegeCount = 1;
	Tp.Privileges->Luid = Luid;
	Tp.Privileges->Attributes = SE_PRIVILEGE_ENABLED;

	if (!AdjustTokenPrivileges(TokenHandle, FALSE, &Tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL))
		return FALSE;

	if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
		return FALSE;

	return TRUE;
}

NTSYSCALLAPI
NTSTATUS
NTAPI
NtLoadDriver(
    _In_ PUNICODE_STRING DriverServiceName
    );

NTSYSCALLAPI
NTSTATUS
NTAPI
NtUnloadDriver(
    _In_ PUNICODE_STRING DriverServiceName
    );

// TODO: Remove once better
UINT64 VmReadCheck = 0x1122334455667788;

BOOLEAN
LdrLockThreadStack(
	VOID
)
{
	MEMORY_BASIC_INFORMATION Mbi;
	ULONG_PTR StackLow;
	ULONG_PTR StackHigh;

	GetCurrentThreadStackLimits(&StackLow, &StackHigh);

	printf("StackHigh=%llX StackLow=%llX\n", StackHigh, StackLow);

	PVOID CurrentAddress = (PVOID)StackLow;
	// Iterate VQ until we have covered all of the stack
	while (CurrentAddress < (PVOID)StackHigh)
	{
		if (VirtualQuery(CurrentAddress, &Mbi, sizeof(Mbi)) == 0)
		{
			printf("VQ(CurrentAddress) failed: %X", GetLastError());
			return FALSE;
		}	

		printf("%p: AllocProtect=%X Protect=%X Size=%llX Type=%X State=%X\n", 
			CurrentAddress, 
			Mbi.AllocationProtect, 
			Mbi.Protect, 
			Mbi.RegionSize, 
			Mbi.Type, 
			Mbi.State
		);

		if (Mbi.Protect != 0 && (Mbi.Protect & PAGE_GUARD) == 0)
		{
			SIZE_T WorkingSetMin, WorkingSetMax;
			if (!GetProcessWorkingSetSize(GetCurrentProcess(), &WorkingSetMin, &WorkingSetMax))
			{
				printf("Failed to get working set limits: %X\n", GetLastError());
				return FALSE;
			}

			printf("WorkingSet: Min=%llX Max=%llX\n", WorkingSetMin, WorkingSetMax);

			// Increase working set size by the size of this region so we can lock it
			WorkingSetMax += Mbi.RegionSize;

			if (!SetProcessWorkingSetSize(GetCurrentProcess(), WorkingSetMin, WorkingSetMax))
			{
				printf("Failed to increase working set limits: %X\n", GetLastError());
				return FALSE;
			}

			if (!VirtualLock(Mbi.BaseAddress, Mbi.RegionSize))
			{
				printf("Failed to lock region: %X\n", GetLastError());
				return FALSE;
			}

			if (VirtualQuery(CurrentAddress, &Mbi, sizeof(Mbi)) == 0)
			{
				printf("VQ(StackLow) failed: %X", GetLastError());
				return FALSE;
			}		

			printf("POST-LOCK - %p: AllocProtect=%X Protect=%X Size=%llX Type=%X State=%X\n", 
				CurrentAddress, 
				Mbi.AllocationProtect, 
				Mbi.Protect, 
				Mbi.RegionSize, 
				Mbi.Type, 
				Mbi.State
			);		
		}

		CurrentAddress = RVA_PTR(CurrentAddress, Mbi.RegionSize);
	}

	return TRUE;
}

int main(int argc, char** argv)
{
	HKEY DriverSvcKey;
	BOOLEAN Active = TRUE;
	CHAR CurrDirectory[MAX_PATH] = {0};
	CHAR DriverPathBuf[MAX_PATH] = {0};
	CHAR CommandBuf[128] = {0};
	ANSI_STRING DriverPath;
	UNICODE_STRING DriverRegistryPath;
	DWORD DriverSvcType = SERVICE_KERNEL_DRIVER;
	DWORD Disposition;
	LSTATUS Err;

	if (argc != ARGV_COUNT)
		return LdrPrintUsage();

	if (!LdrEnablePrivilege(SE_LOAD_DRIVER_NAME) || !LdrEnablePrivilege(SE_DEBUG_NAME))
		return EXIT_FAILURE;

	if (!LdrLockThreadStack())
		return EXIT_FAILURE;

	RtlInitUnicodeString(&DriverRegistryPath, REG_SERVICES_NT_PATH);

	GetCurrentDirectory(MAX_PATH, CurrDirectory);
	strcat_s(DriverPathBuf, MAX_PATH, "\\??\\");
	strcat_s(DriverPathBuf, MAX_PATH, CurrDirectory);
	strcat_s(DriverPathBuf, MAX_PATH, "\\");
	strcat_s(DriverPathBuf, MAX_PATH, argv[ARGV_DRV_PATH]);

	RtlInitAnsiString(&DriverPath, DriverPathBuf);

	Err = RegCreateKeyExW(HKEY_LOCAL_MACHINE, REG_SERVICES_PATH, 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &DriverSvcKey, &Disposition);
	if (Err != ERROR_SUCCESS)
	{
		printf("Failed to create driver service subkey: %X\n", Err);
		return EXIT_FAILURE;
	}

	Err |= RegSetValueEx(DriverSvcKey, "Type", 0, REG_DWORD, (PBYTE)&DriverSvcType, sizeof(DWORD));
	Err |= RegSetValueEx(DriverSvcKey, "ImagePath", 0, REG_SZ, DriverPath.Buffer, DriverPath.Length);

	if (Err != ERROR_SUCCESS)
	{
		printf("Failed to write registry values: %X\n", Err);
		RegCloseKey(DriverSvcKey);
		return EXIT_FAILURE;
	}

	RegCloseKey(DriverSvcKey);

	NTSTATUS Status = NtLoadDriver(&DriverRegistryPath);
	if (!NT_SUCCESS(Status))
	{
		printf("Failed to load the driver: %X\n", Status);
		return EXIT_FAILURE;
	}

	CHAR Cmd = -1;
	// Constantly handle commands while we are provided valid ones
	while (Active)
	{
		printf("> ");

		// Parse input, ignore unknown inputs
		if (scanf_s(" %c", &Cmd, 1) == 0)
			continue;

		switch (Cmd)
		{
		// Exit and unload the driver
		case 'x':
		case 'X': Active = FALSE; break;
		// Get one log record and print it
		case 'l':
		case 'L':
		{
			CHAR Buffer[512] = {0};
			if (VmGetLogRecords(Buffer, 1) == HRESULT_SUCCESS)
				printf("%s", Buffer);
		} break;
		case 'r':
		case 'R':
		{
			VM_PID Pid = -1;
			PVOID Address = NULL;
			if (scanf_s(" %i %p", &Pid, &Address) == 2)
			{
				UINT64 Value = 0;
				// Try to read from the test variable
				HRESULT Result = VmReadMemory(Pid, Address, &Value, sizeof(UINT64));
				if (Result != HRESULT_SUCCESS)
					printf("VmReadMemory failed: %X\n", Result);
				else
				{
					printf("%p: %llX\n", Address, Value);
				}
			}
			else
			{
				printf("\n\tUsage: [R|r] [Process ID] [Address (Hex)] [Size (Hex)]\n\n");
			}
		} break;
		case 'p':
		case 'P':
		{
			CHAR ProcessName[16] = {0};
			// Read the target process name from the command line
			if (scanf_s(" %s", ProcessName, (unsigned int)sizeof(ProcessName)) == 1)
			{
				VM_PID Process = -1;
				// Try to find the process - this is actually useless to be in the hypervisor
				// NOTE: Rethink the use of the PDB in the hypervisor, it has proved useless so far
				// except when it comes to finding symbols for functions or data - types are so-far useless
				HRESULT Result = VmOpenProcess(FNV1A_HASH(ProcessName), &Process);
				if (Result != HRESULT_SUCCESS)
					printf("VmOpenProcess failed: %X\n", Result);
				else
				{
					printf("VmOpenProcess(%s): %i\n", ProcessName, Process);
				}
			}
			else
			{
				printf("\n\tUsage: [P|p] [process name]\n\n");
			}
		} break;
		case 'v':
		case 'V':
		{
			SIZE_T VpteCount = 0;
			// Try get the amount of VPTEs currently being used by the VMM
			HRESULT Result = VmGetActiveVpteCount(&VpteCount);
			if (Result != HRESULT_SUCCESS)
			{
				printf("VmGetActiveVpteCount failed: %X\n", Result);
				break;
			}

			printf("VPTE Count: %llX\n", VpteCount);
		} break;
		// Do nothing with unknown commands
		default: break;
		}
	}

	// Unload the driver
	NtUnloadDriver(&DriverRegistryPath);

	Err = RegDeleteKeyW(HKEY_LOCAL_MACHINE, REG_SERVICES_PATH);
	if (Err != ERROR_SUCCESS)
	{
		printf("Failed to delete driver service: %X\n", Err);
		RegCloseKey(DriverSvcKey);
		return EXIT_FAILURE;
	}
	
	RegCloseKey(DriverSvcKey);

	return 0;
}
