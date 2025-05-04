add_custom_command(OUTPUT mus.libxm
	COMMAND libxmize --zero-all-waveforms
		${CMAKE_CURRENT_SOURCE_DIR}/mus.xm > mus.libxm
	DEPENDS libxmize mus.xm)
set_source_files_properties(xmprocdemo.c
	PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mus.libxm)

add_executable(xmprocdemo xmprocdemo.c)
target_link_libraries(xmprocdemo PRIVATE xm)
target_compile_options(xmprocdemo PRIVATE --embed-dir=.)
target_compile_definitions(xmprocdemo PRIVATE
	LITTLE_ENDIAN=$<STREQUAL:${CMAKE_C_BYTE_ORDER},LITTLE_ENDIAN>)
