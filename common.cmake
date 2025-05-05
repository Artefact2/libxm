# Is included by libxm and all examples

set(CMAKE_C_STANDARD 23)

# Use LTO everywhere
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# Link statically by default
option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

if(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$")
	add_compile_options(
		"$<$<CONFIG:MinSizeRel>:-Oz>"
		"$<$<CONFIG:MinSizeRel>:-ffast-math>"
		-Wall -Wextra -Wpadded -Wdouble-promotion -Wpedantic
	)
	add_link_options(
		"$<$<CONFIG:MinSizeRel>:-z>"
		"$<$<CONFIG:MinSizeRel>:norelro>"
		"$<$<CONFIG:MinSizeRel>:-Wl,--build-id=none>"
	)
endif()
