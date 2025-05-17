cmake_minimum_required(VERSION 3.21)
project(xmprocdemo LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

add_subdirectory(../../src xm_build)

set(OpenGL_GL_PREFERENCE LEGACY)
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_search_module(JACK REQUIRED IMPORTED_TARGET jack)
pkg_search_module(GLFW REQUIRED IMPORTED_TARGET glfw3)

add_executable(xmgl xmgl.c)
target_link_libraries(xmgl
	PRIVATE xm xm_common m OpenGL::GL PkgConfig::JACK PkgConfig::GLFW
)
