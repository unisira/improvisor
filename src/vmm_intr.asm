EXTERN VcpuHandleHostException: PROC

VCPU_TRAP_FRAME STRUCT
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
VCPU_TRAP_FRAME ENDS

VCPU_MACHINE_FRAME STRUCT
_Vector	QWORD ?
_Error	QWORD ?
_Rip	QWORD ?
_Cs		QWORD ?
_RFlags	QWORD ?
_Rsp	QWORD ?
_Ss		QWORD ?
VCPU_MACHINE_FRAME ENDS

.code

; This design is stolen from the darwin kernel, thanks Apple
__vmm_generic_intr_handler proc
	sub rsp, 8										; Fix stack alignment to 16-bytes 

	sub rsp, SIZEOF VCPU_TRAP_FRAME

	mov [rsp].VCPU_TRAP_FRAME._Rax, rax				; Save guest system state
	mov [rsp].VCPU_TRAP_FRAME._Rbx, rbx
	mov [rsp].VCPU_TRAP_FRAME._Rcx, rcx
	mov [rsp].VCPU_TRAP_FRAME._Rdx, rdx
	mov [rsp].VCPU_TRAP_FRAME._Rsi, rsi
	mov [rsp].VCPU_TRAP_FRAME._Rdi, rdi
	mov [rsp].VCPU_TRAP_FRAME._R8, r8
	mov [rsp].VCPU_TRAP_FRAME._R9, r9
	mov [rsp].VCPU_TRAP_FRAME._R10, r10
	mov [rsp].VCPU_TRAP_FRAME._R11, r11
	mov [rsp].VCPU_TRAP_FRAME._R12, r12
	mov [rsp].VCPU_TRAP_FRAME._R13, r13
	mov [rsp].VCPU_TRAP_FRAME._R14, r14
	mov [rsp].VCPU_TRAP_FRAME._R15, r15
	movups [rsp].VCPU_TRAP_FRAME._Xmm0, xmm0
	movups [rsp].VCPU_TRAP_FRAME._Xmm1, xmm1
	movups [rsp].VCPU_TRAP_FRAME._Xmm2, xmm2
	movups [rsp].VCPU_TRAP_FRAME._Xmm3, xmm3
	movups [rsp].VCPU_TRAP_FRAME._Xmm4, xmm4
	movups [rsp].VCPU_TRAP_FRAME._Xmm5, xmm5
	movups [rsp].VCPU_TRAP_FRAME._Xmm6, xmm6
	movups [rsp].VCPU_TRAP_FRAME._Xmm7, xmm7
	movups [rsp].VCPU_TRAP_FRAME._Xmm8, xmm8
	movups [rsp].VCPU_TRAP_FRAME._Xmm9, xmm9
	movups [rsp].VCPU_TRAP_FRAME._Xmm10, xmm10
	movups [rsp].VCPU_TRAP_FRAME._Xmm11, xmm11
	movups [rsp].VCPU_TRAP_FRAME._Xmm12, xmm12
	movups [rsp].VCPU_TRAP_FRAME._Xmm13, xmm13
	movups [rsp].VCPU_TRAP_FRAME._Xmm14, xmm14
	movups [rsp].VCPU_TRAP_FRAME._Xmm15, xmm15

	mov rcx, fs:[0]							; Load the address of the current VCPU into RCX
	lea rdx, [rsp] 							; Load the address of the CPU_TRAP_FRAME into RDX

	sub rsp, 28h							; Allocate shadow stack space
	call VcpuHandleHostException
	add rsp, 28h							; Get rid of shadow stack space

	mov rax, [rsp].VCPU_TRAP_FRAME._Rax			
	mov rbx, [rsp].VCPU_TRAP_FRAME._Rbx
	mov rcx, [rsp].VCPU_TRAP_FRAME._Rcx
	mov rdx, [rsp].VCPU_TRAP_FRAME._Rdx
	mov rsi, [rsp].VCPU_TRAP_FRAME._Rsi
	mov rdi, [rsp].VCPU_TRAP_FRAME._Rdi
	mov r8, [rsp].VCPU_TRAP_FRAME._R8
	mov r9, [rsp].VCPU_TRAP_FRAME._R9
	mov r10, [rsp].VCPU_TRAP_FRAME._R10
	mov r11, [rsp].VCPU_TRAP_FRAME._R11
	mov r12, [rsp].VCPU_TRAP_FRAME._R12
	mov r13, [rsp].VCPU_TRAP_FRAME._R13
	mov r14, [rsp].VCPU_TRAP_FRAME._R14
	mov r15, [rsp].VCPU_TRAP_FRAME._R15
	movups xmm0, [rsp].VCPU_TRAP_FRAME._Xmm0
	movups xmm1, [rsp].VCPU_TRAP_FRAME._Xmm1
	movups xmm2, [rsp].VCPU_TRAP_FRAME._Xmm2
	movups xmm3, [rsp].VCPU_TRAP_FRAME._Xmm3
	movups xmm4, [rsp].VCPU_TRAP_FRAME._Xmm4
	movups xmm5, [rsp].VCPU_TRAP_FRAME._Xmm5
	movups xmm6, [rsp].VCPU_TRAP_FRAME._Xmm6
	movups xmm7, [rsp].VCPU_TRAP_FRAME._Xmm7
	movups xmm8, [rsp].VCPU_TRAP_FRAME._Xmm8
	movups xmm9, [rsp].VCPU_TRAP_FRAME._Xmm9
	movups xmm10, [rsp].VCPU_TRAP_FRAME._Xmm10
	movups xmm11, [rsp].VCPU_TRAP_FRAME._Xmm11
	movups xmm12, [rsp].VCPU_TRAP_FRAME._Xmm12
	movups xmm13, [rsp].VCPU_TRAP_FRAME._Xmm13
	movups xmm14, [rsp].VCPU_TRAP_FRAME._Xmm14
	movups xmm15, [rsp].VCPU_TRAP_FRAME._Xmm15

	add rsp, SIZEOF VCPU_TRAP_FRAME

	add rsp, 16	; Skip the vector and error code on the stack
	iretq		; Return to code again
__vmm_generic_intr_handler endp

; Define __vmm_intr_handler_X for each exception
; These stubs will push some vector & error code onto the stack then jump to __vmm_generic_intr_handler

VMM_INTR_HANDLER macro vector: req, name: req
name proc
	push 0
	push vector
	jmp __vmm_generic_intr_handler
name endp
endm

; Define NMI Host interrupt handler
VMM_INTR_HANDLER 2, __vmm_intr_gate_2

END