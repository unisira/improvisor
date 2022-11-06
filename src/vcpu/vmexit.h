#ifndef IMP_VCPU_VMEXIT_H
#define IMP_VCPU_VMEXIT_H

typedef VMM_EVENT_STATUS VMEXIT_HANDLER(_Inout_ PVCPU, _Inout_ PGUEST_STATE);

// TODO: Figure out how to make this allocated in 
EXTERN_C
VOID
__vmexit_entry(VOID);

#endif