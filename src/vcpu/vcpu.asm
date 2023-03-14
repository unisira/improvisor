.code

__current_vcpu PROC
	mov		rax, fs:[0]
	ret
__current_vcpu ENDP

__vcpu_is_virtualised PROC
	xor rax, rax				; Clear RAX and RCX
	xor rcx, rcx				; 
	mov	rdx, 0441C88F24291161Fh	; 0x441C88F24291161F is a key used to alert the CPU that the following CPUID execution is us
								; and not some random actor, which we wouldn't want to alert to the hypervisors presence
								; TODO: Randomise this value
	cpuid
	ret							; Return, RAX will now have either have 1, if the hypervisor is present or 0 if not.
__vcpu_is_virtualised ENDP

END