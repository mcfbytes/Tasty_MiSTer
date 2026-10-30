add_library(mister_hal STATIC
  src/axi.cpp
  src/board_profile.cpp
  src/board_windows.cpp
  src/compatible_blob.cpp
  src/irq_pin.cpp
  src/link_timing.cpp
  src/scaler_buffers.cpp
  src/scaler_probe.cpp
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
  hal/reg.h
  hal/reg_block.h
  hal/reg_field.h
  hal/region_id.h
  hal/register_window.h
  hal/scaler_buffers.h
  hal/scaler_header.h
  hal/scaler_probe.h
  hal/selected.h
  hal/spi_bus.h
  hal/spi_output_observer.h
  hal/spi_sample.h
  hal/spi_sample_source.h
  hal/spi_transport.h
  hal/sector_block.h
  hal/osd_block.h
  hal/sys_block.h
  hal/sys_directory.h
  hal/thread_map.h
  hal/types.h
  hal/unmeasured.h
  hal/uio_doorbell_source.h
  hal/uio_frame_lane.h
  hal/uio_reg_file.h
  hal/video_out_decl.h
  hal/window_decl.h
)

