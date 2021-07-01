EXTERN VcpuHandleExit: PROC

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
_MxCsr DWORD ?
_Align1 DWORD ?
_Align2 QWORD ?
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
	movaps [rsp].GUEST_STATE._Xmm0, xmm0
	movaps [rsp].GUEST_STATE._Xmm1, xmm1
	movaps [rsp].GUEST_STATE._Xmm2, xmm2
	movaps [rsp].GUEST_STATE._Xmm3, xmm3
	movaps [rsp].GUEST_STATE._Xmm4, xmm4
	movaps [rsp].GUEST_STATE._Xmm5, xmm5
	movaps [rsp].GUEST_STATE._Xmm6, xmm6
	movaps [rsp].GUEST_STATE._Xmm7, xmm7
	movaps [rsp].GUEST_STATE._Xmm8, xmm8
	movaps [rsp].GUEST_STATE._Xmm9, xmm9
	movaps [rsp].GUEST_STATE._Xmm10, xmm10
	movaps [rsp].GUEST_STATE._Xmm11, xmm11
	movaps [rsp].GUEST_STATE._Xmm12, xmm12
	movaps [rsp].GUEST_STATE._Xmm13, xmm13
	movaps [rsp].GUEST_STATE._Xmm14, xmm14
	movaps [rsp].GUEST_STATE._Xmm15, xmm15

	mov rax, [rsp]
	mov [rsp].GUEST_STATE._Rip, rax
	stmxcsr [rsp].GUEST_STATE._MxCsr

	lea rcx, [rsp+SIZEOF GUEST_STATE-6000h]	; Load the address of the stack cache (PVCPU) into RCX
	lea rdx, [rsp] 							; Load the address of the GUEST_STATE into RDX
	sub rsp, 28h							; Allocate shadow stack space
	call VcpuHandleExit						; Call the VMEXIT handler
	add rsp, 28h							; Remove shadow stack space
	
	ldmxcsr [rsp].GUEST_STATE._MxCsr
	mov rax, [rsp].GUEST_STATE._Rip			; Restore guest system state
	mov [rsp+SIZEOF GUEST_STATE], rax
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
	movaps xmm0, [rsp].GUEST_STATE._Xmm0
	movaps xmm1, [rsp].GUEST_STATE._Xmm1
	movaps xmm2, [rsp].GUEST_STATE._Xmm2
	movaps xmm3, [rsp].GUEST_STATE._Xmm3
	movaps xmm4, [rsp].GUEST_STATE._Xmm4
	movaps xmm5, [rsp].GUEST_STATE._Xmm5
	movaps xmm6, [rsp].GUEST_STATE._Xmm6
	movaps xmm7, [rsp].GUEST_STATE._Xmm7
	movaps xmm8, [rsp].GUEST_STATE._Xmm8
	movaps xmm9, [rsp].GUEST_STATE._Xmm9
	movaps xmm10, [rsp].GUEST_STATE._Xmm10
	movaps xmm11, [rsp].GUEST_STATE._Xmm11
	movaps xmm12, [rsp].GUEST_STATE._Xmm12
	movaps xmm13, [rsp].GUEST_STATE._Xmm13
	movaps xmm14, [rsp].GUEST_STATE._Xmm14
	movaps xmm15, [rsp].GUEST_STATE._Xmm15
	add rsp, SIZEOF GUEST_STATE				; Cleanup stack
	ret										; Return to GUEST_STATE.Rip
__vmexit_entry ENDP

END
