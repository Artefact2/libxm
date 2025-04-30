CMAKE_MINIMUM_REQUIRED(VERSION 3.20)
PROJECT(libxm C)

FUNCTION(OPTION_AND_DEFINE name description default_value)
OPTION(${name} ${description} ${default_value})
IF(${name})
ADD_DEFINITIONS(-D${name}=1)
ELSE()
ADD_DEFINITIONS(-D${name}=0)
ENDIF()
ENDFUNCTION()

OPTION_AND_DEFINE(XM_DEBUG "Enable debug symbols and print debugging messages to stderr" "ON")
OPTION_AND_DEFINE(XM_DEFENSIVE "Defensively check XM data for errors/inconsistencies (do not play unknown files with this OFF)" "ON")

OPTION_AND_DEFINE(XM_LINEAR_INTERPOLATION "Use linear interpolation (CPU hungry)" "ON")
OPTION_AND_DEFINE(XM_RAMPING "Enable ramping (smooth volume/panning transitions, CPU hungry)" "ON")
OPTION_AND_DEFINE(XM_STRINGS "Store module, instrument and sample names in context" "ON")

OPTION_AND_DEFINE(XM_LIBXMIZE_DELTA_SAMPLES "Delta-code samples in libxmize format" "ON")
MARK_AS_ADVANCED(XM_LIBXMIZE_DELTA_SAMPLES)

SET(XM_FREQUENCY_TYPES "3" CACHE STRING "Supported frequency types (1=Linear,2=Amiga,3=Both)")
MARK_AS_ADVANCED(XM_FREQUENCY_TYPES)
ADD_DEFINITIONS("-DXM_FREQUENCY_TYPES=${XM_FREQUENCY_TYPES}")



OPTION(XM_BUILD_SHARED_LIBS "Build shared library" "ON")
OPTION(XM_BUILD_EXAMPLES "Build example programs" "ON")
OPTION(XM_OPTIMISE_FOR_SIZE "Optimize for size (for static linking)" "OFF")

ADD_DEFINITIONS("-Wall -Wextra -Wpadded -Wdouble-promotion -Wpedantic --std=c11 -fshort-enums")

IF(CMAKE_C_BYTE_ORDER STREQUAL "BIG")
ADD_DEFINITIONS("-DXM_BIG_ENDIAN=1")
ELSE()
ADD_DEFINITIONS("-DXM_BIG_ENDIAN=0")
ENDIF()

IF(XM_DEBUG)
ADD_DEFINITIONS("-g -Og")
ENDIF()

IF(XM_OPTIMISE_FOR_SIZE)
ADD_DEFINITIONS("-Oz -ffast-math -fdata-sections -ffunction-sections -flto -fuse-linker-plugin -fvisibility=hidden")
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
ELSEIF(NOT XM_DEBUG)
ADD_DEFINITIONS("-O2")
ENDIF()

LIST(APPEND XM_INCLUDE_DIRS "${libxm_SOURCE_DIR}/include")
LIST(APPEND XM_LIBRARIES "m")

ADD_SUBDIRECTORY(src)

IF(XM_BUILD_EXAMPLES)
ADD_SUBDIRECTORY(examples)
ENDIF()
