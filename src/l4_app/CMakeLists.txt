add_library(mister_app STATIC
  src/addon_send.cpp
  src/avi_encoder.cpp
  src/avi_format.cpp
  src/avi_writer.cpp
  src/backdrop_sampler.cpp
  src/backdrop_scaler.cpp
  src/bitstream_programmer.cpp
  src/bt_pump.cpp
  src/capture_main.cpp
  src/cheat_link.cpp
  src/cheat_store.cpp
  src/cmd_fifo.cpp
  src/companion_host.cpp
  src/companion_walk.cpp
  src/config_apply.cpp
  src/core_frame_counter.cpp
  src/core_init.cpp
  src/core_window.cpp
  src/cscd_codec.cpp
  src/durable_write.cpp
  src/durable_write_service.cpp
  src/encode_main.cpp
  src/event.cpp
  src/fabric_state.cpp
  src/file_bytes.cpp
  src/file_stream_service.cpp
  src/file_tx_pieces.cpp
  src/frame_arena.cpp
  src/frame_copier.cpp
  src/frame_hasher.cpp
  src/hd_dirty_ledger.cpp
  src/hd_osd_main.cpp
  src/hd_osd_screen.cpp
  src/hd_renderer.cpp
  src/hd_theme.cpp
  src/hps_framebuffer.cpp
  src/identity_latch.cpp
  src/idle_blank.cpp
  src/ini_parse.cpp
  src/input_decode.cpp
  src/input_main.cpp
  src/input_pipeline.cpp
  src/input_wire.cpp
  src/launcher_host.cpp
  src/launcher_key_bridge.cpp
  src/launcher_main.cpp
  src/launcher_screen.cpp
  src/link_binder.cpp
  src/link_router.cpp
  src/link_session.cpp
  src/load_key.cpp
  src/load_ladder.cpp
  src/load_walk.cpp
  src/load_window.cpp
  src/mailbox_relay.cpp
  src/mgl.cpp
  src/mgl_pump.cpp
  src/motion_badge.cpp
  src/name_config.cpp
  src/osd_wire.cpp
  src/pcm_main.cpp
  src/pcm_ring_feeder.cpp
  src/pending_load.cpp
  src/process_integration.cpp
  src/quiesce_ack.cpp
  src/rbf_resolve.cpp
  src/rec_options.cpp
  src/rec_write_main.cpp
  src/recents_store.cpp
  src/recorder_control.cpp
  src/recorder_status.cpp
  src/remembered_path.cpp
  src/replay_feeder.cpp
  src/replay_gate.cpp
  src/replay_status.cpp
  src/romset_alias_reader.cpp
  src/rt_main.cpp
  src/rt_park.cpp
  src/save_image_source.cpp
  src/scaler_frame_source.cpp
  src/scanout_packet.cpp
  src/scanout_relay.cpp
  src/screenshot_pump.cpp
  src/session_bindings.cpp
  src/session_machine.cpp
  src/session_owner.cpp
  src/sidecar_writer.cpp
  src/spi_encoder.cpp
  src/stream_io.cpp
  src/uart_mode.cpp
  src/video_pump.cpp
  src/video_wire.cpp
  src/window_job_service.cpp
  src/zmbv_codec.cpp
)
target_include_directories(mister_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(ZLIB REQUIRED)
target_link_libraries(mister_app
  PUBLIC mister_cores
  PRIVATE tasty_options ZLIB::ZLIB)

target_include_directories(mister_app SYSTEM PRIVATE ${TASTY_LZO_INCLUDE_DIR})
target_link_libraries(mister_app PRIVATE ${TASTY_LZO_LIBRARY})

if(TASTY_WITH_HDOSD)
  set(_hdosd "${CMAKE_CURRENT_BINARY_DIR}/hdosd")
  file(MAKE_DIRECTORY "${_hdosd}")
  configure_file("${TASTY_HDOSD_FONT_DIR}/NotoSans-Regular.ttf"
                 "${_hdosd}/NotoSans-Regular.ttf" COPYONLY)
  configure_file("${TASTY_HDOSD_FONT_DIR}/NotoSans-Bold.ttf"
                 "${_hdosd}/NotoSans-Bold.ttf" COPYONLY)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/slint/hd_menu.slint"
                 "${_hdosd}/hd_menu.slint" COPYONLY)
  target_sources(mister_app PRIVATE src/slint_raster.cpp)
  set_property(TARGET mister_app PROPERTY SLINT_EMBED_RESOURCES embed-files)
  slint_target_sources(mister_app "${_hdosd}/hd_menu.slint")
  find_package(Threads REQUIRED)
  target_link_libraries(mister_app PRIVATE Slint::Slint Threads::Threads ${CMAKE_DL_LIBS})
  target_compile_definitions(mister_app PUBLIC TASTY_HAVE_HDOSD=1)
  get_target_property(_slint_inc Slint::Slint INTERFACE_INCLUDE_DIRECTORIES)
  target_include_directories(mister_app SYSTEM BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}" ${_slint_inc})
  set_source_files_properties(
    "${CMAKE_CURRENT_BINARY_DIR}/slint_generated_hd_menu_1.cpp"
    PROPERTIES COMPILE_OPTIONS "-w")
