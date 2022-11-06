#ifndef IMP_DBG_H
#define IMP_DBG_H

#include <ntdef.h>
#include <ntimage.h>

typedef struct _RUNTIME_FUNCTION
{
    UINT32 BeginAddress;
    UINT32 EndAddress;
    union {
        UINT32 UnwindInfoAddress;
        UINT32 UnwindData;
    };
} RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

typedef enum _UNWIND_OP_CODES
{
    UWOP_PUSH_NONVOL = 0,
    UWOP_ALLOC_LARGE,
    UWOP_ALLOC_SMALL,
    UWOP_SET_FPREG,
    UWOP_SAVE_NONVOL,
    UWOP_SAVE_NONVOL_FAR,
    UWOP_EPILOG,
    UWOP_SPARE_CODE,
    UWOP_SAVE_XMM128,
    UWOP_SAVE_XMM128_FAR,
    UWOP_PUSH_MACHFRAME
} UNWIND_CODE_OPS;

typedef union _UNWIND_CODE
{
    struct
    {
        CHAR CodeOffset;
        CHAR UnwindOp : 4;
        CHAR OpInfo : 4;
    } u;
    USHORT FrameOffset;
} UNWIND_CODE, * PUNWIND_CODE;

typedef struct _UNWIND_INFO
{
    CHAR Version : 3;
    CHAR Flags : 5;
    CHAR SizeOfProlog;
    CHAR CountOfCodes;
    CHAR FrameRegister : 4;
    CHAR FrameOffset : 4;
    UNWIND_CODE UnwindCode[1]; /* actually CountOfCodes (aligned) */
/*
 *  union
 *  {
 *      OPTIONAL ULONG ExceptionHandler;
 *      OPTIONAL ULONG FunctionEntry;
 *  };
 *  OPTIONAL ULONG ExceptionData[];
 */
} UNWIND_INFO, * PUNWIND_INFO;

#endif