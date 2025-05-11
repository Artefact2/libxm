cmake_minimum_required(VERSION 3.21)
project(test-libxm LANGUAGES C)

set(XM_RAMPING OFF CACHE BOOL "" FORCE)

include(CTest)
include(../common.cmake)
add_subdirectory(../src xm_build)

add_executable(test-libxm test-libxm.c)
target_link_libraries(test-libxm PRIVATE xm)

add_test(NAME test_arpeggio COMMAND test-libxm
	pat0_pat1_eq ${CMAKE_SOURCE_DIR}/arpeggio.xm)
add_test(NAME test_instrument_fadeout COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/instrument-fadeout.xm)
add_test(NAME test_key_off COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/key-off.xm)
add_test(NAME test_pattern_delay COMMAND test-libxm
	pat0_pat1_eq ${CMAKE_SOURCE_DIR}/pattern-delay.xm)
add_test(NAME test_sample_offset COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/sample-offset.xm)
add_test(NAME test_sample_offset_beyond_loop COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/sample-offset-beyond-loop.xm)
add_test(NAME test_sample_ping_pong COMMAND test-libxm
	channelpairs_lreqrl ${CMAKE_SOURCE_DIR}/sample-ping-pong.xm)
add_test(NAME test_volume_envelope COMMAND test-libxm
	channelpairs_lreqrl ${CMAKE_SOURCE_DIR}/volume-envelope.xm)
