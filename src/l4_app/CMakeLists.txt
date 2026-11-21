set(_tasty_logo_png "${CMAKE_SOURCE_DIR}/src/l4_app/assets/logo.png")
set(_tasty_logo_table "${CMAKE_CURRENT_BINARY_DIR}/front_end_logo_table.cpp")
add_custom_command(
  OUTPUT "${_tasty_logo_table}"
  COMMAND python3 "${CMAKE_SOURCE_DIR}/src/tools/embed_logo.py"
          "${_tasty_logo_png}" "${_tasty_logo_table}"
  DEPENDS "${_tasty_logo_png}" "${CMAKE_SOURCE_DIR}/src/tools/embed_logo.py"
          "${CMAKE_SOURCE_DIR}/src/tools/embed_splash.py"
  VERBATIM)
set_source_files_properties("${_tasty_logo_table}" PROPERTIES GENERATED TRUE)

add_library(mister_app_base STATIC
  src/play_refusal.cpp
  src/remembered_path.cpp
  src/replay_status.cpp
)
target_include_directories(mister_app_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app_base
  PUBLIC mister_cores
  PRIVATE tasty_options)

add_library(mister_app_display STATIC
  src/hps_framebuffer.cpp
)
target_include_directories(mister_app_display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app_display
  PUBLIC mister_app_base
  PRIVATE tasty_options)

add_library(mister_app_channels STATIC
  src/core_frame_counter.cpp
  src/core_input_binding.cpp
  src/file_bytes.cpp
  src/identity_latch.cpp
  src/link_router.cpp
  src/screenshot_pump.cpp
)
target_include_directories(mister_app_channels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app_channels
  PUBLIC mister_app_base mister_cores_tas
  PRIVATE tasty_options)

add_library(mister_app_hd STATIC
  src/hd/backdrop_sampler.cpp
  src/hd/backdrop_scaler.cpp
  src/hd/hd_crumb.cpp
  src/hd/hd_dirty_ledger.cpp
  src/hd/hd_osd_main.cpp
  src/hd/hd_osd_screen.cpp
  src/hd/hd_glass.cpp
  src/hd/front_end_logo.cpp
  src/hd/wallpaper_pattern.cpp
  src/hd/hd_renderer.cpp
  src/hd/backdrop_job.cpp
  src/hd/hd_theme.cpp
  ${_tasty_logo_table}
)
target_include_directories(mister_app_hd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app_hd
  PUBLIC mister_app_display mister_app_base
  PRIVATE tasty_options)

