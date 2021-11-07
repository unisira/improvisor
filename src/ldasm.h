#ifndef IMP_LDASM_H
#define IMP_LDASM_H

#include <ntdef.h>

#define F_INVALID       0x01
#define F_PREFIX        0x02
#define F_REX           0x04
#define F_MODRM         0x08
#define F_SIB           0x10
#define F_DISP          0x20
#define F_IMM           0x40
#define F_RELATIVE      0x80

typedef struct _ldasm_data
{
	UINT8  flags;
	UINT8  rex;
	union
	{
		UINT8 all;
		struct
		{
			UINT8 rm : 3;
			UINT8 reg : 3;
			UINT8 mod : 2;
		} fields;
	} modrm;
	UINT8  sib;
	UINT8  opcd_offset;
	UINT8  opcd_size;
	UINT8  disp_offset;
	UINT8  disp_size;
	UINT8  imm_offset;
	UINT8  imm_size;
} ldasm_data;

UINT32 ldasm(const void *code, ldasm_data *ld, BOOLEAN is64);

#endif

