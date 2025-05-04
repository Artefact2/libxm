cmake_minimum_required(VERSION 3.21)

project(libxm LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE) # use LTO everywhere

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
option(XM_BUILD_EXAMPLES "Build example programs" ON)

# XXX: not portable, check for gcc?
add_compile_options(
        "$<$<CONFIG:MinSizeRel>:-Oz>"
	"$<$<CONFIG:MinSizeRel>:-fshort-enums>"
	"$<$<CONFIG:MinSizeRel>:-ffast-math>"
	-Wall -Wextra -Wpadded -Wdouble-promotion -Wpedantic
)
add_link_options(
	"$<$<CONFIG:MinSizeRel>:-z>"
	"$<$<CONFIG:MinSizeRel>:norelro>"
	"$<$<CONFIG:MinSizeRel>:-Wl,--build-id=none>"
)

add_subdirectory(src)

if(XM_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()
