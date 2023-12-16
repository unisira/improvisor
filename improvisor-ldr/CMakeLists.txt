cmake_minimum_required(VERSION 3.16)

enable_language(C ASM_MASM)

# set the project name and version
project(improvisor-gdrv)

# add the executable
add_executable(${PROJECT_NAME}
	"src/main.c"
	"src/gdrv.c"
	"src/ldr.c"
	"src/pe.c"
)

set_property(TARGET ${PROJECT_NAME} PROPERTY
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
