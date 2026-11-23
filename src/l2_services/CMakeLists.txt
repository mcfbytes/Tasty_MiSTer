add_library(mister_svc STATIC
  src/alt_ini_index.cpp
  src/asset_name.cpp
  src/blank_bank_filter.cpp
  src/browse_rules.cpp
  src/card_writer.cpp
  src/crc32_accumulator.cpp
  src/crc32_extend.cpp
  src/audio_service.cpp
  src/config.cpp
  src/config_field.cpp
  src/config_parser.cpp
  src/confine_under.cpp
  src/disc_file_access.cpp
  src/disk_format_pick.cpp
  src/disk_format_row.cpp
  src/open_disk_image.cpp
  src/g64_synthesis.cpp
  src/gcr.cpp
  src/sector_mirror.cpp
  src/track_file_mirror.cpp
  src/gcr_track_image.cpp
  src/t64.cpp
  src/t64_image.cpp
  src/atx_image.cpp
  src/atx_drive.cpp
  src/flat_sector_image.cpp
  src/open_sector_image.cpp
  src/durable_write_backend.cpp
  src/entropy.cpp
  src/evdev_code.cpp
  src/file_slot_backend.cpp
  src/gzip_inflate.cpp
  src/io_main.cpp
  src/nic_bridge.cpp
  src/nic_host_port.cpp
  src/nic_main.cpp
  src/nic_mapped_window.cpp
  src/option_codec.cpp
  src/posix_file.cpp
  src/raw_disk_image.cpp
  src/six_and_two.cpp
  src/nib_codec.cpp
  src/woz_format.cpp
  src/woz525_codec.cpp
  src/woz35_codec.cpp
  src/twomg_image.cpp
  src/dc42_image.cpp
  src/archive_copy_image.cpp
  src/image_admission.cpp
  src/adf_codec.cpp
  src/read_bounded.cpp
  src/save_frame_layer.cpp
  src/save_stage_keeper.cpp
  src/staged_save.cpp
  src/storage_service.cpp
  src/game_asset_finder.cpp
  src/memory_mounts.cpp
  src/vfs.cpp
  src/xml_scan.cpp
  src/xml_scanner.cpp
  src/disk_geometry.cpp
  src/vhd_sidecar.cpp
)
target_include_directories(mister_svc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_svc
  PUBLIC mister_proto
  PRIVATE tasty_options)

add_library(mister_svc_connect STATIC
  src/connect/wpa_conf_file.cpp
  src/connect/wpa_conf_store.cpp
  src/connect/ifup_starter.cpp
)

target_include_directories(mister_svc_connect PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_svc_connect
  PUBLIC mister_os_connect
  PRIVATE tasty_options)

add_library(mister_svc_input STATIC
  src/input/admission_rule.cpp
  src/input/deadzone_rule.cpp
  src/input/device_facts.cpp
  src/input/hotplug_watch.cpp
  src/input/input_device.cpp
  src/input/input_emitter.cpp
  src/input/input_enumerator.cpp
  src/input/input_rules.cpp
  src/input/input_service.cpp
  src/input/joy_plan.cpp
  src/input/map_store.cpp
  src/input/mapping_wizard.cpp
  src/input/mouse_fixup_rule.cpp
  src/input/no_merge_rule.cpp
  src/input/player_slots.cpp
  src/input/rumble_channel.cpp
)

target_link_libraries(mister_svc_input
  PUBLIC mister_svc mister_proto_link
  PRIVATE tasty_options)

add_library(mister_svc_disc STATIC
  src/disc/chd_backing.cpp
  src/disc/chd_prefetch.cpp
  src/disc/chd_source.cpp
  src/disc/chd_track.cpp
  src/disc/cue_programs.cpp
  src/disc/data_sector_sealer.cpp
  src/disc/disc_engine.cpp
  src/disc/disc_geometry.cpp
  src/disc/disc_read_service.cpp
  src/disc/disc_store.cpp
  src/disc/file_table_backing.cpp
  src/disc/prefetch_main.cpp
  src/disc/rw_interleaver.cpp
  src/disc/sector_descrambler.cpp
  src/disc/subcode_interleaver.cpp
  src/disc/subcode_sidecar.cpp
  src/disc/toc_frame.cpp
)

target_link_libraries(mister_svc_disc
  PUBLIC mister_svc
  PRIVATE tasty_options)

