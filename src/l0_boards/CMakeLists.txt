add_library(mister_boards STATIC
  src/cyclone_v/cyclone_v_bridges.cpp
  src/cyclone_v/cyclone_v_ocram_platform.cpp
  src/cyclone_v/cyclone_v_uboot_handoff_page.cpp
  src/cyclone_v/cyclone_v_programmer.cpp
  src/refusing_bridges.cpp
  src/refusing_board_link.cpp
  src/refusing_programmer.cpp
)
target_include_directories(mister_boards PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_boards
  PUBLIC mister_hal
  PRIVATE tasty_options)

tasty_add_header_checks(tasty_boards_header_checks LIB mister_boards HEADERS
  boards/cyclone_v/cyclone_v_bridges.h
  boards/cyclone_v/cyclone_v_ocram_platform.h
  boards/cyclone_v/cyclone_v_rstmgr.h
  boards/cyclone_v/cyclone_v_sysmgr.h
  boards/cyclone_v/cyclone_v_uboot_handoff_page.h
  boards/cyclone_v/cyclone_v_programmer.h
  boards/cyclone_v/cyclone_v_windows.h
  boards/refusing_bridges.h
  boards/refusing_board_link.h
  boards/refusing_programmer.h
)

