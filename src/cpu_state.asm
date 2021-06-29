.code

CPU_STATE STRUCT
_Rax QWORD ?
_Rbx QWORD ?
_Rcx QWORD ?
_Rdx QWORD ?
_Rbp QWORD ?
_Rsp QWORD ?
_Rsi QWORD ?
_Rdi QWORD ?
_R8 QWORD ?
_R9 QWORD ?
_R10 QWORD ?
_R11 QWORD ?
_R12 QWORD ?
_R13 QWORD ?
_R14 QWORD ?
_R15 QWORD ?
_Rbp QWORD ?
_Rsp QWORD ?
_Rip QWORD ?
_MxCsr DWORD ?
_Xmm0 XMMWORD ?
_Xmm1 XMMWORD ?
_Xmm2 XMMWORD ?
_Xmm3 XMMWORD ?
_Xmm4 XMMWORD ?
_Xmm5 XMMWORD ?
_Xmm6 XMMWORD ?
_Xmm7 XMMWORD ?
_Xmm8 XMMWORD ?
_Xmm9 XMMWORD ?
_Xmm10 XMMWORD ?
_Xmm11 XMMWORD ?
_Xmm12 XMMWORD ?
_Xmm13 XMMWORD ?
_Xmm14 XMMWORD ?
_Xmm15 XMMWORD ?
_RFlags QWORD ?
CPU_STATE ENDS

__cpu_save_state PROC
    pushfq
    mov [rcx].CPU_STATE._Rax, rax
    mov [rcx].CPU_STATE._Rbx, rbx
    mov [rcx].CPU_STATE._Rcx, rcx
    mov [rcx].CPU_STATE._Rdx, rdx,
    mov [rcx].CPU_STATE._Rdx, rdx,
    mov [rcx].CPU_STATE._R8, r8,
    mov [rcx].CPU_STATE._R9, r9,
    mov [rcx].CPU_STATE._R10, r10,
    mov [rcx].CPU_STATE._R11, r11,
    mov [rcx].CPU_STATE._R12, r12,
    mov [rcx].CPU_STATE._R13, r13,
    mov [rcx].CPU_STATE._R14, r14,
    mov [rcx].CPU_STATE._R15, r15,
    mov [rcx].CPU_STATE._Rbp, rbp,
    lea rax, [rsp+8]
    mov [rcx].CPU_STATE._Rsp, rax,
    mov rax, [rax]
    mov [rcx].CPU_STATE._Rip, rax
    mov rax, [rcx].CPU_STATE._Rax
    pop [rcx].CPU_STATE._RFlags
    ret
__cpu_save_state ENDP
