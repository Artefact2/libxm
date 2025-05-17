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
add_test(NAME test_effect_memory COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/effect-memory.xm)
add_test(NAME test_instrument_fadeout COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/instrument-fadeout.xm)
add_test(NAME test_key_off COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/key-off.xm)
add_test(NAME test_note_delay COMMAND test-libxm
	pat0_pat1_eq ${CMAKE_SOURCE_DIR}/note-delay.xm)
add_test(NAME test_pattern_delay COMMAND test-libxm
	pat0_pat1_eq ${CMAKE_SOURCE_DIR}/pattern-delay.xm)
add_test(NAME test_pitch_slides COMMAND test-libxm
	channelpairs_pitcheq ${CMAKE_SOURCE_DIR}/pitch-slides.xm)
add_test(NAME test_pitch_slides_amiga COMMAND test-libxm
	channelpairs_pitcheq ${CMAKE_SOURCE_DIR}/pitch-slides-amiga.xm)
add_test(NAME test_sample_offset COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/sample-offset.xm)
add_test(NAME test_sample_offset_beyond_loop COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/sample-offset-beyond-loop.xm)
add_test(NAME test_sample_ping_pong COMMAND test-libxm
	channelpairs_lreqrl ${CMAKE_SOURCE_DIR}/sample-ping-pong.xm)
add_test(NAME test_tremolo COMMAND test-libxm
	pat0_pat1_eq ${CMAKE_SOURCE_DIR}/tremolo.xm)
add_test(NAME test_tremor COMMAND test-libxm
	pat0_pat1_eq ${CMAKE_SOURCE_DIR}/tremor.xm)
add_test(NAME test_trigger_types COMMAND test-libxm
	channelpairs_eq ${CMAKE_SOURCE_DIR}/trigger-types.xm)
add_test(NAME test_vibrato COMMAND test-libxm
	channelpairs_pitcheq ${CMAKE_SOURCE_DIR}/vibrato.xm)
add_test(NAME test_vibrato_amiga COMMAND test-libxm
	channelpairs_pitcheq ${CMAKE_SOURCE_DIR}/vibrato-amiga.xm)
add_test(NAME test_volume_envelope COMMAND test-libxm
	channelpairs_lreqrl ${CMAKE_SOURCE_DIR}/volume-envelope.xm)
