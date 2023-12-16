#define RVA_PTR(Addr, Offs) \
	((PVOID)((PUCHAR)Addr + (UINT64)Offs))

#define RVA_PTR_T(T, Addr, Offs) \
	((T*)RVA_PTR(Addr, Offs))

#define RVA(Addr, Offs) \
	((UINT64)RVA_PTR((Addr), (Offs)))