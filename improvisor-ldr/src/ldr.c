#include "macro.h"
#include "ldr.h"
#include "str.h"
#include "win.h"
#include "pe.h"

#include <Wininet.h>
#include <string.h>
#include <stdio.h>
#include <winnt.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ntdll.lib")

#define PDB_READY_EVENT_NAME (L"\\GX3A1RB5KTAOEVGYG85QTN3NFAFBK6I")
#define PDB_FINISHED_EVENT_NAME (L"\\YGJBOJ6YVPQ1XVA7WI2JRJY8E2UE5N1")

#define PE_IMAGE_CACHE_SIZE (64)

typedef enum _LDR_LAUNCH_FLAGS
{
	// Boot the improvisor with mitigations specific to BattlEye
	LDR_LAUNCH_BATTLEYE,
	// Boot the improvisor with mitigations specific to Easy Anti-Cheat
	LDR_LAUNCH_EAC,
	// Boot the improvisor and install all Watchmen features for analysis of anti-cheats
	LDR_LAUNCH_WATCHMEN,
	// The improvisor was loaded in debug mode, meaning no vulnerable driver was used
	LDR_LAUNCH_DEBUG
} LDR_LAUNCH_FLAGS, * PLDR_LAUNCH_FLAGS;

typedef struct _LDR_LAUNCH_PARAMS
{
	// The address the driver was loaded at
	PVOID ImageBase;
	// The size of the driver's image
	SIZE_T ImageSize;
	// The number of sections in `LDR_LAUNCH_PARAMS::Sections`
	SIZE_T SectionCount;
	// A list of PE section headers
	PIMAGE_SECTION_HEADER Sections;
	// The driver's image NT headers
	PIMAGE_NT_HEADERS Headers;
	// Launch flags controlling how the improvisor should initialise
	LDR_LAUNCH_FLAGS Flags;
	// The path to the exe which launched the improvisor, NULL if LDR_LAUNCH_PARAMS::Flags & LDR_LAUNCH_DEBUG is non-zero
	UNICODE_STRING ClientPath;
	// The process id of the client
	HANDLE ClientID;
} LDR_LAUNCH_PARAMS, * PLDR_LAUNCH_PARAMS;

typedef struct _LDR_PDB_PACKET {
	// Is this PDB packet valid?
	BOOLEAN Valid;
	// Name of the PDB file on disk
	CHAR FileName[64];
	// The base address of the executable associated with this PDB
	ULONG_PTR ImageBase;
	// The address of the buffer containing the PDB's contents
	ULONG_PTR PdbBase;
	// The size of the PDB buffer
	SIZE_T PdbSize;
} LDR_PDB_PACKET, * PLDR_PDB_PACKET;

typedef struct _LDR_SHARED_SECTION_FORMAT {
	// Parameters specific to booting the improvisor
	LDR_LAUNCH_PARAMS LdrParams;
	// The current PDB packet
	LDR_PDB_PACKET PdbPacket;
} LDR_SHARED_SECTION_FORMAT, * PLDR_SHARED_SECTION_FORMAT;

// Raw list of all cached PE images
static LDR_PE_IMAGE sPeImageCache[PE_IMAGE_CACHE_SIZE] = { 0 };
// The head of the PE image cache list, represents the currently free one
static PLDR_PE_IMAGE sPeImageCacheHead = NULL;

// List of images inside C:\Windows\System32 to cache
static LPCSTR sImageCacheList[] = {
	"ntoskrnl.exe",
};

// WININET Internet handle
HINTERNET hInternet = NULL;
// LDR shared section handle
HANDLE hSharedSection = NULL;

PLDR_PE_IMAGE
LdrFindPeImage(
	LPCSTR Name
)
{
	PLDR_PE_IMAGE CurrImage = sPeImageCacheHead;
	while (CurrImage != NULL)
	{
		if (strcmp(CurrImage->Name, Name) == 0)
			return CurrImage;

		CurrImage = (PLDR_PE_IMAGE)CurrImage->Links.Blink;
	}
}

VOID
LdrSetupImageCache(VOID)
{
	// Set the head to be the first entry
	sPeImageCacheHead = sPeImageCache;

	for (SIZE_T i = 0; i < PE_IMAGE_CACHE_SIZE; i++)
	{
		PLDR_PE_IMAGE CurrImgCache = sPeImageCache + i;

		// Set up Flink and Blink
		CurrImgCache->Links.Flink = i < PE_IMAGE_CACHE_SIZE + 1 ? &(CurrImgCache + 1)->Links : NULL;
		CurrImgCache->Links.Blink = i > 0						? &(CurrImgCache - 1)->Links : NULL;
	}

	// Cache all PDB's for any entries in `sImageCacheList`
	for (SIZE_T i = 0; i < (sizeof(sImageCacheList) / sizeof(*sImageCacheList)); i++)
	{
		if (!LdrCacheSystemImage(sImageCacheList[i]))
		{
			printf("[LDR] Failed to cache PDB for system image %s...\n", sImageCacheList[i]);
			return;
		}

		// Download the PDB for this image
		LdrDownloadPdb(LdrFindPeImage(sImageCacheList[i]));
	}
}