endif()

tasty_add_header_checks(tasty_app_header_checks LIB mister_app HEADERS
  app/activity_source.h
  app/avi_encoder.h
  app/avi_format.h
  app/avi_write_status.h
  app/avi_writer.h
  app/bitstream_programmer.h
  app/capture_main.h
  app/chunk_slot.h
  app/core_frame_counter.h
  app/cscd_codec.h
  app/frame_codec.h
  app/rec_options.h
  app/zmbv_codec.h
  app/core_frame_record.h
  app/encode_main.h
  app/encode_status.h
  app/frame_arena.h
  app/frame_copier.h
  app/frame_demand.h
  app/frame_hasher.h
  app/frame_stamp.h
  app/raw_frame_slot.h
  app/rec_control.h
  app/rec_write_main.h
  app/backdrop_sampler.h
  app/backdrop_scaler.h
  app/fb_view.h
  app/hd_dirty_ledger.h
  app/hd_flip_slot.h
  app/hd_layout.h
  app/hd_osd_ask.h
  app/hd_osd_main.h
  app/hd_osd_status.h
  app/hd_osd_screen.h
  app/hd_raster.h
  app/slint_raster.h
  app/hd_rect.h
  app/hd_renderer.h
  app/hd_style.h
  app/hd_surface.h
  app/hd_theme.h
  app/hd_tick_pacer.h
  app/motion_badge.h
  app/hps_framebuffer.h
  app/output_geometry.h
  app/page_description.h
  app/launcher_demand.h
  app/launcher_fb_ack.h
  app/launcher_host.h
  app/launcher_key_bridge.h
  app/launcher_main.h
  app/launcher_profile.h
  app/launcher_screen.h
  app/launcher_state.h
  app/scanout_channel.h
  app/scanout_packet.h
  app/scanout_relay.h
  app/uio_burst.h
  app/rec_write_status.h
  app/recorder_control.h
  app/recorder_status.h
  app/sidecar_msg.h
  app/sidecar_writer.h
  app/board_ops.h
  app/bt_pump.h
  app/capture_arm.h
  app/capture_hit.h
  app/cheat_record.h
  app/cheat_row.h
  app/cheat_store.h
  app/cmd_fifo.h
  app/cmd_verb.h
  app/cmd_verb_sink.h
  app/xml_kind.h
  app/config_slot_cell.h
  app/ladder_cell.h
  app/mount_status_cell.h
  app/save_extent_cell.h
  app/core_init.h
  app/core_window.h
  app/durable_write.h
  app/durable_write_service.h
  app/window_job_service.h
  app/event.h
  app/fabric_state.h
  app/park_receipt.h
  app/quiesce.h
  app/quiesce_ack.h
  app/file_stream_service.h
  app/event_sink.h
  app/owner_tick.h
  app/ui_round_port.h
  app/frame_source.h
  app/game_id.h
  app/info_sink.h
  app/input_build.h
  app/input_decode.h
  app/input_main.h
  app/input_park.h
  app/input_emit.h
  app/input_pipeline.h
  app/input_sample.h
  app/input_wire.h
  app/load_request.h
  app/mgl.h
  app/mgl_pump.h
  app/mra_facts.h
  app/name_config.h
  app/remembered_path.h
  app/osd_wire.h
  app/path_text.h
  app/pcm_command.h
  app/pcm_ring_feeder.h
  app/mailbox_slot.h
  app/mailbox_relay.h
  app/companion_bind.h
  app/companion_host.h
  app/companion_walk.h
  app/addon_send.h
  app/recents_store.h
  app/romset_alias_reader.h
  app/romset_aliases.h
  app/process_integration.h
  app/raw_key_edge.h
  app/rbf_resolve.h
  app/replay_control.h
  app/replay_feeder.h
  app/replay_gate.h
  app/replay_msg.h
  app/replay_ring.h
  app/replay_status.h
  app/rt_main.h
  app/rt_park.h
  app/session_bindings.h
  app/save_image_source.h
  app/scaler_frame_source.h
  app/screenshot_pump.h
  app/screenshot_queue.h
  app/cmd_router.h
  app/identity_latch.h
  app/ini_parse.h
  app/content_request.h
  app/file_bytes.h
  app/file_tx_pieces.h
  app/dev_mem_window_map.h
  app/file_tx_level.h
  app/load_ladder.h
  app/load_window.h
  app/load_window_counts.h
  app/load_window_map.h
  app/link_bytes.h
  app/link_tx_channel.h
  app/link_router.h
  app/link_rx_channel.h
  app/link_event_facts.h
  app/link_event_dispatch.h
  app/link_encoder.h
  app/link_op_facts.h
  app/link_op_ctx.h
  app/encoder_roles.h
  app/cheat_call.h
  app/cheat_link.h
  app/link_op_dispatch.h
  app/link_producers.h
  app/pending_load.h
  app/link_binder.h
  app/link_rows.h
  app/session_identity.h
  app/session_machine.h
  app/session_seats.h
  app/stream_io.h
  app/session_owner.h
  app/link_session.h
  app/load_key.h
  app/spi_encoder.h
  app/stdout_router.h
  app/types.h
  app/uart_mode.h
  app/ui_request_ring.h
  app/video_pump.h
  app/video_wire.h
)