add_library(mister_svc_video STATIC
  src/video/adv7513_breaker.cpp
  src/video/adv7513_bus.cpp
  src/video/adv7513_reg_write.cpp
  src/video/dv1_spd.cpp
  src/video/cec_state_machine.cpp
  src/video/cvt.cpp
  src/video/edid_store.cpp
  src/video/filter_store.cpp
  src/video/modeline.cpp
  src/video/pll_solver.cpp
  src/video/scaling_words.cpp
  src/video/hdmi_link.cpp
  src/video/video_service.cpp
  src/video/video_timing.cpp
  src/video/probe_ops.cpp
  src/video/video_wire_emit.cpp
)

target_link_libraries(mister_svc_video
  PUBLIC mister_svc mister_proto_link
  PRIVATE tasty_options)

find_package(ZLIB REQUIRED)
target_link_libraries(mister_svc PRIVATE ZLIB::ZLIB)

if(NOT TASTY_LIBCHDR_INCLUDE_DIR OR NOT TASTY_LIBCHDR_LIBRARY)
  message(FATAL_ERROR
    "libchdr is required but was not found "
    "(include=${TASTY_LIBCHDR_INCLUDE_DIR} lib=${TASTY_LIBCHDR_LIBRARY}). "
    "The x86-64 host has none; use the arm-buildroot toolchain, whose sysroot "
    "ships libchdr.so and include/libchdr/chd.h.")
endif()
target_include_directories(mister_svc_disc SYSTEM PRIVATE ${TASTY_LIBCHDR_INCLUDE_DIR})
target_link_libraries(mister_svc_disc PUBLIC ${TASTY_LIBCHDR_LIBRARY})

if(NOT TASTY_MINIZIP_INCLUDE_DIR OR NOT TASTY_MINIZIP_LIBRARY)
  message(FATAL_ERROR
    "libminizip is required but was not found "
    "(include=${TASTY_MINIZIP_INCLUDE_DIR} lib=${TASTY_MINIZIP_LIBRARY}). "
    "The x86-64 host ships the armhf -dev package's headers under "
    "/usr/include/minizip but no x86-64 library; use the arm-buildroot "
    "toolchain, whose sysroot ships libminizip.so and include/minizip/unzip.h.")
endif()
target_include_directories(mister_svc SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})

target_link_libraries(mister_svc PUBLIC ${TASTY_MINIZIP_LIBRARY})

target_sources(mister_svc PRIVATE src/vfs_zip.cpp)