BOOL
LdrCacheSystemImage(
	LPCSTR Name
)
{
	CHAR SystemPath[MAX_PATH] = { 0 };
	SIZE_T SystemPathLen = GetSystemDirectory(SystemPath, MAX_PATH);
	if (SystemPathLen == 0)
		return FALSE;

	CHAR Path[MAX_PATH] = { 0 };
	wsprintf(Path, "%s\\%s", SystemPath, Name);

	HANDLE hFile = CreateFile(
		Path,
		GENERIC_READ,          // Open for reading
		FILE_SHARE_READ,       // Share for reading
		NULL,                  // Default security
		OPEN_EXISTING,         // Existing file only
		FILE_ATTRIBUTE_NORMAL, // Normal file
		NULL
	);

	if (hFile == INVALID_HANDLE_VALUE)
		return FALSE;

	SIZE_T FileSize = GetFileSize(hFile, NULL);

	SIZE_T SectorSize = 0;
	// Get sector size
	GetDiskFreeSpace(NULL, NULL, &SectorSize, NULL, NULL);
	// Align `FileSize` to `SectorSize`
	FileSize = ((SectorSize + ((FileSize + SectorSize) - 1)) & ~(SectorSize - 1));

	// Create a buffer big enough to hold the whole file
	PVOID ImageBuffer = malloc(FileSize);
	// Read the whole PE file
	if (!ReadFile(hFile, ImageBuffer, FileSize, NULL, NULL))
		return FALSE;

	CloseHandle(hFile);

	if (sPeImageCache->Links.Flink == NULL)
		return FALSE;

	PLDR_PE_IMAGE Image = sPeImageCacheHead;

	// Copy the name to the `LDR_PE_IMAGE::Name` field
	memcpy(Image->Name, Name, strlen(Name));
	Image->ImageBuffer = ImageBuffer;
	Image->ImageSize = FileSize;

	sPeImageCacheHead = (PLDR_PE_IMAGE)Image->Links.Flink;

	return TRUE;
}

VOID
LdrDownloadPdb(
	PLDR_PE_IMAGE Pe
)
{
	HINTERNET hUrl = NULL;

	SIZE_T DbgInfoSize = 0;
	PIMAGE_DEBUG_DIRECTORY DbgDir = PeImageDirectoryEntryToData(Pe->ImageBuffer, IMAGE_DIRECTORY_ENTRY_DEBUG, &DbgInfoSize);

	PIMAGE_DEBUG_INFORMATION DbgInfo = NULL;
	while (DbgInfoSize >= sizeof(PIMAGE_DEBUG_DIRECTORY))
	{
		// TODO: Handle different types of debug information
		if (*RVA_PTR_T(DWORD, Pe->ImageBuffer, DbgDir->PointerToRawData) == 'SDSR')
			DbgInfo = RVA_PTR(Pe->ImageBuffer, DbgDir->PointerToRawData);

		DbgInfoSize -= sizeof(IMAGE_DEBUG_DIRECTORY);
		// Advance to the next entry
		DbgDir++;
	}

	if (DbgInfo == NULL)
		return;

	CHAR GuidStr[64] = {0};
	wsprintf(
		GuidStr, 
		"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X", 
		DbgInfo->Guid.Data1, 
		DbgInfo->Guid.Data2, 
		DbgInfo->Guid.Data3, 
		DbgInfo->Guid.Data4[0],
		DbgInfo->Guid.Data4[1],
		DbgInfo->Guid.Data4[2],
		DbgInfo->Guid.Data4[3],
		DbgInfo->Guid.Data4[4],
		DbgInfo->Guid.Data4[5],
		DbgInfo->Guid.Data4[6],
		DbgInfo->Guid.Data4[7],
		DbgInfo->Age
	);

	CHAR Url[256] = {0};
	wsprintf(Url, "https://msdl.microsoft.com/download/symbols/%s/%s/%s", DbgInfo->PdbFileName, GuidStr, DbgInfo->PdbFileName);

	hUrl = InternetOpenUrl(
		hInternet, 
		Url, 
		NULL, 
		0, 
		INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE,
		0
	);

	if (hUrl == NULL)
		return;

	CHAR LenStr[256] = { 0 };
	SIZE_T LenResp = sizeof(LenStr);
	if (!HttpQueryInfo(hUrl, HTTP_QUERY_CONTENT_LENGTH, &LenStr, &LenResp, 0))
		return;

	// Convert the length string into an integer
	SIZE_T Len = atoi(LenStr);	

	printf("[%s] Content length: %llu\n", Url, Len);

	// Allocate a buffer for the PDB
	PVOID PdbBuffer = malloc(Len);
	if (PdbBuffer == NULL)
		return;

	CHAR RespCodeStr[256] = { 0 };
	SIZE_T RespCodeLen = sizeof(RespCodeStr);
	if (!HttpQueryInfo(hUrl, HTTP_QUERY_STATUS_CODE, &RespCodeStr, &RespCodeLen, 0))
		return;

	// Convert the response code string into an integer
	UINT32 RespCode = atoi(RespCodeStr);

	printf("[%s] Response code: %d\n", Url, RespCode);

	if (RespCode != 200)
		return;

	SIZE_T Size = 0, SizeDownloaded = 0;
	while (InternetReadFile(hUrl, RVA_PTR(PdbBuffer, SizeDownloaded), Len, &Size))
	{
		// Nothing left to download
		if (Size == 0)
			break;

		printf("[%s] Downloaded %llu bytes...\n", Url, Size);

		SizeDownloaded += Size;
	}

	printf("[%s] Downloaded PDB\n", Url);

#if 0
	Pe->PdbBuffer = PdbBuffer;
	Pe->PdbSize = SizeDownloaded;
#else
	HANDLE hFile = CreateFile(
		DbgInfo->PdbFileName,
		GENERIC_WRITE,			// Open for reading
		FILE_SHARE_WRITE,		// Share for reading
		NULL,					// Default security
		CREATE_ALWAYS,			// Create new file
		FILE_ATTRIBUTE_NORMAL,	// Normal file
		NULL
	);

	if (hFile == NULL)
		goto cleanup;

	if (!WriteFile(hFile, PdbBuffer, SizeDownloaded, NULL, NULL))
		goto cleanup;

cleanup:
	if (hFile != NULL)
		CloseHandle(hFile);

	// Free the PDB buffer
	free(PdbBuffer);
#endif
}

