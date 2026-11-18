add_library(mister_svc STATIC
  src/alt_ini_index.cpp
  src/asset_name.cpp
  src/browse_rules.cpp
  src/card_writer.cpp
  src/crc32_accumulator.cpp
  src/crc32_extend.cpp
  src/audio_service.cpp
  src/config.cpp
  src/config_field.cpp
  src/config_parser.cpp
  src/disc_file_access.cpp
  src/disk_format_row.cpp
  src/disk_image.cpp
  src/durable_write_backend.cpp
  src/entropy.cpp
  src/evdev_code.cpp
  src/file_slot_backend.cpp
  src/gzip_inflate.cpp
  src/io_main.cpp
  src/option_codec.cpp
  src/posix_file.cpp
  src/raw_disk_image.cpp
  src/read_bounded.cpp
  src/save_stage_keeper.cpp
  src/staged_save.cpp
  src/storage_service.cpp
  src/game_asset_finder.cpp
  src/memory_mounts.cpp
  src/vfs.cpp
  src/xml_scan.cpp
  src/xml_scanner.cpp
)
target_include_directories(mister_svc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_svc
  PUBLIC mister_proto
  PRIVATE tasty_options)

add_library(mister_svc_connect STATIC
  src/wpa_conf_file.cpp
  src/wpa_conf_store.cpp
  src/ifup_starter.cpp
)

target_include_directories(mister_svc_connect PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_svc_connect
  PUBLIC mister_os_connect
  PRIVATE tasty_options)

add_library(mister_svc_input STATIC
  src/admission_rule.cpp
  src/deadzone_rule.cpp
  src/device_facts.cpp
  src/hotplug_watch.cpp
  src/input_device.cpp
  src/input_emitter.cpp
  src/input_enumerator.cpp
  src/input_rules.cpp
  src/input_service.cpp
  src/joy_plan.cpp
  src/map_store.cpp
  src/mapping_wizard.cpp
  src/mouse_fixup_rule.cpp
  src/no_merge_rule.cpp
  src/player_slots.cpp
  src/rumble_channel.cpp
)

target_link_libraries(mister_svc_input
  PUBLIC mister_svc mister_proto_link
  PRIVATE tasty_options)

add_library(mister_svc_disc STATIC
  src/chd_backing.cpp
  src/chd_prefetch.cpp
  src/chd_source.cpp
  src/chd_track.cpp
  src/cue_programs.cpp
  src/data_sector_sealer.cpp
  src/disc_engine.cpp
  src/disc_geometry.cpp
  src/disc_read_service.cpp
  src/disc_store.cpp
  src/file_table_backing.cpp
  src/prefetch_main.cpp
  src/rw_interleaver.cpp
  src/sector_descrambler.cpp
  src/subcode_interleaver.cpp
  src/subcode_sidecar.cpp
  src/toc_frame.cpp
)

target_link_libraries(mister_svc_disc
  PUBLIC mister_svc
  PRIVATE tasty_options)

