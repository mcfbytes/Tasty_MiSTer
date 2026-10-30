add_library(mister_services STATIC
  src/adv7513_breaker.cpp
  src/adv7513_bus.cpp
  src/adv7513_reg_write.cpp
  src/alt_ini_index.cpp
  src/audio_service.cpp
  src/cec_state_machine.cpp
  src/chd_backing.cpp
  src/chd_prefetch.cpp
  src/chd_source.cpp
  src/config.cpp
  src/config_field.cpp
  src/config_parser.cpp
  src/cue_programs.cpp
  src/disc_engine.cpp
  src/disc_file_access.cpp
  src/disc_geometry.cpp
  src/disc_read_service.cpp
  src/disc_store.cpp
  src/disk_format_row.cpp
  src/disk_image.cpp
  src/edid_store.cpp
  src/file_slot_backend.cpp
  src/file_table_backing.cpp
  src/filter_store.cpp
  src/frame_shapers.cpp
  src/gzip_inflate.cpp
  src/hotplug_watch.cpp
  src/input_device.cpp
  src/input_emitter.cpp
  src/input_enumerator.cpp
  src/input_rules.cpp
  src/input_service.cpp
  src/io_main.cpp
  src/map_store.cpp
  src/mapping_wizard.cpp
  src/modeline.cpp
  src/option_codec.cpp
  src/player_slots.cpp
  src/pll_solver.cpp
  src/prefetch_main.cpp
  src/raw_disk_image.cpp
  src/read_bounded.cpp
  src/scaling_words.cpp
  src/storage_service.cpp
  src/subcode_sidecar.cpp
  src/toc_frame.cpp
  src/game_asset_finder.cpp
  src/memory_mounts.cpp
  src/vfs.cpp
  src/hdmi_link.cpp
  src/video_service.cpp
  src/video_timing.cpp
  src/video_wire_emit.cpp
  src/xml_scan.cpp
  src/xml_scanner.cpp
)
target_include_directories(mister_services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_services
  PUBLIC mister_proto
  PRIVATE tasty_options)

find_package(ZLIB REQUIRED)
target_link_libraries(mister_services PRIVATE ZLIB::ZLIB)

if(NOT TASTY_LIBCHDR_INCLUDE_DIR OR NOT TASTY_LIBCHDR_LIBRARY)
  message(FATAL_ERROR
    "libchdr is required but was not found "
    "(include=${TASTY_LIBCHDR_INCLUDE_DIR} lib=${TASTY_LIBCHDR_LIBRARY}). "
    "The x86-64 host has none; use the arm-buildroot toolchain, whose sysroot "
    "ships libchdr.so and include/libchdr/chd.h.")
endif()
target_include_directories(mister_services SYSTEM PRIVATE ${TASTY_LIBCHDR_INCLUDE_DIR})
target_link_libraries(mister_services PUBLIC ${TASTY_LIBCHDR_LIBRARY})

if(NOT TASTY_MINIZIP_INCLUDE_DIR OR NOT TASTY_MINIZIP_LIBRARY)
  message(FATAL_ERROR
    "libminizip is required but was not found "
    "(include=${TASTY_MINIZIP_INCLUDE_DIR} lib=${TASTY_MINIZIP_LIBRARY}). "
    "The x86-64 host ships the armhf -dev package's headers under "
    "/usr/include/minizip but no x86-64 library; use the arm-buildroot "
    "toolchain, whose sysroot ships libminizip.so and include/minizip/unzip.h.")
endif()
target_include_directories(mister_services SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})

target_link_libraries(mister_services PUBLIC ${TASTY_MINIZIP_LIBRARY})

target_sources(mister_services PRIVATE src/vfs_zip.cpp)

tasty_add_header_checks(tasty_l2_header_checks LIB mister_services HEADERS
  svc/adv7513_breaker.h
  svc/adv7513_bus.h
  svc/adv7513_init_options.h
  svc/adv7513_io.h
  svc/adv7513_linux_adapter.h
  svc/adv7513_probe_report.h
  svc/adv7513_reg_map.h
  svc/adv7513_reg_write.h
  svc/adv7513_sub_map.h
  svc/alt_ini_index.h
  svc/analog_reshape.h
  svc/analog_xy.h
  svc/archive.h
  svc/audio_service.h
  svc/axis_cal.h
  svc/block_ctl.h
  svc/block_half.h
  svc/button_map.h
  svc/cec_state_machine.h
  svc/chd_backing.h
  svc/chd_meta_row.h
  svc/chd_prefetch.h
  svc/chd_source.h
  svc/config.h
  svc/config_field.h
  svc/config_parser.h
  svc/config_snapshot.h
  svc/cue_mount.h
  svc/cue_policy.h
  svc/cue_program.h
  svc/deadzone_rule.h
  svc/device_identity.h
  svc/device_match.h
  svc/device_report.h
  svc/dir_entry.h
  svc/disc_backing.h
  svc/disc_counters.h
  svc/disc_engine.h
  svc/disc_file_access.h
  svc/disc_geometry.h
  svc/disc_mount_slot.h
  svc/disc_mounter.h
  svc/disc_read_service.h
  svc/disc_read_slot.h
  svc/disc_reader.h
  svc/disc_store.h
  svc/disk_codec.h
  svc/disk_format_row.h
  svc/disk_image.h
  svc/edid_store.h
  svc/file.h
  svc/file_table_backing.h
  svc/filter_bank.h
  svc/filter_resend.h
  svc/filter_send_plan.h
  svc/filter_set.h
  svc/filter_store.h
  svc/frame_shaper.h
  svc/geometry_gate.h
  svc/hdmi_link.h
  svc/gzip_inflate.h
  svc/i2c_adapter.h
  svc/image_opener.h
  svc/device_table.h
  svc/index_pair_cue_program.h
  svc/index_triple_cue_program.h
  svc/device_facts.h
  svc/hotplug_watch.h
  svc/input_counters.h
  svc/input_device.h
  svc/input_emitter.h
  svc/input_enumerator.h
  svc/input_service.h
  svc/but_sw_wire.h
  svc/rumble_out.h
  svc/joy_plan.h
  svc/key_edge.h
  svc/map_store.h
  svc/mapping_wizard.h
  svc/modeline.h
  svc/no_merge_rule.h
  svc/option_codec.h
  svc/parse_error.h
  svc/per_core_setting.h
  svc/player_slots.h
  svc/quirk_id.h
  svc/pll_params.h
  svc/pll_solver.h
  svc/prefetch_main.h
  svc/raw_disk_image.h
  svc/scaling_policy.h
  svc/scaling_words.h
  svc/rw_interleaver.h
  svc/scan_filter.h
  svc/sector_descrambler.h
  svc/sector_program.h
  svc/pending_writes.h
  svc/read_bounded.h
  svc/search_policy.h
  svc/io_coworker.h
  svc/io_main.h
  svc/file_slot_backend.h
  svc/sentinel_cue_program.h
  svc/slot_path.h
  svc/stick_cal.h
  svc/storage_backend.h
  svc/storage_lifecycle.h
  svc/storage_service.h
  svc/subcode_sidecar.h
  svc/toc.h
  svc/toc_frame.h
  svc/track.h
  svc/types.h
  svc/unbound_disc.h
  svc/memory_mounts.h
  svc/vfs.h
  svc/video_sample.h
  svc/video_service.h
  svc/video_timing.h
  svc/parsed_mode.h
  svc/stored_mode.h
  svc/xml_attr.h
  svc/xml_scan.h
  svc/xml_scanner.h
  svc/xml_tag.h
  svc/xml_token.h
)

