cmake_minimum_required(VERSION 3.21)
project(test-libxm LANGUAGES C)

include(CTest)
include(../common.cmake)
add_subdirectory(../src xm_build)

add_executable(test-libxm test-libxm.c)
target_link_libraries(test-libxm PRIVATE xm)

set(XM_RAMPING OFF CACHE BOOL "" FORCE)

add_test(NAME test_sample_offset COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/sample-offset.xm)
add_test(NAME test_sample_ping_pong COMMAND test-libxm
	channelpairs_lreqrl ${CMAKE_SOURCE_DIR}/sample-ping-pong.xm)
add_test(NAME test_volume_envelope COMMAND test-libxm
	channelpairs_lreqrl ${CMAKE_SOURCE_DIR}/volume-envelope.xm)
