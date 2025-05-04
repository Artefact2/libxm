cmake_minimum_required(VERSION 3.21)
project(xmprocdemo LANGUAGES C)

include(../../common.cmake)
add_subdirectory(../../src xm_build)

set(OpenGL_GL_PREFERENCE LEGACY)
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_search_module(JACK REQUIRED IMPORTED_TARGET jack)
pkg_search_module(GLFW REQUIRED IMPORTED_TARGET glfw3)

add_executable(xmgl xmgl.c)
target_link_libraries(xmgl
	PRIVATE xm OpenGL::GL PkgConfig::JACK PkgConfig::GLFW
)
