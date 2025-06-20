cmake_minimum_required(VERSION 3.21)
project(libxm LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

add_library(xm_common INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$")
	target_compile_options(xm_common INTERFACE
		"$<$<CONFIG:MinSizeRel>:-Oz>"
		"$<$<CONFIG:MinSizeRel>:-ffast-math>"
		-Wall -Wextra -Wpedantic -Wconversion
		-Wpadded -Wdouble-promotion -Wvla
	)
	target_link_options(xm_common INTERFACE
		"$<$<CONFIG:MinSizeRel>:-z>"
		"$<$<CONFIG:MinSizeRel>:norelro>"
		"$<$<CONFIG:MinSizeRel>:-Wl,--build-id=none>"
	)
endif()

add_library(xm xm.c load.c play.c analyze.c)
set_target_properties(xm PROPERTIES
	PUBLIC_HEADER ${CMAKE_CURRENT_BINARY_DIR}/xm.h)

# Bump this when breaking public ABI
set_target_properties(xm PROPERTIES SOVERSION 8)

find_library(MATH_LIBRARY m REQUIRED)
target_include_directories(xm SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(xm PRIVATE xm_common ${MATH_LIBRARY})

function(option_and_define name description default_value)
	option(${name} ${description} ${default_value})
	if(${name})
                target_compile_definitions(xm PRIVATE ${name}=1)
	else()
		target_compile_definitions(xm PRIVATE ${name}=0)
	endif()
endfunction()

option_and_define(XM_VERBOSE
	"Print informational messages to stderr"
	"ON")

option_and_define(XM_LINEAR_INTERPOLATION
	"Use linear interpolation (FT2-like, CPU hungry)"
	"ON")

option_and_define(XM_RAMPING
	"Enable ramping (FT2-like, CPU hungry)"
	"ON")

option_and_define(XM_LIBXM_DELTA_SAMPLES
	"Delta-code samples in libxm format (may improve compressibility, but adds some code size)"
	"ON")

option_and_define(XM_STRINGS
	"Store module, instrument and sample names in context" "ON")

option_and_define(XM_TIMING_FUNCTIONS
	"Enable xm_latest_trigger_of_*() functions for instruments, samples and channels" "ON")

option_and_define(XM_MUTING_FUNCTIONS
	"Enable xm_mute_*() functions for instruments and channels" "ON")

set(XM_SAMPLE_TYPE "int16_t" CACHE STRING
	"Sample type of internal samples (int8_t,int16_t,float)")

set(XM_MICROSTEP_BITS "12" CACHE STRING
	"Precision of sample stepping (8..12, higher = better pitch accuracy, lower = can load larger samples)")
target_compile_definitions(xm PRIVATE XM_MICROSTEP_BITS=${XM_MICROSTEP_BITS})

set(XM_PANNING_TYPE "8" CACHE STRING
	"Panning type (0=Mono, 1..7=Hard Amiga panning, 8=Full stereo panning)")
target_compile_definitions(xm PRIVATE XM_PANNING_TYPE=${XM_PANNING_TYPE})

set(XM_LOOPING_TYPE "2" CACHE STRING
	"Looping type (0=Loop forever, 1=Loop once, 2=Runtime based via xm_set_max_loop_count())")
target_compile_definitions(xm PRIVATE XM_LOOPING_TYPE=${XM_LOOPING_TYPE})

set(XM_DISABLED_EFFECTS "0" CACHE STRING
	"Bit mask of effects to disable, see xm_internal.h for EFFECT_ constants")
target_compile_definitions(xm PRIVATE "XM_DISABLED_EFFECTS=${XM_DISABLED_EFFECTS}ULL")

set(XM_DISABLED_VOLUME_EFFECTS "0" CACHE STRING
	"Bit mask of volume effects to disable, see xm_internal.h for VOLUME_EFFECT_ constants")
target_compile_definitions(xm PRIVATE "XM_DISABLED_VOLUME_EFFECTS=${XM_DISABLED_VOLUME_EFFECTS}ULL")

set(XM_DISABLED_FEATURES "0" CACHE STRING
	"Bit mask of features to disable, see xm_internal.h for FEATURE_ constants")
target_compile_definitions(xm PRIVATE "XM_DISABLED_FEATURES=${XM_DISABLED_FEATURES}ULL")

configure_file(xm.h.in xm.h @ONLY)
