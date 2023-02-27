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
	_In_ PVOID ImageBase,
	_In_ PVOID Pdb
);

#endif
