# Resolve minizip-ng's zlib dependency against the source-vendored static
# target.  This prevents CMake from locating an unrelated machine-wide DLL.
if(NOT TARGET zlibstatic)
    set(ZLIB_FOUND FALSE)
    return()
endif()

if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

set(ZLIB_FOUND TRUE)
set(ZLIB_VERSION_STRING "1.3.2")
set(ZLIB_LIBRARIES ZLIB::ZLIB)
set(ZLIB_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../zlib;${CMAKE_BINARY_DIR}/zlib")