tasty_add_header_checks(tasty_l2_header_checks LIB mister_svc HEADERS
  svc/save_files_on_io.h
  svc/vfs_image_opener.h
  svc/alt_ini_index.h
  svc/ini_catalog.h
  svc/archive.h
  svc/asset_name.h
  svc/asset_query.h
  svc/afilter_parser.h
  svc/audio_service.h
  svc/filter_wire_image.h
  svc/blank_region.h
  svc/blank_save.h
  svc/block_ctl.h
  svc/card_dirs.h
  svc/card_writer.h
  svc/slot_arena.h
  svc/config.h
  svc/config_field.h
  svc/config_parser.h
  svc/config_snapshot.h
  svc/confine_under.h
  svc/crc32_accumulator.h
  svc/dir_entry.h
  svc/disc_file_access.h
  svc/disk_codec.h
  svc/disk_format_pick.h
  svc/disk_format_row.h
  svc/disk_image.h
  svc/g64_synthesis.h
  svc/gcr.h
  svc/gcr_caps.h
  svc/gcr_format_rows.h
  svc/gcr_layout.h
  svc/gcr_sector_map.h
  svc/gcr_track_image.h
  svc/gcr_types.h
  svc/mirror_piece.h
  svc/mirror_source.h
  svc/sector_mirror.h
  svc/track_file_mirror.h
  svc/t64.h
  svc/t64_image.h
  svc/atx_types.h
  svc/atx_drive.h
  svc/atx_image.h
  svc/flat_sector_image.h
  svc/sector_ask.h
  svc/sector_geometry.h
  svc/sector_image.h
  svc/sector_read.h
  svc/sector_types.h
  svc/track_image.h
  svc/durable_write.h
  svc/entropy.h
  svc/file.h
  svc/game_asset_finder.h
  svc/gzip_inflate.h
  svc/image_opener.h
  svc/input_gates.h
  svc/but_sw_wire.h
  svc/option_codec.h
  svc/parse_error.h
  svc/per_core_setting.h
  svc/raw_disk_image.h
  svc/nib_track.h
  svc/six_and_two.h
  svc/nib_codec.h
  svc/woz_format.h
  svc/woz525_codec.h
  svc/woz35_codec.h
  svc/twomg_image.h
  svc/dc42_image.h
  svc/archive_copy_image.h
  svc/image_facts.h
  svc/image_field.h
  svc/image_admission.h
  svc/mount_refusal.h
  svc/adf_codec.h
  svc/scan_filter.h
  svc/read_bounded.h
  svc/move_names.h
  svc/rename_files.h
  svc/search_policy.h
  svc/io_coworker.h
  svc/io_main.h
  svc/nic_bridge.h
  svc/nic_control.h
  svc/nic_control_dispatch.h
  svc/nic_frame.h
  svc/nic_grant.h
  svc/nic_host_port.h
  svc/nic_host_seed.h
  svc/nic_link.h
  svc/nic_link_plan.h
  svc/nic_mac.h
  svc/nic_main.h
  svc/nic_mapped_window.h
  svc/nic_personality.h
  svc/nic_poll.h
  svc/nic_port.h
  svc/nic_slot_word.h
  svc/nic_status.h
  svc/nic_window.h
  svc/nic_window_row.h
  svc/nic_window_source.h
  svc/io_pass.h
  svc/file_slot_backend.h
  svc/blank_bank_filter.h
  svc/save_banks.h
  svc/save_frame_layer.h
  svc/save_stage_crossings.h
  svc/save_stage_keeper.h
  svc/slot_path.h
  svc/staged_save.h
  svc/storage_backend.h
  svc/storage_lifecycle.h
  svc/storage_service.h
  svc/types.h
  svc/msf.h
  svc/track_row.h
  svc/filter_format.h
  svc/filter_bank_index.h
  svc/filter_send_plan.h
  svc/evdev_range.h
  svc/io_meter.h
  svc/memory_mounts.h
  svc/vfs.h
  svc/xml_attr.h
  svc/xml_scan.h
  svc/xml_scanner.h
  svc/xml_tag.h
  svc/xml_token.h
  svc/disk_geometry.h
  svc/vhd_sidecar.h
)
tasty_add_header_checks(tasty_svc_connect_header_checks LIB mister_svc_connect HEADERS
  svc/connect/bt_page_mode.h
  svc/connect/bt_activity.h
  svc/connect/bt_device_view.h
  svc/connect/bt_failure.h
  svc/connect/bt_presence.h
  svc/connect/bt_prompt.h
  svc/connect/bt_prompt_view.h
  svc/connect/bt_radio_binding.h
  svc/connect/bt_service.h
  svc/connect/bt_view.h
  svc/connect/connect_fd_delta.h
  svc/connect/connect_request.h
  svc/connect/conf_stamp.h
  svc/connect/ifup_starter.h
  svc/connect/prompt_answer.h
  svc/connect/prompt_gen.h
  svc/connect/view_gen.h
  svc/connect/wifi_failure.h
  svc/connect/wifi_link.h
  svc/connect/wifi_presence.h
  svc/connect/wifi_saved_view.h
  svc/connect/wifi_scan_view.h
  svc/connect/wifi_service.h
  svc/connect/wifi_view.h
  svc/connect/wpa_conf_file.h
  svc/connect/wpa_conf_store.h
)
tasty_add_header_checks(tasty_svc_input_header_checks LIB mister_svc_input HEADERS
  svc/input/axis_position.h
  svc/input/axis_slot.h
  svc/input/osd_combo.h
  svc/input/vid_pid_key.h
  svc/input/packed_keys.h
  svc/input/pad_signature.h
  svc/input/rumble_pad_set.h
  svc/input/admission_rule.h
  svc/input/analog_reshape.h
  svc/input/analog_xy.h
  svc/input/axis_cal.h
  svc/input/button_map.h
  svc/input/joy_bit.h
  svc/input/sys_button.h
  svc/input/deadzone_rule.h
  svc/input/device_identity.h
  svc/input/device_match.h
  svc/input/spinner_policy.h
  svc/input/device_report.h
  svc/input/device_table.h
  svc/input/device_facts.h
  svc/input/hotplug_watch.h
  svc/input/dial_report.h
  svc/input/dial_gate.h
  svc/input/input_counters.h
  svc/input/input_device.h
  svc/input/input_emitter.h
  svc/input/input_enumerator.h
  svc/input/input_service.h
  svc/input/input_service_counts.h
  svc/input/rumble_out.h
  svc/input/joy_plan.h
  svc/input/key_event.h
  svc/input/map_store.h
  svc/input/mapping_wizard.h
  svc/input/mouse_fixup_rule.h
  svc/input/no_merge_rule.h
  svc/input/player_slots.h
  svc/input/quirk_id.h
  svc/input/stick_cal.h
  svc/input/stick_index.h
  svc/input/stick_reach.h
  svc/input/stick_state.h
)
tasty_add_header_checks(tasty_svc_disc_header_checks LIB mister_svc_disc HEADERS
  svc/disc/disc_identity.h
  svc/disc/chd_backing.h
  svc/disc/chd_geometry.h
  svc/disc/chd_meta_row.h
  svc/disc/chd_prefetch.h
  svc/disc/chd_source.h
  svc/disc/cue_mount.h
  svc/disc/cue_policy.h
  svc/disc/cue_program.h
  svc/disc/disc_ask.h
  svc/disc/disc_backing.h
  svc/disc/disc_counters.h
  svc/disc/disc_engine.h
  svc/disc/disc_geometry.h
  svc/disc/disc_rules.h
  svc/disc/disc_mount_slot.h
  svc/disc/disc_mounter.h
  svc/disc/disc_read_service.h
  svc/disc/disc_read_slot.h
  svc/disc/disc_reader.h
  svc/disc/disc_store.h
  svc/disc/disc_take.h
  svc/disc/file_table_backing.h
  svc/disc/frame_shaper.h
  svc/disc/index_pair_cue_program.h
  svc/disc/index_triple_cue_program.h
  svc/disc/prefetch_main.h
  svc/disc/q_channel.h
  svc/disc/q_control.h
  svc/disc/rw_interleaver.h
  svc/disc/sector_descrambler.h
  svc/disc/sector_header.h
  svc/disc/sector_program.h
  svc/disc/sentinel_cue_program.h
  svc/disc/subcode_sidecar.h
  svc/disc/toc.h
  svc/disc/toc_frame.h
  svc/disc/toc_point.h
  svc/disc/track.h
  svc/disc/unbound_disc.h
  svc/disc/seek_span.h
)
tasty_add_header_checks(tasty_svc_video_header_checks LIB mister_svc_video HEADERS
  svc/video/filter_phase.h
  svc/video/pll_payload.h
  svc/video/res_word.h
  svc/video/modeline_item.h
  svc/video/adv7513_breaker.h
  svc/video/adv7513_bus.h
  svc/video/adv7513_hdr_mode.h
  svc/video/adv7513_init_options.h
  svc/video/adv7513_io.h
  svc/video/adv7513_linux_adapter.h
  svc/video/adv7513_probe_report.h
  svc/video/adv7513_reg_map.h
  svc/video/adv7513_reg_write.h
  svc/video/adv7513_spd_packet.h
  svc/video/adv7513_sub_map.h
  svc/video/dv1_spd.h
  svc/video/probe_ops.h
  svc/video/shadow_mask_word.h
  svc/video/cec_state_machine.h
  svc/video/cvt.h
  svc/video/edid_store.h
  svc/video/filter_bank.h
  svc/video/filter_resend.h
  svc/video/filter_set.h
  svc/video/filter_store.h
  svc/video/scaler_slot_mode.h
  svc/video/geometry_gate.h
  svc/video/hdmi_link.h
  svc/video/i2c_adapter.h
  svc/video/resolution.h
  svc/video/axis_timing.h
  svc/video/modeline.h
  svc/video/pll_block.h
  svc/video/pll_params.h
  svc/video/pll_solver.h
  svc/video/scaling_policy.h
  svc/video/scaling_words.h
  svc/video/video_sample.h
  svc/video/aspect_raw.h
  svc/video/fb_par_crc.h
  svc/video/video_service.h
  svc/video/video_timing.h
  svc/video/parsed_kind.h
  svc/video/parsed_mode.h
  svc/video/preset_pick.h
  svc/video/stored_mode.h
  svc/video/filter_kernel.h
  svc/video/video_wire_emit.h
  svc/video/yc_table.h
)

