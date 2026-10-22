cmake_minimum_required(VERSION 3.21)

project(GravityDesktopZipDependencies LANGUAGES C)

# Keep this dependency build fully static and ABI-compatible with the main
# /MT Release targets.  The application itself owns directory traversal and
# calls the library only with validated, UTF-8 relative entry names.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)

# zlib 1.3.2 reads zconf.h during configuration, but its upstream ignore
# rules exclude that generated file from a clean checkout. Our checked-in
# template is identical to the header used by the validated build.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/zlib/zconf.h")
    configure_file(zlib/zconf.h.in "${CMAKE_CURRENT_SOURCE_DIR}/zlib/zconf.h" COPYONLY)
    unset(ZLIB_CONF_WRITTEN CACHE)
endif()

add_subdirectory(zlib)
if(NOT TARGET zlibstatic)
    message(FATAL_ERROR "Expected static zlib target was not created")
endif()
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Minizip-ng is intentionally restricted to ordinary Deflate ZIP creation.
set(MZ_COMPAT OFF CACHE BOOL "" FORCE)
set(MZ_ZLIB ON CACHE BOOL "" FORCE)
set(MZ_ZLIB_FLAVOR "zlib" CACHE STRING "" FORCE)
set(MZ_BZIP2 OFF CACHE BOOL "" FORCE)
set(MZ_LZMA OFF CACHE BOOL "" FORCE)
set(MZ_PPMD OFF CACHE BOOL "" FORCE)
set(MZ_ZSTD OFF CACHE BOOL "" FORCE)
set(MZ_LIBCOMP OFF CACHE BOOL "" FORCE)
set(MZ_FETCH_LIBS OFF CACHE BOOL "" FORCE)
set(MZ_FORCE_FETCH_LIBS OFF CACHE BOOL "" FORCE)
set(MZ_PKCRYPT OFF CACHE BOOL "" FORCE)
set(MZ_WZAES OFF CACHE BOOL "" FORCE)
set(MZ_OPENSSL OFF CACHE BOOL "" FORCE)
set(MZ_ICONV OFF CACHE BOOL "" FORCE)
set(MZ_COMPRESS_ONLY OFF CACHE BOOL "" FORCE)
set(MZ_DECOMPRESS_ONLY OFF CACHE BOOL "" FORCE)
set(MZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(MZ_BUILD_FUZZ_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(minizip-ng)