add_library(mister_svc_video STATIC
  src/adv7513_breaker.cpp
  src/adv7513_bus.cpp
  src/adv7513_reg_write.cpp
  src/dv1_spd.cpp
  src/cec_state_machine.cpp
  src/cvt.cpp
  src/edid_store.cpp
  src/filter_store.cpp
  src/modeline.cpp
  src/pll_solver.cpp
  src/scaling_words.cpp
  src/hdmi_link.cpp
  src/video_service.cpp
  src/video_timing.cpp
  src/probe_ops.cpp
  src/video_wire_emit.cpp
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
  svc/card_writer.h
  svc/slot_arena.h
  svc/config.h
  svc/config_field.h
  svc/config_parser.h
  svc/config_snapshot.h
  svc/crc32_accumulator.h
  svc/dir_entry.h
  svc/disc_file_access.h
  svc/disk_codec.h
  svc/disk_format_row.h
  svc/disk_image.h
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
  svc/scan_filter.h
  svc/read_bounded.h
  svc/rename_files.h
  svc/search_policy.h
  svc/io_coworker.h
  svc/io_main.h
  svc/io_pass.h
  svc/file_slot_backend.h
  svc/save_stage_crossings.h
  svc/save_stage_keeper.h
  svc/slot_path.h
  svc/staged_save.h
  svc/storage_backend.h
  svc/storage_lifecycle.h
  svc/storage_service.h
  svc/types.h
  svc/evdev_range.h
  svc/io_meter.h
  svc/memory_mounts.h
  svc/vfs.h
  svc/xml_attr.h
  svc/xml_scan.h
  svc/xml_scanner.h
  svc/xml_tag.h
  svc/xml_token.h
)
tasty_add_header_checks(tasty_svc_connect_header_checks LIB mister_svc_connect HEADERS
  svc/bt_page_mode.h
  svc/bt_activity.h
  svc/bt_device_view.h
  svc/bt_failure.h
  svc/bt_presence.h
  svc/bt_prompt.h
  svc/bt_prompt_view.h
  svc/bt_radio_binding.h
  svc/bt_service.h
  svc/bt_view.h
  svc/connect_fd_delta.h
  svc/connect_request.h
  svc/conf_stamp.h
  svc/ifup_starter.h
  svc/prompt_answer.h
  svc/prompt_gen.h
  svc/view_gen.h
  svc/wifi_failure.h
  svc/wifi_link.h
  svc/wifi_presence.h
  svc/wifi_saved_view.h
  svc/wifi_scan_view.h
  svc/wifi_service.h
  svc/wifi_view.h
  svc/wpa_conf_file.h
  svc/wpa_conf_store.h
)
tasty_add_header_checks(tasty_svc_input_header_checks LIB mister_svc_input HEADERS
  svc/axis_position.h
  svc/axis_slot.h
  svc/osd_combo.h
  svc/vid_pid_key.h
  svc/packed_keys.h
  svc/pad_signature.h
  svc/rumble_pad_set.h
  svc/admission_rule.h
  svc/analog_reshape.h
  svc/analog_xy.h
  svc/axis_cal.h
  svc/button_map.h
  svc/joy_bit.h
  svc/sys_button.h
  svc/deadzone_rule.h
  svc/device_identity.h
  svc/device_match.h
  svc/spinner_policy.h
  svc/device_report.h
  svc/device_table.h
  svc/device_facts.h
  svc/hotplug_watch.h
  svc/input_counters.h
  svc/input_device.h
  svc/input_emitter.h
  svc/input_enumerator.h
  svc/input_service.h
  svc/input_service_counts.h
  svc/rumble_out.h
  svc/joy_plan.h
  svc/key_event.h
  svc/map_store.h
  svc/mapping_wizard.h
  svc/mouse_fixup_rule.h
  svc/no_merge_rule.h
  svc/player_slots.h
  svc/quirk_id.h
  svc/stick_cal.h
  svc/stick_index.h
  svc/stick_state.h
)
tasty_add_header_checks(tasty_svc_disc_header_checks LIB mister_svc_disc HEADERS
  svc/disc_identity.h
  svc/msf.h
  svc/chd_backing.h
  svc/chd_geometry.h
  svc/chd_meta_row.h
  svc/chd_prefetch.h
  svc/chd_source.h
  svc/cue_mount.h
  svc/cue_policy.h
  svc/cue_program.h
  svc/disc_ask.h
  svc/disc_backing.h
  svc/disc_counters.h
  svc/disc_engine.h
  svc/disc_geometry.h
  svc/disc_rules.h
  svc/disc_mount_slot.h
  svc/disc_mounter.h
  svc/disc_read_service.h
  svc/disc_read_slot.h
  svc/disc_reader.h
  svc/disc_store.h
  svc/disc_take.h
  svc/file_table_backing.h
  svc/frame_shaper.h
  svc/index_pair_cue_program.h
  svc/index_triple_cue_program.h
  svc/prefetch_main.h
  svc/rw_interleaver.h
  svc/sector_descrambler.h
  svc/sector_header.h
  svc/sector_program.h
  svc/sentinel_cue_program.h
  svc/subcode_sidecar.h
  svc/toc.h
  svc/toc_frame.h
  svc/track_row.h
  svc/track.h
  svc/unbound_disc.h
  svc/seek_span.h
)
tasty_add_header_checks(tasty_svc_video_header_checks LIB mister_svc_video HEADERS
  svc/filter_phase.h
  svc/pll_payload.h
  svc/res_word.h
  svc/modeline_item.h
  svc/adv7513_breaker.h
  svc/adv7513_bus.h
  svc/adv7513_init_options.h
  svc/adv7513_io.h
  svc/adv7513_linux_adapter.h
  svc/adv7513_probe_report.h
  svc/adv7513_reg_map.h
  svc/adv7513_reg_write.h
  svc/adv7513_spd_packet.h
  svc/adv7513_sub_map.h
  svc/dv1_spd.h
  svc/probe_ops.h
  svc/cec_state_machine.h
  svc/cvt.h
  svc/edid_store.h
  svc/filter_bank.h
  svc/filter_bank_index.h
  svc/filter_resend.h
  svc/filter_send_plan.h
  svc/filter_set.h
  svc/filter_store.h
  svc/scaler_slot_mode.h
  svc/geometry_gate.h
  svc/hdmi_link.h
  svc/i2c_adapter.h
  svc/resolution.h
  svc/axis_timing.h
  svc/modeline.h
  svc/pll_block.h
  svc/pll_params.h
  svc/pll_solver.h
  svc/scaling_policy.h
  svc/scaling_words.h
  svc/filter_format.h
  svc/video_sample.h
  svc/aspect_raw.h
  svc/fb_par_crc.h
  svc/video_service.h
  svc/video_timing.h
  svc/parsed_kind.h
  svc/parsed_mode.h
  svc/preset_pick.h
  svc/stored_mode.h
  svc/filter_kernel.h
  svc/video_wire_emit.h
  svc/yc_table.h
)

