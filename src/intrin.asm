
.code

__sgdt PROC
	lea		rax, [rcx]  
	sgdt	[rax]    
	xor		rax, rax
	ret
__sgdt ENDP

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
	invept rcx, oword ptr [rdx]
	ret
__invept ENDP

__invvpid PROC
	invvpid rcx, oword ptr [rdx]
	ret
__invvpid ENDP

__writecr2 PROC
	mov cr2, rcx
	ret
__writecr2 ENDP

__invd PROC
	invd
	ret
__invd ENDP

__enable PROC
	pushfq
	mov rax, 0200h
	or [rsp], rax
	popfq
	ret
__enable ENDP

END
