add_library(mister_boards STATIC
  src/agilex_5/agilex5_bridges.cpp
  src/agilex_5/agilex5_programmer.cpp
  src/cyclone_v/cyclone_v_bridges.cpp
  src/cyclone_v/cyclone_v_programmer.cpp
  src/refusing_link_port.cpp
  src/spi_sbc/spi_sbc_bridges.cpp
  src/spi_sbc/spi_sbc_programmer.cpp
  src/zynq_7000/zynq7000_bridges.cpp
  src/zynq_7000/zynq7000_programmer.cpp
)
target_include_directories(mister_boards PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_boards
  PUBLIC mister_hal
  PRIVATE tasty_options)

tasty_add_header_checks(tasty_boards_header_checks LIB mister_boards HEADERS
  boards/agilex_5/agilex5_bridges.h
  boards/agilex_5/agilex5_programmer.h
  boards/cyclone_v/cyclone_v_bridges.h
  boards/cyclone_v/cyclone_v_programmer.h
  boards/cyclone_v/cyclone_v_windows.h
  boards/refusing_link_port.h
  boards/spi_sbc/spi_sbc_bridges.h
  boards/spi_sbc/spi_sbc_programmer.h
  boards/zynq_7000/zynq7000_bridges.h
  boards/zynq_7000/zynq7000_programmer.h
)

if(TASTY_BUILD_TESTS)

  foreach(mod IN ITEMS link_port loader)
    tasty_unit_test(tasty_boards_${mod}_unit
      NAME tasty.boards_${mod}
      SOURCES tests/${mod}_unit.cpp
      LIBS mister_boards tasty_test_options
      TIMEOUT 60)
  endforeach()
endif()
