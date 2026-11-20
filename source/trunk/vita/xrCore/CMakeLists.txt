cmake_minimum_required(VERSION 3.16)

project(xrCore_vita LANGUAGES CXX)

add_library(xrCore_vita STATIC
    crc32.cpp
    file_reader.cpp
    native_file_reader.cpp
    vita_find_file.cpp
    vita_stream_reader.cpp
    critical_section.cpp
    xrSyncronize_vita.cpp
    threading_vita.cpp
    xrMemory_vita.cpp
    xrMath_vita.cpp
    xrDebug_vita.cpp
    FileSystem_vita.cpp
    ../../xrCore/_compressed_normal.cpp
    ../../xrCore/cpuid.cpp
    ../../xrCore/xr_trims.cpp
    ../../xrCore/rt_compressor.cpp
    ../../xrCore/rt_lzo1x_1.cpp
    ../../xrCore/rt_lzo1x_d1.cpp
    ../../xrCore/rt_lzo_init.cpp
    ../../xrCore/xrstring.cpp
    ../../xrCore/xrsharedmem.cpp
    ../../xrCore/LocatorAPI_defs.cpp
    ../../xrCore/FS.cpp
    ../../xrCore/xrCore.cpp
    ../../xrCore/log.cpp
    ../../xrCore/LzHuf.cpp
    ../../xrCore/stream_reader.cpp
    ../../xrCore/file_stream_reader.cpp
    ../../xrCore/LocatorAPI.cpp
)

target_include_directories(xrCore_vita PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../xrCore"
)

set_target_properties(xrCore_vita PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)

target_compile_options(xrCore_vita PRIVATE
    -Wall
    -Wextra
    -fno-operator-names
)

target_link_libraries(xrCore_vita PUBLIC SceIofilemgr_stub SceKernelThreadMgr_stub)