add_library(mister_app_capture STATIC
  src/capture/avi_encoder.cpp
  src/capture/avi_format.cpp
  src/capture/avi_writer.cpp
  src/capture/capture_main.cpp
  src/capture/cscd_codec.cpp
  src/capture/encode_main.cpp
  src/capture/frame_arena.cpp
  src/capture/frame_codec.cpp
  src/capture/frame_copier.cpp
  src/capture/frame_numbering.cpp
  src/capture/frame_hasher.cpp
  src/capture/half_scaler.cpp
  src/capture/png_encoder.cpp
  src/capture/rec_options.cpp
  src/capture/rec_write_main.cpp
  src/capture/recorder_status.cpp
  src/capture/scale_governor.cpp
  src/capture/scaler_frame_source.cpp
  src/capture/sidecar_writer.cpp
  src/capture/zmbv_codec.cpp
)
target_include_directories(mister_app_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(ZLIB REQUIRED)
target_link_libraries(mister_app_capture
  PUBLIC mister_app_base mister_app_channels ZLIB::ZLIB
  PRIVATE tasty_options)

add_library(mister_app_launcher STATIC
  src/launcher/launcher_host.cpp
  src/launcher/launcher_main.cpp
  src/launcher/launcher_screen.cpp
  src/launcher/scanout_lease.cpp
  src/launcher/scanout_packet.cpp
  src/launcher/scanout_relay.cpp
)
target_include_directories(mister_app_launcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app_launcher
  PUBLIC mister_app_display mister_app_base mister_app_channels
  PRIVATE tasty_options)

add_library(mister_app_recents STATIC
  src/recents/recents_store.cpp
  src/recents/played_history.cpp
  src/recents/ui_write_lane.cpp
  src/recents/recent_games.cpp
)
target_include_directories(mister_app_recents PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app_recents
  PUBLIC mister_app_base
  PRIVATE tasty_options)

add_library(mister_app_replay STATIC
  src/replay/hash_stage.cpp
  src/replay/replay_feeder.cpp
  src/replay/replay_gate.cpp
  src/replay/replay_play.cpp
  src/replay/rom_hasher.cpp
  src/replay/run_stage.cpp
  src/replay/scan_stage.cpp
  src/replay/settle_stage.cpp
)
target_include_directories(mister_app_replay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app_replay
  PUBLIC mister_app_channels mister_app_base mister_cores_tas
  PRIVATE tasty_options)

add_library(mister_app_input STATIC
  src/input/host_led_watch.cpp
  src/input/input_build.cpp
  src/input/input_decode.cpp
  src/input/input_emit.cpp
  src/input/input_main.cpp
  src/input/input_pipeline.cpp
  src/input/joy_level_emitter.cpp
  src/input/joy_menu_chords.cpp
  src/input/launcher_key_bridge.cpp
  src/input/link_pad_sink.cpp
  src/input/mouse_divider.cpp
  src/input/pad_egress.cpp
  src/input/ps2_egress.cpp
  src/input/rumble_relay.cpp
  src/input/lw_pad_endpoint.cpp
)
target_include_directories(mister_app_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app_input
  PUBLIC mister_app_channels mister_app_base
  PRIVATE tasty_options)

add_library(mister_app_pcm STATIC
  src/pcm/companion_host.cpp
  src/pcm/mailbox_relay.cpp
  src/pcm/pcm_main.cpp
  src/pcm/pcm_ring_feeder.cpp
)
target_include_directories(mister_app_pcm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app_pcm
  PUBLIC mister_app_base
  PRIVATE tasty_options)

add_library(mister_app STATIC
  src/addon_send.cpp
  src/audio_pump.cpp
  src/bitstream_programmer.cpp
  src/bitstream_resolve.cpp
  src/board_ops.cpp
  src/board_reboot.cpp
  src/cheat_catalog_pack.cpp
  src/cheat_link.cpp
  src/cheat_store.cpp
  src/cmd_fifo.cpp
  src/cmd_line.cpp
  src/companion_walk.cpp
  src/config_apply.cpp
  src/content_pick.cpp
  src/load_belt.cpp
  src/ladder_host_adapter.cpp
  src/bind_tail.cpp
  src/config_saves.cpp
  src/companion_slot.cpp
  src/owner_push.cpp
  src/core_init.cpp
  src/core_builder.cpp
  src/core_switch.cpp
  src/core_option_cells.cpp
  src/core_tenancy.cpp
  src/block_line.cpp
  src/lw_host_enable.cpp
  src/lw_sector_endpoint.cpp
  src/lw_osd_endpoint.cpp
  src/lw_sys_endpoint.cpp
  src/core_window.cpp
  src/dev_mem_window_map.cpp
  src/ini_key_edit.cpp
  src/fabric_state.cpp
  src/file_tx_pieces.cpp
  src/game_id_pump.cpp
  src/identity_record.cpp
  src/idle_blank.cpp
  src/info_decoder.cpp
  src/ini_parse.cpp
  src/intern_exact.cpp
  src/launcher_latch.cpp
  src/link_binder.cpp
  src/link_session.cpp
  src/nvram_upload.cpp
  src/held_transfer.cpp
  src/boot_arm.cpp
  src/boot_handoff.cpp
  src/core_ini.cpp
  src/reboot_answer.cpp
  src/load_key.cpp
  src/load_ladder.cpp
  src/load_walk.cpp
  src/manifest_ladder.cpp
  src/load_window.cpp
  src/disk_relay.cpp
  src/flat_save_servant.cpp
  src/save_upload_relay.cpp
  src/mgl_player.cpp
  src/mgl_pump.cpp
  src/mount_queue.cpp
  src/name_config.cpp
  src/negotiated_core.cpp
  src/osd_wire.cpp
  src/owner_ladder.cpp
  src/prelude_composer.cpp
  src/process_integration.cpp
  src/quiesce_ack.cpp
  src/rbf_resolve.cpp
  src/readiness_watch.cpp
  src/recorder_control.cpp
  src/romset_alias_reader.cpp
  src/rt_main.cpp
  src/rt_park.cpp
  src/save_flush.cpp
  src/save_image_source.cpp
  src/session_bindings.cpp
  src/session_machine.cpp
  src/slot_bindings.cpp
  src/session_rows.cpp
  src/save_stage_director.cpp
  src/session_owner.cpp
  src/spi_encoder.cpp
  src/spi_osd_rows.cpp
  src/start_chain.cpp
  src/stdout_router.cpp
  src/stream_io.cpp
  src/baud_table.cpp
  src/uart_binding.cpp
  src/uart_mode.cpp
  src/uart_performer.cpp
  src/uart_persist.cpp
  src/adv7513_seat.cpp
  src/video_ini.cpp
  src/video_pump.cpp
  src/video_wire.cpp
  src/window_job_service.cpp
  src/save_state_attach.cpp
)
target_include_directories(mister_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app
  PUBLIC mister_app_launcher mister_app_capture mister_app_display mister_app_recents mister_app_replay mister_app_input mister_app_base mister_app_channels mister_app_pcm mister_cores mister_cores_tas
  PRIVATE tasty_options)

add_library(mister_app_connect STATIC
  src/connect/connect_main.cpp
)
target_include_directories(mister_app_connect PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_app_connect
  PUBLIC mister_svc_connect mister_svc
  PRIVATE tasty_options)

target_include_directories(mister_app_capture SYSTEM PRIVATE ${TASTY_LZO_INCLUDE_DIR})
target_link_libraries(mister_app_capture PRIVATE ${TASTY_LZO_LIBRARY})

set(_hdosd "${CMAKE_CURRENT_BINARY_DIR}/hdosd")
file(MAKE_DIRECTORY "${_hdosd}")
foreach(_face pixel-operator/PixelOperator8.ttf pixel-operator/PixelOperator8-Bold.ttf
             noto-sans/NotoSans-Regular.ttf noto-sans/NotoSans-Bold.ttf)
  get_filename_component(_face_name "${_face}" NAME)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/fonts/${_face}"
                 "${_hdosd}/${_face_name}" COPYONLY)
