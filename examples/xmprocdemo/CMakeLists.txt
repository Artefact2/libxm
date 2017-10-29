ADD_CUSTOM_COMMAND(OUTPUT mus.libxm COMMAND libxmize ${CMAKE_CURRENT_SOURCE_DIR}/mus.xm mus.libxm DEPENDS libxmize mus.xm)
ADD_CUSTOM_COMMAND(OUTPUT mus.h COMMAND filetoh mus mus.libxm mus.h DEPENDS filetoh mus.libxm)

ADD_EXECUTABLE(xmprocdemo xmprocdemo.c mus.h)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})
TARGET_LINK_LIBRARIES(xmprocdemo ${ALSA_LIBRARIES} ${XM_LIBRARIES} xms)
