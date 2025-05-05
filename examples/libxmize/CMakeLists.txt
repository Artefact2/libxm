cmake_minimum_required(VERSION 3.21)
project(libxmize LANGUAGES C)

include(../../common.cmake)
add_subdirectory(../../src xm_build)

add_executable(libxmize libxmize.c)
target_link_libraries(libxmize PRIVATE xm)

add_executable(libxmtoau libxmtoau.c)
target_link_libraries(libxmtoau PRIVATE xm)
target_compile_definitions(libxmtoau PRIVATE
	LITTLE_ENDIAN=$<STREQUAL:${CMAKE_C_BYTE_ORDER},LITTLE_ENDIAN>
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
