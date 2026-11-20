cmake_minimum_required(VERSION 3.16)

project(xrRender_vita_gl LANGUAGES CXX)

find_path(VITAGL_INCLUDE_DIR vitaGL.h
	HINTS "$ENV{VITASDK}/arm-vita-eabi/include"
	REQUIRED
)

find_library(VITAGL_LIBRARY vitaGL
	HINTS "$ENV{VITASDK}/arm-vita-eabi/lib"
	REQUIRED
)

add_library(xrRender_vita_gl STATIC
	vita_gl_geometry.cpp
	vita_gl_program.cpp
	vita_gl_vertex_format.cpp
	vita_gl_r1_buffers.cpp
	vita_gl_device.cpp
	vita_gl_debug_renderer.cpp
	vita_gl_texture.cpp
	vita_gl_sky.cpp
)

target_include_directories(xrRender_vita_gl PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${VITAGL_INCLUDE_DIR}"
)

target_compile_options(xrRender_vita_gl PRIVATE
	-fno-operator-names
)

target_link_libraries(xrRender_vita_gl PUBLIC
	xrCore_vita
	"${VITAGL_LIBRARY}"
	vitashark
	SceShaccCgExt
	taihen_stub
	mathneon
	SceAppMgr_stub
	SceAppUtil_stub
	SceCommonDialog_stub
	SceDisplay_stub
	SceGxm_stub
	SceKernelDmacMgr_stub
	SceKernelThreadMgr_stub
	SceShaccCg_stub
	SceSysmodule_stub
	ScePower_stub
	m
	stdc++
	c
)

set_target_properties(xrRender_vita_gl PROPERTIES
	CXX_STANDARD 11
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS ON
)
