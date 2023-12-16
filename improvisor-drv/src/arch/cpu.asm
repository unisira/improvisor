
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

__cpu_save_state PROC
	mov		[rcx].CPU_STATE._Rax, rax
	mov		[rcx].CPU_STATE._Rbx, rbx
	mov		[rcx].CPU_STATE._Rcx, rcx
	mov		[rcx].CPU_STATE._Rdx, rdx
	mov		[rcx].CPU_STATE._Rdx, rsi
	mov		[rcx].CPU_STATE._Rdx, rdi
	mov		[rcx].CPU_STATE._R8, r8
	mov		[rcx].CPU_STATE._R9, r9
	mov		[rcx].CPU_STATE._R10, r10
	mov		[rcx].CPU_STATE._R11, r11
	mov		[rcx].CPU_STATE._R12, r12
	mov		[rcx].CPU_STATE._R13, r13
	mov		[rcx].CPU_STATE._R14, r14
	mov		[rcx].CPU_STATE._R15, r15
	mov		[rcx].CPU_STATE._Rbp, rbp
	movups	[rcx].CPU_STATE._Xmm0, xmm0
	movups	[rcx].CPU_STATE._Xmm1, xmm1
	movups	[rcx].CPU_STATE._Xmm2, xmm2
	movups	[rcx].CPU_STATE._Xmm3, xmm3
	movups	[rcx].CPU_STATE._Xmm4, xmm4
	movups	[rcx].CPU_STATE._Xmm5, xmm5
	movups	[rcx].CPU_STATE._Xmm6, xmm6
	movups	[rcx].CPU_STATE._Xmm7, xmm7
	movups	[rcx].CPU_STATE._Xmm8, xmm8
	movups	[rcx].CPU_STATE._Xmm9, xmm9
	movups	[rcx].CPU_STATE._Xmm10, xmm10
	movups	[rcx].CPU_STATE._Xmm11, xmm11
	movups	[rcx].CPU_STATE._Xmm12, xmm12
	movups	[rcx].CPU_STATE._Xmm13, xmm13
	movups	[rcx].CPU_STATE._Xmm14, xmm14
	movups	[rcx].CPU_STATE._Xmm15, xmm15
	stmxcsr [rcx].CPU_STATE._MxCsr
	lea		rax, [rsp]
	mov		[rcx].CPU_STATE._Rsp, rax
	mov		rax, [rsp]
	mov		[rcx].CPU_STATE._Rip, rax
	mov		rax, [rcx].CPU_STATE._Rax
	pushfq
	pop		[rcx].CPU_STATE._RFlags
	ret
__cpu_save_state ENDP

__cpu_restore_state PROC
	push	[rcx].CPU_STATE._RFlags
	mov		rbx, [rcx].CPU_STATE._Rbx
	mov		rcx, [rcx].CPU_STATE._Rcx
	mov		rdx, [rcx].CPU_STATE._Rdx
	mov		rsi, [rcx].CPU_STATE._Rsi
	mov		rdi, [rcx].CPU_STATE._Rdi
	mov		r8, [rcx].CPU_STATE._R8
	mov		r9, [rcx].CPU_STATE._R9
	mov		r10, [rcx].CPU_STATE._R10
	mov		r11, [rcx].CPU_STATE._R11
	mov		r12, [rcx].CPU_STATE._R12
	mov		r13, [rcx].CPU_STATE._R13
	mov		r14, [rcx].CPU_STATE._R14
	mov		r15, [rcx].CPU_STATE._R15
	mov		rbp, [rcx].CPU_STATE._Rbp
	movups	xmm0, [rcx].CPU_STATE._Xmm0
	movups	xmm1, [rcx].CPU_STATE._Xmm1
	movups	xmm2, [rcx].CPU_STATE._Xmm2
	movups	xmm3, [rcx].CPU_STATE._Xmm3
	movups	xmm4, [rcx].CPU_STATE._Xmm4
	movups	xmm5, [rcx].CPU_STATE._Xmm5
	movups	xmm6, [rcx].CPU_STATE._Xmm6
	movups	xmm7, [rcx].CPU_STATE._Xmm7
	movups	xmm8, [rcx].CPU_STATE._Xmm8
	movups	xmm9, [rcx].CPU_STATE._Xmm9
	movups	xmm10, [rcx].CPU_STATE._Xmm10
	movups	xmm11, [rcx].CPU_STATE._Xmm11
	movups	xmm12, [rcx].CPU_STATE._Xmm12
	movups	xmm13, [rcx].CPU_STATE._Xmm13
	movups	xmm14, [rcx].CPU_STATE._Xmm14
	movups	xmm15, [rcx].CPU_STATE._Xmm15
	ldmxcsr [rcx].CPU_STATE._MxCsr
	popfq

	mov		rsp, [rcx].CPU_STATE._Rsp
	mov		rax, [rcx].CPU_STATE._Rip
	mov		[rsp], rax
	mov		rax, [rcx].CPU_STATE._Rax 
	ret
__cpu_restore_state ENDP


__readmsr PROC
	xor 	rax, rax
	xor 	rdx, rdx
	rdmsr
	shl		rdx, 32
	or		rax, rdx
	ret
__readmsr ENDP

__writemsr PROC
	mov 	rax, rcx
	mov 	rdx, rax
	shr 	rdx, 32
	shl 	rdx, 32
	rdmsr
	xor 	rax, rax
	ret
__writemsr ENDP

__sgdt PROC
	lea		rax, [rcx]  
	sgdt	[rax]    
	xor		rax, rax
	ret
__sgdt ENDP

__lgdt PROC
	lea		rax, [rcx]
	lgdt	fword ptr [rax]
	xor		rax, rax
	ret
__lgdt ENDP

__readldt PROC
	sldt    ax
	ret
__readldt ENDP

__readtr PROC
	str     ax
	ret
__readtr ENDP

__readcs PROC
	mov     ax, cs
	ret
__readcs ENDP

__readss PROC
	mov     ax, ss
	ret
__readss ENDP

__readds PROC
	mov     ax, ds
	ret
__readds ENDP

__reades PROC
	mov     ax, es
	ret
__reades ENDP

__readfs PROC
	mov     ax, fs
	ret
__readfs ENDP

__readgs PROC
	mov     ax, gs
	ret
__readgs ENDP

__segmentar PROC
		lar     rax, rcx 
		jz      no_error 
		xor     rax, rax 
no_error:
		ret
__segmentar ENDP

__invept PROC
	invept	rcx, oword ptr [rdx]
	ret
__invept ENDP

__invvpid PROC
	invvpid	rcx, oword ptr [rdx]
	ret
__invvpid ENDP

__writecr2 PROC
	mov		cr2, rcx
	ret
__writecr2 ENDP

__invd PROC
	invd
	ret
__invd ENDP

__enable PROC
	pushfq
	mov		rax, 0200h
	or [	rsp], rax
	popfq
	ret
__enable ENDP

__disable PROC
	pushfq
	mov		rax, 0200h
	xor		rax, 0FFFFFFFFh
	and		[rsp], rax
	popfq
	ret
__disable ENDP

__invlpg PROC
	invlpg byte ptr [rcx]
	ret
__invlpg ENDP

END
