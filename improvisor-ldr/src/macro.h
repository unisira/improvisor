#define RVA_PTR(Addr, Offs) \
	((PVOID)((PCHAR)Addr + (INT64)(Offs)))

#define RVA_PTR_T(T, Addr, Offs) \
	((T*)RVA_PTR((Addr), (Offs)))

#define RVA(Addr, Offs) \
	((UINT64)RVA_PTR((Addr), (Offs)))
