CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
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
OPTION_AND_DEFINE(XM_DEFENSIVE "Defensively check XM data for errors/inconsistencies" "ON")
OPTION_AND_DEFINE(XM_BIG_ENDIAN "Use big endian byte order (unfinished)" "OFF")

OPTION_AND_DEFINE(XM_LINEAR_INTERPOLATION "Use linear interpolation (CPU hungry)" "ON")
OPTION_AND_DEFINE(XM_RAMPING "Enable ramping (smooth volume/panning transitions, CPU hungry)" "ON")
OPTION_AND_DEFINE(XM_STRINGS "Store module, instrument and sample names in context" "OFF")
OPTION_AND_DEFINE(XM_LIBXMIZE_DELTA_SAMPLES "Delta-code samples in libxmize format" "ON")

OPTION(XM_BUILD_SHARED_LIBS "Build shared library" "ON")
OPTION(XM_BUILD_EXAMPLES "Build example programs" "ON")
OPTION(XM_DEMO_MODE "Optimize for size (then link statically against libxms and strip dead code)" "OFF")

ADD_DEFINITIONS("-Wall -pedantic --std=c11")

IF(XM_DEBUG)
ADD_DEFINITIONS("-g")
ENDIF()

IF(XM_DEMO_MODE)
ADD_DEFINITIONS("-Os -fdata-sections -ffunction-sections -flto -fuse-linker-plugin -fvisibility=hidden")
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
ELSE()
ADD_DEFINITIONS("-O2")
ENDIF()

LIST(APPEND XM_INCLUDE_DIRS "${libxm_SOURCE_DIR}/include")
LIST(APPEND XM_LIBRARIES "m")

ADD_SUBDIRECTORY(src)

IF(XM_BUILD_EXAMPLES)
ADD_SUBDIRECTORY(examples)
ENDIF()
