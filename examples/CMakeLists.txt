INCLUDE_DIRECTORIES(${XM_INCLUDE_DIRS})

ADD_EXECUTABLE(libxmtoau libxmtoau.c)
TARGET_LINK_LIBRARIES(libxmtoau ${XM_LIBRARIES} xms)
target_link_options(libxmtoau PRIVATE -z norelro)
if(NOT XM_DEBUG)
target_link_options(libxmtoau PRIVATE -nostartfiles)
endif()


ADD_SUBDIRECTORY(xmprocdemo)
ADD_SUBDIRECTORY(xmgl)
