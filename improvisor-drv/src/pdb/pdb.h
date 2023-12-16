#ifndef IMP_PDB_H
#define IMP_PDB_H

#include <ntdef.h>
#include <wdm.h>
#include <hash.h>

typedef struct _PDB_SYMBOL_RESULT
{
	UINT32 Segment;
	UINT32 Offset;
} PDB_SYMBOL_RESULT, *PPDB_SYMBOL_RESULT;

#if 0
#define PDB_CACHE_MEMBER_OFFSET(Pdb, Structure, Member) \
	VMM_DATA static SIZE_T s##Member##Offset = -1;		\
														\
	if (s##Member##Offset == -1)						\
	{													\
		s##Member##Offset = PdbFindMemberOffset(		\
			FNV1A_HASH(Pdb),							\
			FNV1A_HASH(#Structure),						\
			FNV1A_HASH(#Member)							\
		);												\
	}													
#endif

// Offsets and addresses cached from PDB parsing
// TODO: Hashmap?
// KTHREAD::Process
extern ULONG_PTR gProcessOffset;
// EPROCESS::DirectoryTableBase
extern ULONG_PTR gDirectoryTableBaseOffset;
// EPROCESS::ActiveProcessLinks
extern ULONG_PTR gActiveProcessLinksOffset;
// EPROCESS::ImageFileName
extern ULONG_PTR gImageFileNameOffset;
// EPROCESS::UniqueProcessId
extern ULONG_PTR gUniqueProcessIdOffset;
// KPCR::CurrentThread
extern ULONG_PTR gCurrentThreadOffset;

NTSTATUS
PdbReserveEntries(
	_In_ SIZE_T Count
);

PDB_SYMBOL_RESULT
PdbFindSymbol(
	_In_ FNV1A Pdb,
	_In_ FNV1A Name,
	_In_ UINT64 Flags
);

SIZE_T
PdbFindMemberOffset(
	_In_ FNV1A Pdb,
	_In_ FNV1A Structure,
	_In_ FNV1A Member
);

NTSTATUS
PdbParseFile(
	_In_ FNV1A Name,
	_In_ PVOID Pdb
);

NTSTATUS
PdbCacheOffsets(VOID);

#endif
