cmake_minimum_required(VERSION 3.21)
project(xmprocdemo LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# Force a few options for this specific example (we're always playing back the
# same module anyway)
foreach(X XM_VERBOSE XM_LINEAR_INTERPOLATION
		XM_RAMPING XM_LIBXM_DELTA_SAMPLES XM_STRINGS
		XM_TIMING_FUNCTIONS XM_MUTING_FUNCTIONS)
	set(${X} OFF CACHE BOOL "" FORCE)
endforeach()
set(XM_DISABLED_EFFECTS "0xFFFFFFFFFFFF42E1" CACHE STRING "" FORCE)
set(XM_DISABLED_VOLUME_EFFECTS "0xEC00" CACHE STRING "" FORCE)
set(XM_DISABLED_FEATURES "0xFFFFFFFFFFBFEE8C" CACHE STRING "" FORCE)
set(XM_LOOPING_TYPE "0" CACHE STRING "" FORCE)
set(XM_SAMPLE_TYPE "int16_t" CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(../libxmize libxmize_build)

add_custom_command(OUTPUT mus.libxm
	COMMAND libxmize --zero-all-waveforms
		${CMAKE_CURRENT_SOURCE_DIR}/mus.xm > mus.libxm
	DEPENDS libxmize mus.xm)
set_source_files_properties(xmprocdemo.c
	PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/mus.libxm)

add_executable(xmprocdemo xmprocdemo.c)
target_link_libraries(xmprocdemo PRIVATE xm xm_common)
target_compile_options(xmprocdemo PRIVATE --embed-dir=.)
target_compile_definitions(xmprocdemo PRIVATE
	LITTLE_ENDIAN=$<STREQUAL:${CMAKE_C_BYTE_ORDER},LITTLE_ENDIAN>)
