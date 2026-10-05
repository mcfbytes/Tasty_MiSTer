find_package(Threads REQUIRED)

add_library(mister_fw_assembly STATIC
  assembly.cpp
  board_parts.cpp
  diag_main.cpp
  diag_sampler.cpp
  ui_main.cpp
  census.cpp
)
target_include_directories(mister_fw_assembly PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mister_fw_assembly
  PUBLIC mister_app mister_cores mister_boards Threads::Threads
  PRIVATE tasty_options)

add_library(mister_fw_tasty STATIC
  tasty_cli.cpp
  tasty_ctl.cpp
  tasty_hub.cpp
  tasty_registry.cpp
  tasty_session.cpp
  tasty_sink.cpp
  tasty_splash.cpp
  hub.cpp
  process_main.cpp
  ladder_scope.cpp
)
target_include_directories(mister_fw_tasty PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mister_fw_tasty
  PUBLIC mister_fw_assembly
  PRIVATE tasty_options)

add_executable(tasty_exe tasty_main.cpp)
set_target_properties(tasty_exe PROPERTIES OUTPUT_NAME tasty)
target_link_libraries(tasty_exe PRIVATE mister_fw_tasty mister_fw_assembly tasty_options)
target_compile_definitions(mister_fw_tasty PRIVATE TASTY_VDATE="${TASTY_VDATE}")

if(NOT TASTY_SPLASH_PNG)
  set(TASTY_SPLASH_PNG "${CMAKE_SOURCE_DIR}/art/banner.png")
endif()
if(NOT EXISTS "${TASTY_SPLASH_PNG}")
  message(FATAL_ERROR "splash PNG not found: ${TASTY_SPLASH_PNG}")
endif()
set(_splash_h "${CMAKE_CURRENT_BINARY_DIR}/splash_embed.h")
add_custom_command(
  OUTPUT "${_splash_h}"
  COMMAND python3 "${CMAKE_SOURCE_DIR}/src/tools/embed_splash.py"
          "${TASTY_SPLASH_PNG}" "${_splash_h}"
  DEPENDS "${TASTY_SPLASH_PNG}" "${CMAKE_SOURCE_DIR}/src/tools/embed_splash.py"
  VERBATIM)
add_custom_target(tasty_splash_embed DEPENDS "${_splash_h}")
add_dependencies(mister_fw_tasty tasty_splash_embed)
set_source_files_properties(tasty_splash.cpp PROPERTIES OBJECT_DEPENDS "${_splash_h}")
target_include_directories(mister_fw_tasty PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
find_package(ZLIB REQUIRED)
target_link_libraries(mister_fw_tasty PRIVATE ZLIB::ZLIB)
target_link_libraries(tasty_exe PRIVATE ZLIB::ZLIB)