VOID
LdrCreatePdbPacket(
	VOID
)
{

}

BOOL
LdrCreatePdbEvents(
	VOID
)
{
	HANDLE PdbReady = NULL;
	HANDLE PdbFinished = NULL;

	PdbReady = CreateEventW(NULL, FALSE, FALSE, PDB_READY_EVENT_NAME);
	if (PdbReady == INVALID_HANDLE_VALUE)
		return FALSE;

	PdbFinished = CreateEventW(NULL, FALSE, FALSE, PDB_FINISHED_EVENT_NAME);
	if (PdbFinished == INVALID_HANDLE_VALUE)
		return FALSE;

	return TRUE;
}

VOID
LdrSendPdb(
	VOID
)
{

}

BOOL
LdrCreateSharedSection(
	VOID
)
{
	LARGE_INTEGER Size = {
		.QuadPart = sizeof(LDR_SHARED_SECTION_FORMAT)
	};

	UNICODE_STRING SharedSectionName;
	// TODO: Randomise this string
	RtlInitUnicodeString(&SharedSectionName, L"\\JRJY8E2U1XVRB5KTNFA1FBKA7E2WI2J");

	OBJECT_ATTRIBUTES Attr;
	// Setup opject attributes for the shared section
	InitializeObjectAttributes(
		&Attr,
		&SharedSectionName,
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
		NULL,
		NULL
	);

	if (!NT_SUCCESS(
		ZwCreateSection(
			&hSharedSection,
			SECTION_ALL_ACCESS,
			&Attr,
			&Size,
			PAGE_READWRITE,
			SEC_COMMIT,
			NULL
		)))
		return FALSE;

	return TRUE;
}

VOID
LdrSetup(
	VOID
)
{
	// Open a handle to the internet with the useragent for the symbol server
	hInternet = InternetOpen(
		"Microsoft-Symbol-Server/10.0.10522.521",
		INTERNET_OPEN_TYPE_PRECONFIG,
		NULL,
		NULL,
		0
	);

	if (!hInternet)
		return;

	ULONG32 Timeout = 1000;
	// Timeout after 1000ms
	InternetSetOption(hInternet,
		INTERNET_OPTION_RECEIVE_TIMEOUT,
		&Timeout,
		sizeof(ULONG32)
	);

	// Initialise PDB events
	if (!LdrCreatePdbEvents())
		return;

	// Create the shared section
	if (!LdrCreateSharedSection())
		return;

	// Initialise the image cache
	LdrSetupImageCache();
}

VOID
LdrCleanup(
	VOID
)
{

}
