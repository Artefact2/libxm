add_executable(libxmize libxmize.c)
target_link_libraries(libxmize PRIVATE xm)

add_executable(libxmtoau libxmtoau.c)
target_link_libraries(libxmtoau PRIVATE xm)
target_compile_definitions(libxmtoau PRIVATE
	LITTLE_ENDIAN=$<STREQUAL:${CMAKE_C_BYTE_ORDER},LITTLE_ENDIAN>
	ENTRY=$<IF:$<CONFIG:MinSizeRel>,_start,main>
)
target_link_options(libxmtoau PRIVATE
	$<$<CONFIG:MinSizeRel>:-nostartfiles>
)

add_subdirectory(xmgl)
add_subdirectory(xmprocdemo)
