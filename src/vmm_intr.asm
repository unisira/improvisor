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
	add rsp, 010h	; Skip the vector and error code on the stack
	iretq			; Return to code again
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
VMM_INTR_HANDLER 0, __vmm_intr_gate_0
VMM_INTR_HANDLER 1, __vmm_intr_gate_1
VMM_INTR_HANDLER 2, __vmm_intr_gate_2
VMM_INTR_HANDLER 3, __vmm_intr_gate_3
VMM_INTR_HANDLER 4, __vmm_intr_gate_4
VMM_INTR_HANDLER 5, __vmm_intr_gate_5
VMM_INTR_HANDLER 6, __vmm_intr_gate_6
VMM_INTR_HANDLER 7, __vmm_intr_gate_7
VMM_INTR_HANDLER 8, __vmm_intr_gate_8
VMM_INTR_HANDLER 9, __vmm_intr_gate_9
VMM_INTR_HANDLER 10, __vmm_intr_gate_10
VMM_INTR_HANDLER 11, __vmm_intr_gate_11
VMM_INTR_HANDLER 12, __vmm_intr_gate_12
VMM_INTR_HANDLER 13, __vmm_intr_gate_13
VMM_INTR_HANDLER 14, __vmm_intr_gate_14
VMM_INTR_HANDLER 15, __vmm_intr_gate_15
VMM_INTR_HANDLER 16, __vmm_intr_gate_16
VMM_INTR_HANDLER 17, __vmm_intr_gate_17
VMM_INTR_HANDLER 18, __vmm_intr_gate_18
VMM_INTR_HANDLER 19, __vmm_intr_gate_19
VMM_INTR_HANDLER 20, __vmm_intr_gate_20
VMM_INTR_HANDLER 21, __vmm_intr_gate_21
VMM_INTR_HANDLER 22, __vmm_intr_gate_22
VMM_INTR_HANDLER 23, __vmm_intr_gate_23
VMM_INTR_HANDLER 24, __vmm_intr_gate_24
VMM_INTR_HANDLER 25, __vmm_intr_gate_25
VMM_INTR_HANDLER 26, __vmm_intr_gate_26
VMM_INTR_HANDLER 27, __vmm_intr_gate_27
VMM_INTR_HANDLER 28, __vmm_intr_gate_28
VMM_INTR_HANDLER 29, __vmm_intr_gate_29
VMM_INTR_HANDLER 30, __vmm_intr_gate_30
VMM_INTR_HANDLER 31, __vmm_intr_gate_31
VMM_INTR_HANDLER 32, __vmm_intr_gate_32

; Define unknown host interrupt handler
VMM_INTR_HANDLER 0FFFFFFFFFFFFFFFFh, __vmm_intr_gate_unk

END