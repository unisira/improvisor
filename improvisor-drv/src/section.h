#ifndef IMP_SECTION_H
#define IMP_SECTION_H

#define DECLSPEC_NOINLINE \
	__declspec(noinline)

#pragma section(".VSC", read, execute)
#pragma section(".VMMC", read, execute)
#pragma section(".VMMD", read, write)
#pragma section(".VMMSD", read, write)
#pragma section(".VMMRD", read)

#pragma data_seg(".VMMD$1")
#pragma data_seg(".VMMD$2")
#pragma data_seg(".VMMD$3")
#pragma data_seg()

#pragma data_seg(".VMMSD$1")
#pragma data_seg(".VMMSD$2")
#pragma data_seg(".VMMSD$3")
#pragma data_seg()

#pragma const_seg(".VMMRD$1")
#pragma const_seg(".VMMRD$2")
#pragma const_seg(".VMMRD$3")
#pragma const_seg()

#pragma code_seg(".VMMC$1")
#pragma code_seg(".VMMC$2")
#pragma code_seg(".VMMC$3")
#pragma code_seg()

#pragma code_seg(".VSC$1")
#pragma code_seg(".VSC$2")
#pragma code_seg(".VSC$3")
#pragma code_seg()

#define VMM_API \
	__declspec(code_seg(".VMMC$2")) DECLSPEC_NOINLINE

#define VSC_API \
	__declspec(code_seg(".VSC$2")) DECLSPEC_NOINLINE

#define VMM_DATA \
	__declspec(allocate(".VMMD$2"))

#define VMM_SDATA \
	__declspec(allocate(".VMMD$2"))

#define VMM_RDATA \
	__declspec(allocate(".VMMRD$2"))

#endif
