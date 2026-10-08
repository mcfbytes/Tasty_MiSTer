cmake_minimum_required(VERSION 3.21)
project(tasty VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/ccache.cmake")
  include(cmake/ccache.cmake)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(TASTY_BUILD_TESTS "Build layer unit tests" OFF)
option(TASTY_TESTS "Build tasty host tests" ON)

include(cmake/decompressors.cmake)
find_path(TASTY_LZO_INCLUDE_DIR lzo/lzo1x.h)
find_library(TASTY_LZO_LIBRARY NAMES lzo2)
foreach(_dep IN ITEMS LIBCHDR MINIZIP LZO)
  if(NOT TASTY_${_dep}_INCLUDE_DIR OR NOT TASTY_${_dep}_LIBRARY)
    message(FATAL_ERROR "tasty needs ${_dep}: headers and library not both found")
  endif()
endforeach()

include(cmake/warnings.cmake)
include(cmake/header_checks.cmake)
include(cmake/unit_test.cmake)
enable_testing()

if(NOT TASTY_VDATE)
  string(TIMESTAMP TASTY_VDATE_DEFAULT "%y%m%d" UTC)
  set(TASTY_VDATE "${TASTY_VDATE_DEFAULT}" CACHE STRING "YYMMDD version stamp")
endif()

set(TASTY_SPLASH_PNG "${CMAKE_SOURCE_DIR}/art/banner.png" CACHE FILEPATH "splash PNG")

add_subdirectory(src/infra)
add_subdirectory(src/l0_os)
add_subdirectory(src/l0_hal)
add_subdirectory(src/l0_reactor)
add_subdirectory(src/l0_boards)
add_subdirectory(src/l1_proto)
add_subdirectory(src/l2_services)
add_subdirectory(src/l3_cores)
add_subdirectory(src/l4_app)
add_subdirectory(src/firmware)

if(TASTY_TESTS)
  add_subdirectory(tests)
endif()
