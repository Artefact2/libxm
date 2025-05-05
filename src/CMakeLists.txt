cmake_minimum_required(VERSION 3.21)
project(libxm LANGUAGES C)

include(../common.cmake)

add_library(xm xm.c load.c play.c)

# Bump this when breaking public ABI
set_target_properties(xm PROPERTIES SOVERSION 4)

find_library(MATH_LIBRARY m REQUIRED)
target_include_directories(xm SYSTEM PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/../include")
target_compile_options(xm PRIVATE -fshort-enums)
target_link_libraries(xm PRIVATE ${MATH_LIBRARY})
target_compile_definitions(xm PRIVATE
	XM_BIG_ENDIAN=$<STREQUAL:${CMAKE_C_BYTE_ORDER},BIG_ENDIAN>
	XM_DEBUG=$<BOOL:$<CONFIG:Debug>>
)

function(option_and_define name description default_value)
	option(${name} ${description} ${default_value})
	if(${name})
                target_compile_definitions(xm PRIVATE ${name}=1)
	else()
		target_compile_definitions(xm PRIVATE ${name}=0)
	endif()
endfunction()

option_and_define(XM_DEFENSIVE
	"Defensively check XM data for errors/inconsistencies (do not play unknown files with this OFF)"
	"ON")

option_and_define(XM_LINEAR_INTERPOLATION
	"Use linear interpolation (CPU hungry)"
	"ON")

option_and_define(XM_RAMPING
	"Enable ramping (smooth volume/panning transitions, CPU hungry)"
	"ON")

option_and_define(XM_LIBXM_DELTA_SAMPLES
	"Delta-code samples in libxm format (may improve compressibility, but adds some code size)"
	"ON")

set(XM_SAMPLE_TYPE "float" CACHE STRING
	"Sample type of internal samples (int8_t,int16_t,float)")

option_and_define(XM_STRINGS
	"Store module, instrument and sample names in context" "ON")

set(XM_FREQUENCY_TYPES "3" CACHE STRING
	"Supported frequency types (1=Linear,2=Amiga,3=Both)")
target_compile_definitions(xm PRIVATE XM_FREQUENCY_TYPES=${XM_FREQUENCY_TYPES})

configure_file(
	../include/xm.h.in
	../include/xm.h
	@ONLY
)
