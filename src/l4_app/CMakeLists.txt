add_library(mister_app STATIC
  src/avi_encoder.cpp
  src/avi_format.cpp
  src/avi_writer.cpp
  src/bitstream_programmer.cpp
  src/capture_main.cpp
  src/cscd_codec.cpp
  src/core_frame_counter.cpp
  src/encode_main.cpp
  src/frame_arena.cpp
  src/frame_copier.cpp
  src/frame_hasher.cpp
  src/rec_write_main.cpp
  src/recorder_control.cpp
  src/recorder_status.cpp
  src/sidecar_writer.cpp
  src/bt_pump.cpp
  src/cheat_store.cpp
  src/cmd_fifo.cpp
  src/config_apply.cpp
  src/core_init.cpp
  src/core_window.cpp
  src/durable_write.cpp
  src/durable_write_service.cpp
  src/window_job_service.cpp
  src/event.cpp
  src/fabric_state.cpp
  src/file_stream_service.cpp
  src/mgl.cpp
  src/input_decode.cpp
  src/input_main.cpp
  src/input_pipeline.cpp
  src/input_wire.cpp
  src/idle_blank.cpp
  src/mgl_pump.cpp
  src/name_config.cpp
  src/osd_wire.cpp
  src/pcm_main.cpp
  src/pcm_ring_feeder.cpp
  src/process_integration.cpp
  src/rbf_resolve.cpp
  src/recents_store.cpp
  src/romset_alias_reader.cpp
  src/replay_feeder.cpp
  src/replay_gate.cpp
  src/replay_status.cpp
  src/rt_main.cpp
  src/rt_park.cpp
  src/link_router.cpp
  src/save_image_source.cpp
  src/scaler_frame_source.cpp
  src/screenshot_pump.cpp
  src/link_binder.cpp
  src/session_bindings.cpp
  src/session_machine.cpp
  src/stream_io.cpp
  src/pending_load.cpp
  src/quiesce_ack.cpp
  src/identity_latch.cpp
  src/ini_parse.cpp
  src/file_bytes.cpp
  src/file_tx_pieces.cpp
  src/load_ladder.cpp
  src/load_walk.cpp
  src/load_window.cpp
  src/session_owner.cpp
  src/link_session.cpp
  src/load_key.cpp
  src/spi_encoder.cpp
  src/uart_mode.cpp
  src/video_pump.cpp
  src/video_wire.cpp
)
target_include_directories(mister_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(ZLIB REQUIRED)
target_link_libraries(mister_app
  PUBLIC mister_cores
  PRIVATE tasty_options ZLIB::ZLIB)

if(TASTY_WITH_LZO)
  target_include_directories(mister_app SYSTEM PRIVATE ${TASTY_LZO_INCLUDE_DIR})
  target_link_libraries(mister_app PRIVATE ${TASTY_LZO_LIBRARY})
  target_compile_definitions(mister_app PUBLIC TASTY_HAVE_LZO=1)
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
  app/osd_wire.h
  app/path_text.h
  app/pcm_command.h
  app/pcm_ring_feeder.h
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

  add_executable(tasty_app_cheat_store_unit tests/cheat_store_unit.cpp)
  target_link_libraries(tasty_app_cheat_store_unit PRIVATE mister_app tasty_test_options)
  if(TASTY_WITH_MINIZIP)
    target_compile_definitions(tasty_app_cheat_store_unit PRIVATE TASTY_HAVE_MINIZIP=1)
    target_include_directories(tasty_app_cheat_store_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})
  endif()
  add_test(NAME tasty.app_cheat_store COMMAND tasty_app_cheat_store_unit)
  set_tests_properties(tasty.app_cheat_store PROPERTIES TIMEOUT 60)

  add_executable(tasty_app_unit tests/app_unit.cpp)
  target_link_libraries(tasty_app_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.app_unit COMMAND tasty_app_unit)

  add_executable(tasty_link_channel_unit tests/link_channel_unit.cpp)
  target_link_libraries(tasty_link_channel_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.link_channel COMMAND tasty_link_channel_unit)

  add_executable(tasty_session_bindings_unit tests/session_bindings_unit.cpp)
  target_link_libraries(tasty_session_bindings_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.session_bindings COMMAND tasty_session_bindings_unit)

  add_executable(tasty_link_router_unit tests/link_router_unit.cpp)
  target_link_libraries(tasty_link_router_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.link_router COMMAND tasty_link_router_unit)

  add_executable(tasty_link_op_facts_unit tests/link_op_facts_unit.cpp)
  target_link_libraries(tasty_link_op_facts_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.link_op_facts COMMAND tasty_link_op_facts_unit)

  add_executable(tasty_link_op_unrouted_on_probe EXCLUDE_FROM_ALL tests/link_op_unrouted_on_probe.cpp)
  target_link_libraries(tasty_link_op_unrouted_on_probe PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.link_op_unrouted_on_probe
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target tasty_link_op_unrouted_on_probe)
  set_tests_properties(tasty.link_op_unrouted_on_probe PROPERTIES
    PASS_REGULAR_EXPRESSION "an on\\(\\) overload for an alternative this sink is not routed"
    TIMEOUT 120 RUN_SERIAL TRUE LABELS compile_probe)

  add_executable(tasty_spi_encoder_unit tests/spi_encoder_unit.cpp)
  target_link_libraries(tasty_spi_encoder_unit PRIVATE mister_app tasty_test_options)
  target_include_directories(tasty_spi_encoder_unit PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  add_test(NAME tasty.spi_encoder COMMAND tasty_spi_encoder_unit)

  add_executable(tasty_core_init_unit tests/core_init_unit.cpp)
  target_link_libraries(tasty_core_init_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.core_init COMMAND tasty_core_init_unit)

  add_executable(tasty_app_fabric_crossing tests/fabric_crossing_unit.cpp)
  target_link_libraries(tasty_app_fabric_crossing PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.app_fabric_crossing COMMAND tasty_app_fabric_crossing)

  set_tests_properties(tasty.app_fabric_crossing PROPERTIES TIMEOUT 120)

  add_executable(tasty_bitstream_programmer_unit tests/bitstream_programmer_unit.cpp)
  target_link_libraries(tasty_bitstream_programmer_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.bitstream_programmer COMMAND tasty_bitstream_programmer_unit)

  set_tests_properties(tasty.bitstream_programmer PROPERTIES TIMEOUT 60)

  add_executable(tasty_app_idle_blank tests/idle_blank_unit.cpp)
  target_link_libraries(tasty_app_idle_blank PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.app_idle_blank COMMAND tasty_app_idle_blank)

  add_executable(tasty_app_binder_unit tests/binder_unit.cpp)
  target_link_libraries(tasty_app_binder_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.app_binder COMMAND tasty_app_binder_unit)
  set_tests_properties(tasty.app_binder PROPERTIES TIMEOUT 60)

  add_executable(tasty_app_pcm_feeder_unit tests/pcm_feeder_unit.cpp)
  target_link_libraries(tasty_app_pcm_feeder_unit PRIVATE mister_app tasty_test_options Threads::Threads)
  add_test(NAME tasty.app_pcm_feeder COMMAND tasty_app_pcm_feeder_unit)

  set_tests_properties(tasty.app_pcm_feeder PROPERTIES TIMEOUT 60)

  add_executable(tasty_app_durable_write_service_unit tests/durable_write_service_unit.cpp)
  target_link_libraries(tasty_app_durable_write_service_unit PRIVATE mister_app tasty_test_options
                        Threads::Threads)
  add_test(NAME tasty.app_durable_write_service
           COMMAND tasty_app_durable_write_service_unit)
  set_tests_properties(tasty.app_durable_write_service PROPERTIES TIMEOUT 60)

  add_executable(tasty_app_window_job_service_unit tests/window_job_service_unit.cpp)
  target_link_libraries(tasty_app_window_job_service_unit PRIVATE mister_app tasty_test_options)
  target_include_directories(tasty_app_window_job_service_unit PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  add_test(NAME tasty.app_window_job_service COMMAND tasty_app_window_job_service_unit)

  add_executable(tasty_app_file_stream_service_unit tests/file_stream_service_unit.cpp)
  target_link_libraries(tasty_app_file_stream_service_unit PRIVATE mister_app tasty_test_options
                        Threads::Threads)
  add_test(NAME tasty.app_file_stream_service
           COMMAND tasty_app_file_stream_service_unit)
  set_tests_properties(tasty.app_file_stream_service PROPERTIES TIMEOUT 60)

  add_executable(tasty_app_cmd_unit tests/cmd_unit.cpp)

  target_include_directories(tasty_app_cmd_unit PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  target_link_libraries(tasty_app_cmd_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.app_cmd COMMAND tasty_app_cmd_unit)

  set_tests_properties(tasty.app_cmd PROPERTIES TIMEOUT 60)

  find_package(Threads REQUIRED)
  add_executable(tasty_input_wire_unit tests/input_wire_unit.cpp)
  target_link_libraries(tasty_input_wire_unit PRIVATE mister_app tasty_test_options
                        Threads::Threads)
  add_test(NAME tasty.input_wire_unit COMMAND tasty_input_wire_unit)

  set_tests_properties(tasty.input_wire_unit PROPERTIES TIMEOUT 120)

  add_executable(tasty_osd_wire_unit tests/osd_wire_unit.cpp)
  target_link_libraries(tasty_osd_wire_unit PRIVATE mister_app tasty_test_options
                        Threads::Threads)
  add_test(NAME tasty.osd_wire_unit COMMAND tasty_osd_wire_unit)
  set_tests_properties(tasty.osd_wire_unit PROPERTIES TIMEOUT 120)

  add_executable(tasty_scaler_frame_source_unit tests/scaler_frame_source_unit.cpp)
  target_link_libraries(tasty_scaler_frame_source_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.scaler_frame_source COMMAND tasty_scaler_frame_source_unit)

  add_executable(tasty_session_owner_unit tests/session_owner_unit.cpp)
  target_link_libraries(tasty_session_owner_unit PRIVATE mister_app tasty_test_options)
  if(TASTY_WITH_MINIZIP)
    target_compile_definitions(tasty_session_owner_unit PRIVATE TASTY_HAVE_MINIZIP=1)
    target_include_directories(tasty_session_owner_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})
  endif()
  add_test(NAME tasty.session_owner COMMAND tasty_session_owner_unit)
  set_tests_properties(tasty.session_owner PROPERTIES RESOURCE_LOCK tmp-GAMEID)

  add_executable(tasty_app_load_walk_unit tests/load_walk_unit.cpp)
  target_link_libraries(tasty_app_load_walk_unit PRIVATE mister_app tasty_test_options)
  if(TASTY_WITH_MINIZIP)
    target_compile_definitions(tasty_app_load_walk_unit PRIVATE TASTY_HAVE_MINIZIP=1)
    target_include_directories(tasty_app_load_walk_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})
  endif()
  add_test(NAME tasty.app_load_walk COMMAND tasty_app_load_walk_unit)
  set_tests_properties(tasty.app_load_walk PROPERTIES TIMEOUT 120)

  add_executable(tasty_app_neogeo_rows_unit tests/neogeo_rows_unit.cpp)
  target_link_libraries(tasty_app_neogeo_rows_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.app_neogeo_rows COMMAND tasty_app_neogeo_rows_unit)
  set_tests_properties(tasty.app_neogeo_rows PROPERTIES TIMEOUT 60)

  add_executable(tasty_pending_load_unit tests/pending_load_unit.cpp)
  target_link_libraries(tasty_pending_load_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.pending_load COMMAND tasty_pending_load_unit)

  add_executable(tasty_romset_alias_reader_unit tests/romset_alias_reader_unit.cpp)
  target_link_libraries(tasty_romset_alias_reader_unit PRIVATE mister_app tasty_test_options)
  target_compile_definitions(tasty_romset_alias_reader_unit PRIVATE
    TASTY_NEOGEO_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/neogeo")
  add_test(NAME tasty.romset_alias_reader COMMAND tasty_romset_alias_reader_unit)
  set_tests_properties(tasty.romset_alias_reader PROPERTIES TIMEOUT 60)

  add_executable(tasty_store_unit tests/store_unit.cpp)
  target_link_libraries(tasty_store_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.store_unit COMMAND tasty_store_unit)

  add_executable(tasty_uart_proc_unit tests/uart_proc_unit.cpp)
  target_link_libraries(tasty_uart_proc_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.uart_proc COMMAND tasty_uart_proc_unit)
  set_tests_properties(tasty.uart_proc PROPERTIES TIMEOUT 60 RESOURCE_LOCK tmp-uart-handoffs)

  add_executable(tasty_mgl_pump_unit tests/mgl_pump_unit.cpp)
  target_link_libraries(tasty_mgl_pump_unit PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.mgl_pump COMMAND tasty_mgl_pump_unit)

  set_tests_properties(tasty.mgl_pump PROPERTIES TIMEOUT 60)

  add_executable(tasty_app_session tests/app_session.cpp)
  target_include_directories(tasty_app_session PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  target_link_libraries(tasty_app_session PRIVATE mister_app mister_boards tasty_test_options)

  target_compile_definitions(tasty_app_session PRIVATE
    TASTY_L4_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
    TASTY_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/goldens")
  add_test(NAME tasty.app_session COMMAND tasty_app_session)

  set_tests_properties(tasty.app_session PROPERTIES TIMEOUT 60 RESOURCE_LOCK tmp-GAMEID)

  add_executable(tasty_corestate_via_supervisor_probe EXCLUDE_FROM_ALL
                 tests/corestate_via_supervisor_probe.cpp)
  target_link_libraries(tasty_corestate_via_supervisor_probe
                        PRIVATE mister_app tasty_test_options)
  add_test(NAME tasty.app_corestate_redef
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                   --target tasty_corestate_via_supervisor_probe)
  set_tests_properties(tasty.app_corestate_redef PROPERTIES
    WILL_FAIL TRUE
    LABELS compile_probe
    TIMEOUT 120

    RUN_SERIAL TRUE)
endif()