endforeach()
file(GLOB _slint_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/slint/*.slint")
foreach(_slint_file IN LISTS _slint_files)
  get_filename_component(_slint_name "${_slint_file}" NAME)
  configure_file("${_slint_file}" "${_hdosd}/${_slint_name}" COPYONLY)
endforeach()
target_sources(mister_app_hd PRIVATE src/hd/slint_raster.cpp)
set_property(TARGET mister_app_hd PROPERTY SLINT_EMBED_RESOURCES embed-files)
slint_target_sources(mister_app_hd "${_hdosd}/hd_menu.slint")
add_custom_target(tasty_hd_slint_codegen DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/hd_menu.h")
find_package(Threads REQUIRED)
target_link_libraries(mister_app_hd PRIVATE Slint::Slint Threads::Threads ${CMAKE_DL_LIBS})
get_target_property(_slint_inc Slint::Slint INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(mister_app_hd SYSTEM BEFORE PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" ${_slint_inc})
set_source_files_properties(
  "${CMAKE_CURRENT_BINARY_DIR}/slint_generated_hd_menu_1.cpp"
  PROPERTIES COMPILE_OPTIONS "-w")

tasty_add_header_checks(tasty_app_connect_header_checks LIB mister_app_connect HEADERS
  app/connect/connect_cells.h
  app/connect/connect_channel.h
  app/connect/connect_fd_sync.h
  app/connect/connect_host.h
  app/connect/connect_main.h
  app/connect/connect_sink.h
  app/connect/view_mark.h
)
tasty_add_header_checks(tasty_app_base_header_checks LIB mister_app_base HEADERS
  app/percent.h
  app/percents.h
  app/ratio.h
  app/frame_ticks.h
  app/rec_every.h
  app/front_end_status.h
  app/hd_look.h
  app/hd_rect.h
  app/indicator_id.h
  app/info_plane.h
  app/path_text.h
  app/play_refusal.h
  app/replay_status.h
  app/types.h
  app/video_pump_stats.h
  app/video_geometry_record.h
  app/lat_stat.h
  app/log_clock.h
  app/osd_close.h
  app/output_pixels.h
  app/output_size.h
  app/picture_pixels.h
  app/picture_row.h
  app/picture_size.h
  app/rgb_rows.h
  app/round_words.h
  app/row_bytes.h
  app/core_name.h
  app/declared_save_bits.h
  app/core_scope.h
  app/game_id.h
  app/mra_facts.h
  app/output_lock.h
  app/remembered_path.h
  app/session_identity.h
  app/xml_kind.h
)
tasty_add_header_checks(tasty_app_display_header_checks LIB mister_app_display HEADERS
  app/fb_tenancy.h
  app/fb_view.h
  app/hps_framebuffer.h
  app/output_geometry.h
)
tasty_add_header_checks(tasty_app_channels_header_checks LIB mister_app_channels HEADERS
  app/block_grant.h
  app/conf_str_cell.h
  app/core_frame_counter.h
  app/core_frame_record.h
  app/core_input_binding.h
  app/core_input_facts.h
  app/delivered.h
  app/event.h
  app/event_dispatch.h
  app/event_sink.h
  app/file_bytes.h
  app/file_intern.h
  app/frame_demand.h
  app/game_id_pump.h
  app/identity_latch.h
  app/interned_file.h
  app/launcher_state.h
  app/link_bytes.h
  app/link_encoder.h
  app/link_event_dispatch.h
  app/link_event_facts.h
  app/link_op_ctx.h
  app/link_op_dispatch.h
  app/link_op_facts.h
  app/link_router.h
  app/link_rx_channel.h
  app/link_tx_channel.h
  app/link_tx_reader.h
  app/lw_block_endpoint.h
  app/pad_grant.h
  app/pad_relay_cell.h
  app/pad_relay_tally.h
  app/pad_route.h
  app/screen_gen.h
  app/screenshot_pump.h
  app/screenshot_queue.h
  app/ui_request.h
  app/ui_request_dispatch.h
  app/ui_request_ring.h
)
tasty_add_header_checks(tasty_app_hd_header_checks LIB mister_app_hd HEADERS
  app/hd/px_plane.h
  app/hd/backdrop_job.h
  app/hd/backdrop_sampler.h
  app/hd/backdrop_scaler.h
  app/hd/band_fade_pattern.h
  app/hd/band_gradient_pattern.h
  app/hd/black_pattern.h
  app/hd/checkers_pattern.h
  app/hd/floor_ratio_walk.h
  app/hd/front_end_logo.h
  app/hd/front_end_pattern.h
  app/hd/spectrum_pattern.h
  app/hd/wallpaper_pattern.h
  app/hd/hd_dirty_ledger.h
  app/hd/hd_flip_slot.h
  app/hd/hd_help.h
  app/hd/hd_layout.h
  app/hd/hd_look_ini.h
  app/hd/hd_metrics.h
  app/hd/hd_osd_ask.h
  app/hd/hd_osd_main.h
  app/hd/hd_osd_status.h
  app/hd/hd_osd_screen.h
  app/hd/hd_panel_plan.h
  app/hd/hd_raster.h
  app/hd/slint_ordinals.h
  app/hd/slint_raster.h
  app/hd/hd_glass.h
  app/hd/hd_renderer.h
  app/hd/hd_visible_stats.h
  app/hd/hd_style.h
  app/hd/hd_surface.h
  app/hd/hd_template_traits.h
  app/hd/hd_theme.h
  app/hd/hd_themes.h
  app/hd/hd_tick_pacer.h
  app/hd/pause_badge.h
  app/hd/page_description.h
  app/hd/tier_set.h
  app/hd/canvas_gen.h
  app/hd/flip_buffer.h
  app/hd/hd_badge_view.h
  app/hd/hd_captions.h
  app/hd/hd_crumb.h
  app/hd/hd_dirty.h
  app/hd/hd_epoch.h
  app/hd/hd_flip_holds.h
  app/hd/hd_flip_seq.h
  app/hd/hd_status_slots.h
  app/hd/hd_visible_window.h
  app/hd/hd_window.h
  app/hd/vec3.h
)
tasty_add_header_checks(tasty_app_capture_header_checks LIB mister_app_capture HEADERS
  app/capture/avi_encoder.h
  app/capture/avi_format.h
  app/capture/avi_frame_count.h
  app/capture/avi_frame_index.h
  app/capture/avi_write_status.h
  app/capture/avi_writer.h
  app/capture/capture_main.h
  app/capture/chunk_dispatch.h
  app/capture/chunk_place.h
  app/capture/chunk_slot.h
  app/capture/cscd_codec.h
  app/capture/frame_codec.h
  app/capture/rec_options.h
  app/capture/zmbv_codec.h
  app/capture/encode_main.h
  app/capture/encode_status.h
  app/capture/frame_arena.h
  app/capture/frame_budget.h
  app/capture/frame_copier.h
  app/capture/frame_numbering.h
  app/capture/frame_hasher.h
  app/capture/frame_stamp.h
  app/capture/half_scaler.h
  app/capture/raw_frame_slot.h
  app/capture/rec_control.h
  app/capture/rec_write_main.h
  app/capture/scale_governor.h
  app/capture/step_count.h
  app/capture/rec_write_status.h
  app/capture/recorder_status.h
  app/capture/sidecar_msg.h
  app/capture/sidecar_writer.h
  app/capture/frame_source.h
  app/capture/scaler_frame_source.h
  app/capture/png_encoder.h
  app/capture/raw_frame_dispatch.h
  app/capture/recording_name.h
  app/capture/segment_index.h
)
tasty_add_header_checks(tasty_app_launcher_header_checks LIB mister_app_launcher HEADERS
  app/launcher/launcher_demand.h
  app/launcher/launcher_fb_ack.h
  app/launcher/launcher_host.h
  app/launcher/launcher_main.h
  app/launcher/launcher_profile.h
  app/launcher/launcher_screen.h
  app/launcher/scanout_channel.h
  app/launcher/scanout_lease.h
  app/launcher/scanout_packet.h
  app/launcher/scanout_relay.h
  app/launcher/uio_burst.h
)
tasty_add_header_checks(tasty_app_recents_header_checks LIB mister_app_recents HEADERS
  app/recents/recents_store.h
  app/recents/text_field.h
  app/recents/recent_games.h
  app/recents/played_history.h
  app/recents/ui_write_lane.h
  app/recents/write_verdict.h
  app/recents/verdict_sinks.h
)
tasty_add_header_checks(tasty_app_replay_header_checks LIB mister_app_replay HEADERS
  app/replay/epoch_rule.h
  app/replay/frame_reference.h
  app/replay/hash_stage.h
  app/replay/replay_control.h
  app/replay/replay_feeder.h
  app/replay/replay_gate.h
  app/replay/replay_msg.h
  app/replay/replay_play.h
  app/replay/replay_ring.h
  app/replay/replay_stage.h
  app/replay/replay_wiring.h
  app/replay/rom_hasher.h
  app/replay/run_stage.h
  app/replay/scan_stage.h
  app/replay/settle_stage.h
)
tasty_add_header_checks(tasty_app_input_header_checks LIB mister_app_input HEADERS
  app/input/capture_arm.h
  app/input/capture_hit.h
  app/input/device_table_loans.h
  app/input/host_led_watch.h
  app/input/input_build.h
  app/input/input_decode.h
  app/input/input_decode_tally.h
  app/input/input_emit.h
  app/input/input_gates_cell.h
  app/input/input_key_counts.h
  app/input/input_main.h
  app/input/input_park.h
  app/input/input_pipeline.h
  app/input/input_sample.h
  app/input/joy_level_emitter.h
  app/input/link_pad_sink.h
  app/input/pad_egress.h
  app/input/pad_level_sink.h
  app/input/joy_menu_chords.h
  app/input/joymap_ends.h
  app/input/key_focus.h
  app/input/launcher_key_bridge.h
  app/input/mouse_accumulator.h
  app/input/mouse_divider.h
  app/input/ps2_egress.h
  app/input/raw_key_edge.h
  app/input/rumble_players_cell.h
  app/input/rumble_relay.h
  app/input/lw_pad_endpoint.h
)
tasty_add_header_checks(tasty_app_pcm_header_checks LIB mister_app_pcm HEADERS
  app/pcm/companion_bind.h
  app/pcm/companion_host.h
  app/pcm/mailbox_act_writer.h
  app/pcm/mailbox_relay.h
  app/pcm/mailbox_slot.h
  app/pcm/pcm_command.h
  app/pcm/pcm_command_dispatch.h
  app/pcm/pcm_main.h
  app/pcm/pcm_ring_feeder.h
)
tasty_add_header_checks(tasty_app_header_checks LIB mister_app HEADERS
  app/baud_table.h
  app/baud_words.h
  app/mgl_hold.h
  app/bitstream_programmer.h
  app/bitstream_resolve.h
  app/bay_counts.h
  app/intern_exact.h
  app/launcher_latch.h
  app/recorder_control.h
  app/board_ops.h
  app/cheat_record.h
  app/cheat_row.h
  app/cheat_store.h
  app/cmd_fifo.h
  app/cmd_line.h
  app/cmd_verb.h
  app/cmd_verb_sink.h
  app/config_slot_cell.h
  app/ladder_cell.h
  app/mount_status_cell.h
  app/save_director_status.h
  app/autosave_cells.h
  app/save_extent_cell.h
  app/core_init.h
  app/core_option_cells.h
  app/core_window.h
  app/ini_key_edit.h
  app/window_job_service.h
  app/save_state_attach.h
  app/save_state_facts.h
  app/fabric_state.h
  app/park_receipt.h
  app/switch_park.h
  app/quiesce_ack.h
  app/core_switch.h
  app/core_block_roles.h
  app/core_block_row.h
  app/save_channel_roles.h
  app/slot_bindings.h
  app/core_tenancy.h
  app/held_transfer.h
  app/ui_write_ticker.h
  app/save_state_ui_ticker.h
  app/ui_round_port.h
  app/ui_pass_work.h
  app/info_sink.h
  app/info_origin.h
  app/map_save_verdict.h
  app/load_request.h
  app/mgl.h
  app/mgl_player.h
  app/mgl_pump.h
  app/midi_link_facts.h
  app/name_config.h
  app/osd_row_tally.h
  app/spi_osd_rows.h
  app/osd_wire.h
  app/ui_page_record.h
  app/page_label.h
  app/disk_slot.h
  app/disk_relay.h
  app/flat_save_servant.h
  app/save_upload_slot.h
  app/save_upload_relay.h
  app/companion_walk.h
  app/addon_send.h
  app/romset_alias_reader.h
  app/romset_aliases.h
  app/process_integration.h
  app/rbf_resolve.h
  app/rt_main.h
  app/round_budget.h
  app/rt_park.h
  app/session_bindings.h
  app/save_image_source.h
  app/start_chain.h
  app/cmd_router.h
  app/identity_record.h
  app/info_decoder.h
  app/idle_knobs.h
  app/autosave_knobs.h
  app/echo_rate.h
  app/echo_wait.h
  app/rung.h
  app/rung_edges.h
  app/rung_rows.h
  app/load_belt.h
  app/ladder_host_adapter.h
  app/answered.h
  app/belt_orders.h
  app/bind_tail.h
  app/config_saves.h
  app/companion_slot.h
  app/rung_mask.h
  app/picked_load.h
  app/window_tally.h
  app/ini_cell.h
  app/ini_parse.h
  app/content_request.h
  app/file_tx_pieces.h
  app/dev_mem_window_map.h
  app/file_tx_level.h
  app/load_ladder.h
  app/manifest_ladder.h
  app/load_window.h
  app/load_window_counts.h
  app/load_window_map.h
  app/encoder_roles.h
  app/cheat_call.h
  app/cheat_link.h
  app/link_op_producers.h
  app/link_producers.h
  app/link_binder.h
  app/link_rows.h
  app/lw_pad.h
  app/lw_lend_set.h
  app/block_line.h
  app/lw_grant_stamp.h
  app/lw_sector.h
  app/lw_sector_endpoint.h
  app/sector_grant.h
  app/sector_route.h
  app/lw_osd.h
  app/lw_osd_endpoint.h
  app/osd_grant.h
  app/osd_route.h
  app/lw_sys.h
  app/lw_sys_endpoint.h
  app/sys_grant.h
  app/sys_route.h
  app/session_machine.h
  app/session_state.h
  app/deadline_miss_log.h
  app/log_lane.h
  app/log_rec.h
  app/log_rec_dispatch.h
  app/session_rows.h
  app/run_policy.h
  app/supervisor_parts.h
  app/stream_io.h
  app/session_owner.h
  app/main_pass.h
  app/link_session.h
  app/load_key.h
  app/spi_encoder.h
  app/stdout_paths.h
  app/stdout_router.h
  app/uart_binding.h
  app/uart_handoff.h
  app/uart_mode.h
  app/uart_mode_facts.h
  app/uart_modes.h
  app/uart_performer.h
  app/uart_persist.h
  app/adv7513_seat.h
  app/audio_pump.h
  app/prelude_composer.h
  app/prelude_stats.h
  app/staged_frame.h
  app/staged_frame_dispatch.h
  app/video_ini.h
  app/video_pump.h
  app/video_wire.h
  app/act_keys.h
  app/all_leases_home.h
  app/bay_releases.h
  app/boot_args.h
  app/boot_restore.h
  app/bracket_close.h
  app/built_core.h
  app/carrier.h
  app/session_routes.h
  app/cheat_apply.h
  app/cheat_blob_cell.h
  app/cheat_catalog_cell.h
  app/config_rel.h
  app/core_deleter.h
  app/core_option_acts.h
  app/core_terms.h
  app/core_window_map.h
  app/diag_counters.h
  app/dip_cell.h
  app/dip_choices.h
  app/dip_labels.h
  app/doorbell_cell.h
  app/fallback_counts.h
  app/fenced_core_signals.h
  app/link_condition.h
  app/lw_block_lend.h
  app/lw_lend_key.h
  app/mgl_delay.h
  app/mgl_item.h
  app/mgl_item_dispatch.h
  app/mgl_slot_remember.h
  app/option_cell.h
  app/owed_mount.h
  app/owed_save_bind.h
  app/owner_tally.h
  app/progress_image_sink.h
  app/relay_stamp.h
  app/retiree.h
  app/sdram_size_code.h
  app/session_tally.h
  app/start_in_flight.h
  app/switch_phase.h
  app/tx_digest_cell.h
  app/uart_handoff_dispatch.h
  app/ui_admission.h
  app/ui_round_edge.h
)

