add_library(mister_hal STATIC
  src/axi.cpp
  src/board_profile.cpp
  src/board_windows.cpp
  src/compatible_blob.cpp
  src/irq_pin.cpp
  src/link_timing.cpp
  src/scaler_buffers.cpp
  src/spi_bus.cpp
  src/thread_map.cpp
)
target_include_directories(mister_hal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_hal
  PUBLIC mister_os
  PRIVATE tasty_options)

tasty_add_header_checks(tasty_hal_header_checks LIB mister_hal HEADERS
  hal/axi.h
  hal/board_profile.h
  hal/board_windows.h
  hal/boards_table.h
  hal/boot_handoff.h
  hal/bridge_sequencer.h
  hal/cause_reg.h
  hal/compatible_blob.h
  hal/core_capabilities.h
  hal/core_identity.h
  hal/core_signals.h
  hal/dma_ring.h
  hal/doorbell_policy.h
  hal/doorbell_source.h
  hal/fact.h
  hal/fact_field.h
  hal/fabric_region.h
  hal/fpga_aperture.h
  hal/fpga_memory.h
  hal/fpga_programmer.h
  hal/handoff_page.h
  hal/hdmi_int.h
  hal/hdmi_int_level.h
  hal/irq_pin.h
  hal/kernel_contract.h
  hal/link_port.h
  hal/link_timing.h
  hal/mailbox.h
  hal/memory_model.h
  hal/phys_region.h
  hal/pin_levels.h
  hal/program_geometry.h
  hal/region_id.h
  hal/register_window.h
  hal/scaler_buffers.h
  hal/scaler_header.h
  hal/selected.h
  hal/spi_bus.h
  hal/spi_output_observer.h
  hal/spi_sample.h
  hal/spi_sample_source.h
  hal/spi_transport.h
  hal/thread_map.h
  hal/types.h
  hal/unmeasured.h
  hal/uio_doorbell_source.h
  hal/video_out_decl.h
  hal/window_decl.h
)

if(TASTY_BUILD_TESTS)

  foreach(mod IN ITEMS axi board_profile board_windows fabric_region irq_pin spi)
    add_executable(tasty_hal_${mod}_unit tests/${mod}_unit.cpp)
    target_link_libraries(tasty_hal_${mod}_unit PRIVATE mister_hal tasty_test_options)
    add_test(NAME tasty.hal_${mod} COMMAND tasty_hal_${mod}_unit)
    set_tests_properties(tasty.hal_${mod} PROPERTIES TIMEOUT 60)
  endforeach()

  target_compile_definitions(tasty_hal_board_profile_unit PRIVATE
    TASTY_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")

  add_executable(tasty_hal_fact_probe tests/fact_probe.cpp)
  target_link_libraries(tasty_hal_fact_probe PRIVATE mister_hal tasty_test_options)
  add_test(NAME tasty.hal_fact_probe COMMAND tasty_hal_fact_probe)
  foreach(arm IN ITEMS 1 2 3)
    add_executable(tasty_hal_fact_probe_arm${arm} EXCLUDE_FROM_ALL tests/fact_probe.cpp)
    target_compile_definitions(tasty_hal_fact_probe_arm${arm} PRIVATE TASTY_FACT_PROBE=${arm})
    target_link_libraries(tasty_hal_fact_probe_arm${arm} PRIVATE mister_hal tasty_test_options)
    add_test(NAME tasty.hal_fact_probe_arm${arm}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                     --target tasty_hal_fact_probe_arm${arm})
    set_tests_properties(tasty.hal_fact_probe_arm${arm} PROPERTIES
      TIMEOUT 120 RUN_SERIAL TRUE LABELS compile_probe)
  endforeach()
  set_tests_properties(tasty.hal_fact_probe_arm1 PROPERTIES
    PASS_REGULAR_EXPRESSION "unmeasured_fact_read_in_a_constant_expression")
  set_tests_properties(tasty.hal_fact_probe_arm2 PROPERTIES
    PASS_REGULAR_EXPRESSION "use of deleted function|call to deleted constructor")
  set_tests_properties(tasty.hal_fact_probe_arm3 PROPERTIES
    PASS_REGULAR_EXPRESSION "no matching function|no matching constructor")

  add_executable(tasty_hal_window_probe tests/window_probe.cpp)
  target_link_libraries(tasty_hal_window_probe PRIVATE mister_hal tasty_test_options)
  add_test(NAME tasty.hal_window_probe COMMAND tasty_hal_window_probe)
  foreach(arm IN ITEMS 1 2)
    add_executable(tasty_hal_window_probe_arm${arm} EXCLUDE_FROM_ALL tests/window_probe.cpp)
    target_compile_definitions(tasty_hal_window_probe_arm${arm} PRIVATE TASTY_WINDOW_PROBE=${arm})
    target_link_libraries(tasty_hal_window_probe_arm${arm} PRIVATE mister_hal tasty_test_options)
    add_test(NAME tasty.hal_window_probe_arm${arm}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                     --target tasty_hal_window_probe_arm${arm})
    set_tests_properties(tasty.hal_window_probe_arm${arm} PROPERTIES
      TIMEOUT 120 RUN_SERIAL TRUE LABELS compile_probe
      PASS_REGULAR_EXPRESSION "no_window_of_that_name")
  endforeach()

  target_link_libraries(tasty_hal_axi_unit PRIVATE mister_boards)
  target_link_libraries(tasty_hal_board_windows_unit PRIVATE mister_boards)
endif()
