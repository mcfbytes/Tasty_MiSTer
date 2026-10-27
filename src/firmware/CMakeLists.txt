find_package(Threads REQUIRED)

add_library(mister_fw_assembly STATIC
  assembly.cpp
  board_parts.cpp
  board_reset.cpp
  census.cpp
  device_block.cpp
  diag_main.cpp
  diag_sampler.cpp
  hub.cpp
  ladder_scope.cpp
  open_verdict.cpp
  process_main.cpp
  process_standup.cpp
  recorder_block.cpp
  replay_block.cpp
  report.cpp
  ui_main.cpp
)
target_include_directories(mister_fw_assembly PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mister_fw_assembly
  PUBLIC mister_app mister_cores mister_boards Threads::Threads
  PRIVATE tasty_options)

add_library(mister_fw_menu STATIC
  hd_osd_block.cpp
  launcher_block.cpp
  menu_block.cpp
  menu_mode.cpp
  ui_sink.cpp
)
target_include_directories(mister_fw_menu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mister_fw_menu
  PUBLIC mister_fw_assembly mister_ui
  PRIVATE tasty_options)
target_compile_definitions(mister_fw_menu PRIVATE TASTY_VDATE="${TASTY_VDATE}")

add_library(mister_fw_tasty STATIC
  tasty_cli.cpp
  tasty_ctl.cpp
  tasty_entry.cpp
  replay_mode.cpp
  tasty_registry.cpp
  tasty_session.cpp
  tasty_sink.cpp
  tasty_splash.cpp
)
target_include_directories(mister_fw_tasty PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mister_fw_tasty
  PUBLIC mister_fw_assembly
  PRIVATE tasty_options)
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

add_executable(mister_exe main.cpp)
set_target_properties(mister_exe PROPERTIES OUTPUT_NAME MiSTer)
target_link_libraries(mister_exe
  PRIVATE mister_fw_menu mister_fw_tasty mister_fw_assembly mister_ui tasty_options ZLIB::ZLIB)