if(TASTY_BUILD_TESTS)

  tasty_unit_test(tasty_app_cheat_store_unit
    NAME tasty.app_cheat_store
    SOURCES tests/cheat_store_unit.cpp
    LIBS mister_app tasty_test_options
    TIMEOUT 60)
  target_include_directories(tasty_app_cheat_store_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})

  add_executable(tasty_launcher_fake_child tests/launcher_fake_child.cpp)
  set(TASTY_FAKE_LAUNCHER_PATH "$<TARGET_FILE:tasty_launcher_fake_child>")

  if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
    set(_tasty_emu "${CMAKE_CROSSCOMPILING_EMULATOR}")
    list(TRANSFORM _tasty_emu PREPEND "'")
    list(TRANSFORM _tasty_emu APPEND "'")
    list(JOIN _tasty_emu " " _tasty_emu)
    set(TASTY_FAKE_LAUNCHER_PATH "${CMAKE_CURRENT_BINARY_DIR}/launcher_fake_child.sh")
    file(GENERATE OUTPUT "${TASTY_FAKE_LAUNCHER_PATH}"
      CONTENT "#!/bin/sh\nexec ${_tasty_emu} '$<TARGET_FILE:tasty_launcher_fake_child>' \"$@\"\n"
      FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                       WORLD_READ WORLD_EXECUTE)
  endif()
  tasty_unit_test(tasty_launcher_host_unit
    NAME tasty.launcher_host
    SOURCES tests/launcher_host_unit.cpp
    DEFINES TASTY_FAKE_LAUNCHER="${TASTY_FAKE_LAUNCHER_PATH}"
    LIBS mister_app tasty_test_options
    TIMEOUT 120)
  add_dependencies(tasty_launcher_host_unit tasty_launcher_fake_child)

  tasty_unit_test(tasty_scanout_relay_unit
    NAME tasty.scanout_relay
    SOURCES tests/scanout_relay_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_launcher_ui_unit
    NAME tasty.launcher_ui
    SOURCES tests/launcher_ui_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_app_hd_theme_unit
    NAME tasty.app_hd_theme
    SOURCES tests/hd_theme_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_hd_tick_pacer_unit
    NAME tasty.hd_tick_pacer
    SOURCES tests/hd_tick_pacer_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_hd_renderer_unit
    NAME tasty.hd_renderer
    SOURCES tests/hd_renderer_unit.cpp
    LIBS mister_app tasty_test_options)
  if(TASTY_WITH_HDOSD)
    tasty_unit_test(hd_slint_smoke
      NAME hd_slint_smoke
      SOURCES tests/hd_slint_smoke.cpp
      LIBS mister_app tasty_test_options)
  endif()
  tasty_unit_test(tasty_backdrop_unit
    NAME tasty.backdrop_unit
    SOURCES tests/backdrop_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_app_unit
    NAME tasty.app_unit
    SOURCES tests/app_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_link_channel_unit
    NAME tasty.link_channel
    SOURCES tests/link_channel_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_session_bindings_unit
    NAME tasty.session_bindings
    SOURCES tests/session_bindings_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_link_router_unit
    NAME tasty.link_router
    SOURCES tests/link_router_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_link_op_facts_unit
    NAME tasty.link_op_facts
    SOURCES tests/link_op_facts_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_link_op_unrouted_on_probe
    NAME tasty.link_op_unrouted_on_probe
    SOURCES tests/link_op_unrouted_on_probe.cpp
    EXCLUDE_FROM_ALL
    LIBS mister_app tasty_test_options
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target tasty_link_op_unrouted_on_probe
    LABELS compile_probe
    TIMEOUT 120
    PROPERTIES PASS_REGULAR_EXPRESSION
      "an on\\(\\) overload for an alternative this sink is not routed"
      RUN_SERIAL
      TRUE)

  tasty_unit_test(tasty_spi_encoder_unit
    NAME tasty.spi_encoder
    SOURCES tests/spi_encoder_unit.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/tests
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_core_init_unit
    NAME tasty.core_init
    SOURCES tests/core_init_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_app_fabric_crossing
    NAME tasty.app_fabric_crossing
    SOURCES tests/fabric_crossing_unit.cpp
    LIBS mister_app tasty_test_options
    TIMEOUT 120)

  tasty_unit_test(tasty_bitstream_programmer_unit
    NAME tasty.bitstream_programmer
    SOURCES tests/bitstream_programmer_unit.cpp
    LIBS mister_app tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_app_idle_blank
    NAME tasty.app_idle_blank
    SOURCES tests/idle_blank_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_app_binder_unit
    NAME tasty.app_binder
    SOURCES tests/binder_unit.cpp
    LIBS mister_app tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_app_mailbox_relay_unit
    NAME tasty.app_mailbox_relay
    SOURCES tests/mailbox_relay_unit.cpp
    LIBS mister_app tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_app_pcm_feeder_unit
    NAME tasty.app_pcm_feeder
    SOURCES tests/pcm_feeder_unit.cpp
    LIBS mister_app tasty_test_options Threads::Threads
    TIMEOUT 60)

  tasty_unit_test(tasty_app_durable_write_service_unit
    NAME tasty.app_durable_write_service
    SOURCES tests/durable_write_service_unit.cpp
    LIBS mister_app tasty_test_options Threads::Threads
    TIMEOUT 60)

  tasty_unit_test(tasty_app_window_job_service_unit
    NAME tasty.app_window_job_service
    SOURCES tests/window_job_service_unit.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/tests
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_app_file_stream_service_unit
    NAME tasty.app_file_stream_service
    SOURCES tests/file_stream_service_unit.cpp
    LIBS mister_app tasty_test_options Threads::Threads
    TIMEOUT 60)

  tasty_unit_test(tasty_app_cmd_unit
    NAME tasty.app_cmd
    SOURCES tests/cmd_unit.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/tests
    LIBS mister_app tasty_test_options
    TIMEOUT 60)

  find_package(Threads REQUIRED)

  tasty_unit_test(tasty_input_wire_unit
    NAME tasty.input_wire_unit
    SOURCES tests/input_wire_unit.cpp
    LIBS mister_app tasty_test_options Threads::Threads
    TIMEOUT 120)

  tasty_unit_test(tasty_osd_wire_unit
    NAME tasty.osd_wire_unit
    SOURCES tests/osd_wire_unit.cpp
    LIBS mister_app tasty_test_options Threads::Threads
    TIMEOUT 120)

  tasty_unit_test(tasty_scaler_frame_source_unit
    NAME tasty.scaler_frame_source
    SOURCES tests/scaler_frame_source_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_session_owner_unit
    NAME tasty.session_owner
    SOURCES tests/session_owner_unit.cpp
    LIBS mister_app tasty_test_options
    PROPERTIES RESOURCE_LOCK tmp-GAMEID)
  target_include_directories(tasty_session_owner_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})

  tasty_unit_test(tasty_app_load_walk_unit
    NAME tasty.app_load_walk
    SOURCES tests/load_walk_unit.cpp
    LIBS mister_app tasty_test_options
    TIMEOUT 120)
  target_include_directories(tasty_app_load_walk_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})

  tasty_unit_test(tasty_app_neogeo_rows_unit
    NAME tasty.app_neogeo_rows
    SOURCES tests/neogeo_rows_unit.cpp
    LIBS mister_app tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_pending_load_unit
    NAME tasty.pending_load
    SOURCES tests/pending_load_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_romset_alias_reader_unit
    NAME tasty.romset_alias_reader
    SOURCES tests/romset_alias_reader_unit.cpp
    DEFINES TASTY_NEOGEO_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/neogeo"
    LIBS mister_app tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_store_unit
    NAME tasty.store_unit
    SOURCES tests/store_unit.cpp
    LIBS mister_app tasty_test_options)

  tasty_unit_test(tasty_uart_proc_unit
    NAME tasty.uart_proc
    SOURCES tests/uart_proc_unit.cpp
    LIBS mister_app tasty_test_options
    TIMEOUT 60
    PROPERTIES RESOURCE_LOCK tmp-uart-handoffs)

  tasty_unit_test(tasty_mgl_pump_unit
    NAME tasty.mgl_pump
    SOURCES tests/mgl_pump_unit.cpp
    LIBS mister_app tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_app_session
    NAME tasty.app_session
    SOURCES tests/app_session.cpp
      tests/session_cd_ladders.cpp
      tests/session_goldens.cpp
      tests/session_lifecycle.cpp
      tests/session_manifest.cpp
      tests/session_mount_save.cpp
      tests/session_nvram_blank.cpp
      tests/session_pcm_msu.cpp
      tests/session_psx_menu.cpp
      tests/session_recovery.cpp
      tests/session_rt_drain.cpp
      tests/session_switch_main.cpp
      tests/session_window_n64.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/tests
    DEFINES TASTY_L4_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
      TASTY_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/goldens"
    LIBS mister_app mister_boards tasty_test_options
    TIMEOUT 60
    PROPERTIES RESOURCE_LOCK tmp-GAMEID)
  set_source_files_properties(tests/session_switch_main.cpp PROPERTIES COMPILE_OPTIONS -Wno-array-bounds)

  tasty_unit_test(tasty_corestate_via_supervisor_probe
    NAME tasty.app_corestate_redef
    SOURCES tests/corestate_via_supervisor_probe.cpp
    EXCLUDE_FROM_ALL
    LIBS mister_app tasty_test_options
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target tasty_corestate_via_supervisor_probe
    LABELS compile_probe
    TIMEOUT 120
    PROPERTIES WILL_FAIL
      TRUE

      RUN_SERIAL
      TRUE)
endif()
