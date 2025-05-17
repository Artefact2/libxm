cmake_minimum_required(VERSION 3.21)
project(libxmize LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

add_subdirectory(../../src xm_build)

add_executable(libxmize libxmize.c)
target_link_libraries(libxmize PRIVATE xm xm_common)

add_executable(libxmtoau libxmtoau.c)
target_link_libraries(libxmtoau PRIVATE xm xm_common)
target_compile_definitions(libxmtoau PRIVATE
	XM_LITTLE_ENDIAN=$<STREQUAL:${CMAKE_C_BYTE_ORDER},LITTLE_ENDIAN>
)

if(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$")
	target_compile_definitions(libxmtoau PRIVATE
		ENTRY=$<IF:$<CONFIG:MinSizeRel>,_start,main>
	)
	target_link_options(libxmtoau PRIVATE
		$<$<CONFIG:MinSizeRel>:-nostartfiles>
	)

else()
	target_compile_definitions(libxmtoau PRIVATE ENTRY=main)
endif()
