EXTERN VcpuHandleExit: PROC
EXTERN VcpuShutdownVmx: PROC
EXTERN __cpu_save_state: PROC
EXTERN __cpu_restore_state: PROC

VMM_STATUS_ABORT equ 0

GUEST_STATE STRUCT
_Rax QWORD ?
_Rbx QWORD ?
_Rcx QWORD ?
_Rdx QWORD ?
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
_Rip QWORD ?
_Cr8 QWORD ?
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
GUEST_STATE ENDS

CPU_STATE STRUCT
_Rax QWORD ?
_Rbx QWORD ?
_Rcx QWORD ?
_Rdx QWORD ?
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

.code

__vmexit_entry PROC
	sub rsp, SIZEOF GUEST_STATE					; We are on the host stack now, which is guaranteed to be 16-byte aligned
	mov [rsp].GUEST_STATE._Rax, rax				; Save guest system state
	mov [rsp].GUEST_STATE._Rbx, rbx
	mov [rsp].GUEST_STATE._Rcx, rcx
	mov [rsp].GUEST_STATE._Rdx, rdx
	mov [rsp].GUEST_STATE._Rsi, rsi
	mov [rsp].GUEST_STATE._Rdi, rdi
	mov [rsp].GUEST_STATE._R8, r8
	mov [rsp].GUEST_STATE._R9, r9
	mov [rsp].GUEST_STATE._R10, r10
	mov [rsp].GUEST_STATE._R11, r11
	mov [rsp].GUEST_STATE._R12, r12
	mov [rsp].GUEST_STATE._R13, r13
	mov [rsp].GUEST_STATE._R14, r14
	mov [rsp].GUEST_STATE._R15, r15
	movups [rsp].GUEST_STATE._Xmm0, xmm0
	movups [rsp].GUEST_STATE._Xmm1, xmm1
	movups [rsp].GUEST_STATE._Xmm2, xmm2
	movups [rsp].GUEST_STATE._Xmm3, xmm3
	movups [rsp].GUEST_STATE._Xmm4, xmm4
	movups [rsp].GUEST_STATE._Xmm5, xmm5
	movups [rsp].GUEST_STATE._Xmm6, xmm6
	movups [rsp].GUEST_STATE._Xmm7, xmm7
	movups [rsp].GUEST_STATE._Xmm8, xmm8
	movups [rsp].GUEST_STATE._Xmm9, xmm9
	movups [rsp].GUEST_STATE._Xmm10, xmm10
	movups [rsp].GUEST_STATE._Xmm11, xmm11
	movups [rsp].GUEST_STATE._Xmm12, xmm12
	movups [rsp].GUEST_STATE._Xmm13, xmm13
	movups [rsp].GUEST_STATE._Xmm14, xmm14
	movups [rsp].GUEST_STATE._Xmm15, xmm15
	mov rbx, cr8
	mov rax, 0Fh
	mov cr8, rax
	mov [rsp].GUEST_STATE._Cr8, rbx
	stmxcsr [rsp].GUEST_STATE._MxCsr

	mov rcx, [rsp+SIZEOF GUEST_STATE-5FF0h]	; Load the address of the stack cache (PVCPU) into RCX
	lea rdx, [rsp] 							; Load the address of the GUEST_STATE into RDX
	sub rsp, 28h							; Allocate shadow stack space
	call VcpuHandleExit						; Call the VM-exit handler
	add rsp, 28h							; Remove shadow stack space

    test rax, rax							; Test if RAX == 0
    jne no_abort                            ; TODO: Check for KVA shadowing
	add rsp, SIZEOF GUEST_STATE				; Get rid of GUEST_STATE data
	sub rsp, SIZEOF CPU_STATE				; Allocate CPU_STATE
	lea rcx, [rsp]							; RCX = &CPU_STATE
	call __cpu_save_state
	mov rdx, rcx							; Set second parameter as the CPU_STATE
	mov rcx, [rsp+SIZEOF GUEST_STATE-5FF0h]	; Load the address of the stack cache (PVCPU) into RCX
	sub rsp, 28h							
	call VcpuShutdownVmx					; Shutdown VMX and restore guest register state
	int 3									; VcpuShutdownVmx doesn't return							

no_abort:
	ldmxcsr [rsp].GUEST_STATE._MxCsr		; Restore register state
	mov rax, [rsp].GUEST_STATE._Rip	
	mov [rsp+SIZEOF GUEST_STATE], rax
	mov rax, [rsp].GUEST_STATE._Cr8
	mov cr8, rax
	mov rax, [rsp].GUEST_STATE._Rax			
	mov rbx, [rsp].GUEST_STATE._Rbx
	mov rcx, [rsp].GUEST_STATE._Rcx
	mov rdx, [rsp].GUEST_STATE._Rdx
	mov rsi, [rsp].GUEST_STATE._Rsi
	mov rdi, [rsp].GUEST_STATE._Rdi
	mov r8, [rsp].GUEST_STATE._R8
	mov r9, [rsp].GUEST_STATE._R9
	mov r10, [rsp].GUEST_STATE._R10
	mov r11, [rsp].GUEST_STATE._R11
	mov r12, [rsp].GUEST_STATE._R12
	mov r13, [rsp].GUEST_STATE._R13
	mov r14, [rsp].GUEST_STATE._R14
	mov r15, [rsp].GUEST_STATE._R15
	movups xmm0, [rsp].GUEST_STATE._Xmm0
	movups xmm1, [rsp].GUEST_STATE._Xmm1
	movups xmm2, [rsp].GUEST_STATE._Xmm2
	movups xmm3, [rsp].GUEST_STATE._Xmm3
	movups xmm4, [rsp].GUEST_STATE._Xmm4
	movups xmm5, [rsp].GUEST_STATE._Xmm5
	movups xmm6, [rsp].GUEST_STATE._Xmm6
	movups xmm7, [rsp].GUEST_STATE._Xmm7
	movups xmm8, [rsp].GUEST_STATE._Xmm8
	movups xmm9, [rsp].GUEST_STATE._Xmm9
	movups xmm10, [rsp].GUEST_STATE._Xmm10
	movups xmm11, [rsp].GUEST_STATE._Xmm11
	movups xmm12, [rsp].GUEST_STATE._Xmm12
	movups xmm13, [rsp].GUEST_STATE._Xmm13
	movups xmm14, [rsp].GUEST_STATE._Xmm14
	movups xmm15, [rsp].GUEST_STATE._Xmm15
	add rsp, SIZEOF GUEST_STATE				; Cleanup stack
	ret										
__vmexit_entry ENDP

END
