
.code

__vmcall PROC
	mov rax, rcx
	mov rbx, rdx
	mov rcx, r8
	mov rdx, r9
	vmcall
	ret
__vmcall ENDP

END