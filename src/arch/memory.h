#ifndef IMP_ARCH_MEMORY_H
#define IMP_ARCH_MEMORY_H

#include <ntdef.h>

#define PAGE_FRAME_NUMBER(Addr) ((UINT64)(Addr) >> 12)
#define PAGE_ADDRESS(Pfn) ((UINT64)(Pfn) << 12)
#define PAGE_ALIGN(Addr) ((UINT64)(Addr) & ~0xFFFULL)
#define PAGE_END_ALIGN(Addr) ((UINT64)(Addr + PAGE_SIZE) & ~0xFFFULL)
#define PAGE_OFFSET(Addr) ((UINT64)(Addr) & 0xFFFULL)
#define GB(N) ((UINT64)(N) * 1024 * 1024 * 1024)
#define MB(N) ((UINT64)(N) * 1024 * 1024)
#define KB(N) ((UINT64)(N) * 1024)


#define RVA_PTR(Addr, Offs) \
	((PUCHAR)Addr + (UINT64)Offs)

#define RVA(Addr, Offs) \
	((UINT64)RVA_PTR(Addr, Offs))

typedef union _X86_LA48
{
	UINT64 Value;

	struct
	{
		UINT64 PageOffset : 12;
		UINT64 PtIndex : 9;
		UINT64 PdIndex : 9;
		UINT64 PdptIndex : 9;
		UINT64 Pml4Index : 9;
		UINT64 Reserved1 : 16;
	};
} X86_LA48, *PX86_LA48;

typedef union _X86_LA57
{
	UINT64 Value;

	struct
	{
		UINT64 PageOffset : 12;
		UINT64 PtIndex : 9;
		UINT64 PdIndex : 9;
		UINT64 PdptIndex : 9;
		UINT64 Pml4Index : 9;
		UINT64 Pml5Index : 9;
		UINT64 Reserved1 : 7;
	};
} X86_LA57, *PX86_LA57;

#endif
