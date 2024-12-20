## Improvisor - Hypervisor for System Introspection

A simple, well documented X86 VMX hypervisor for Windows which exposes the following functionality through hypercalls:
- Kernel-mode function hooking
- Arbitrary read-write of any processes address space
- Windows kernel introspection (process lookup, etc.)

It also implements TSC spoofing to bypass basic timing attack checks, and properly virtualizes many X86 features which are often incorrectly virtualised
