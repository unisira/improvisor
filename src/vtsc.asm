
.code

__vtsc_estimate_vmexit_latency PROC
    push rbx
    ; VcpuHandleHypercall checks GuestState::Rbx to be this value
    mov rbx, 01FF2C88911424416h
    ; Measure TSC right before forcing a VM-exit
    rdtsc
    ; VMCALL is quickest VM-exiting instruction, its only purpose is to exit
    vmcall
    ; Restore RBX
    pop rbx
    ; The difference between RAX:RDX and the value stored in the VM-exit MSR store area
    ; is stored in RAX, we can just return
    ret
__vtsc_estimate_vmexit_latency ENDP

__vtsc_estimate_vmentry_latency PROC
    push rbx
    ; VcpuHandleHypercall checks GuestState::Rbx to be this value
    mov rbx, 0F2C889114244161Fh
    ; Force a VM-exit, VMX preemption timer will be activated
    vmcall
    ; Restore RBX
    pop rbx
    ; The elapsed time it took to enter then exit again, minus the VM-exit latency is now in RAX
    ret
__vtsc_estimate_vmentry_latency ENDP

__vtsc_estimate_cpuid_latency PROC
    ; Disable interrupts
    mov rbx, cr8
    mov rax, 0Fh
    mov cr8, rax
    ; Measure TSC just before CPUID execution
    rdtsc
    ; Store TSC value into R8
    shl rdx, 32
    or rax, rdx
    mov r8, rax
    ; Execute CPUID
    mov eax, 1
    cpuid
    ; Measure TSC again
    rdtsc
    ; Move TSC value into RAX and subtract the previous value
    shl rdx, 32
    or rax, rdx
    sub rax, r8
    movzx rbx, bl
    mov cr8, rbx
    ret
__vtsc_estimate_cpuid_latency ENDP

__vtsc_estimate_rdtsc_latency PROC
    ; Disable interrupts
    mov rbx, cr8
    mov rax, 0Fh
    mov cr8, rax
    ; Measure TSC just before CPUID execution
    rdtsc
    ; Store TSC value into R8
    shl rdx, 32
    or rax, rdx
    mov r8, rax
    ; Execute RDTSC
    rdtsc
    ; Measure TSC again
    rdtsc
    ; Move TSC value into RAX and subtract the previous value
    shl rdx, 32
    or rax, rdx
    sub rax, r8
    movzx rbx, bl
    mov cr8, rbx
    ret
__vtsc_estimate_rdtsc_latency ENDP

__vtsc_estimate_rdtscp_latency PROC
    ; Disable interrupts
    mov rbx, cr8
    mov rax, 0Fh
    mov cr8, rax
    ; Measure TSC just before CPUID execution
    rdtsc
    ; Store TSC value into R8
    shl rdx, 32
    or rax, rdx
    mov r8, rax
    ; Execute RDTSCP
    rdtscp
    lfence                  ; Make sure RDTSCP has finished
    ; Measure TSC again
    rdtsc
    ; Move TSC value into RAX and subtract the previous value
    shl rdx, 32
    or rax, rdx
    sub rax, r8
    movzx rbx, bl
    mov cr8, rbx
    ret
__vtsc_estimate_rdtscp_latency ENDP

END
