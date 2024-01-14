cmake_minimum_required(VERSION 3.16)

# add the executable
add_executable(improvisor-ldr
	src/main.c
	src/vmcall.asm
	src/vmcall.c
)

set_target_properties(improvisor-ldr PROPERTIES
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

