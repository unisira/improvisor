#ifndef IMP_INTRIN_H
#define IMP_INTRIN_H

EXTERN_C
VOID
__sgdt(PX86_PSEUDO_DESCRIPTOR);

EXTERN_C
UINT64
__segmentar(X86_SEGMENT_SELECTOR);

EXTERN_C
UINT16
__readldt(VOID);

EXTERN_C
UINT16
__readtr(VOID);

EXTERN_C
UINT16
__readcs(VOID);

EXTERN_C
UINT16
__readss(VOID);

EXTERN_C
UINT16
__readds(VOID);

EXTERN_C
UINT16
__reades(VOID);

EXTERN_C
UINT16
__readfs(VOID);

EXTERN_C
UINT16
__readgs(VOID);

EXTERN_C
VOID
__enable(VOID);

EXTERN_C
VOID
__writecr2(UINT64);

EXTERN_C
VOID
__invept(UINT64, PVOID);

EXTERN_C
VOID
__invvpid(UINT64, PVOID);

EXTERN_C
VOID
__invd(void);

#endif
