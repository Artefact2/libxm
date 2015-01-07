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

OPTION_AND_DEFINE(XM_DEBUG "Enable debugging messages" "ON")
OPTION_AND_DEFINE(XM_BIG_ENDIAN "Assume big endian byte order (default: little endian assumed)" "OFF")
OPTION_AND_DEFINE(XM_LINEAR_INTERPOLATION "Use linear interpolation (CPU hungry)" "ON")
OPTION_AND_DEFINE(XM_RAMPING "Enable ramping (smooth volume/panning transitions, CPU hungry)" "ON")

OPTION(XM_BUILD_SHARED_LIBS "Build shared library" "ON")
OPTION(XM_BUILD_EXAMPLES "Build example programs" "ON")

ADD_DEFINITIONS("-g -Os -Wall -pedantic --std=c11")
ADD_DEFINITIONS("-fdata-sections -ffunction-sections")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -dead_strip")
ELSE()
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
ENDIF()

LIST(APPEND XM_INCLUDE_DIRS "${libxm_SOURCE_DIR}/include")
LIST(APPEND XM_LIBRARIES "m")

ADD_SUBDIRECTORY(src)

IF(XM_BUILD_EXAMPLES)
ADD_SUBDIRECTORY(examples)
ENDIF()

