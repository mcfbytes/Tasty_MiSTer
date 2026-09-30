add_library(mister_services STATIC
  src/adv7513_breaker.cpp
  src/adv7513_bus.cpp
  src/audio_service.cpp
  src/chd_prefetch.cpp
  src/prefetch_main.cpp
  src/config.cpp
  src/disc_engine.cpp
  src/disc_geometry.cpp
  src/disc_read_service.cpp
  src/disc_store.cpp
  src/frame_shapers.cpp
  src/disk_format_row.cpp
  src/disk_image.cpp
  src/file_slot_backend.cpp
  src/filter_store.cpp
  src/gzip_inflate.cpp
  src/input_emitter.cpp
  src/input_rules.cpp
  src/input_service.cpp
  src/io_main.cpp
  src/raw_disk_image.cpp
  src/storage_service.cpp
  src/toc_frame.cpp
  src/read_bounded.cpp
  src/vfs.cpp
  src/video_service.cpp
  src/xml_scan.cpp
)
target_include_directories(mister_services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_services
  PUBLIC mister_proto
  PRIVATE tasty_options)

find_package(ZLIB REQUIRED)
target_link_libraries(mister_services PRIVATE ZLIB::ZLIB)

if(TASTY_WITH_LIBCHDR)
  if(NOT TASTY_LIBCHDR_INCLUDE_DIR OR NOT TASTY_LIBCHDR_LIBRARY)
    message(FATAL_ERROR
      "TASTY_WITH_LIBCHDR=ON but libchdr was not found "
      "(include=${TASTY_LIBCHDR_INCLUDE_DIR} lib=${TASTY_LIBCHDR_LIBRARY}). "
      "The x86-64 host has none; use the arm-buildroot toolchain, whose sysroot "
      "ships libchdr.so and include/libchdr/chd.h.")
  endif()
  target_include_directories(mister_services SYSTEM PRIVATE ${TASTY_LIBCHDR_INCLUDE_DIR})
  target_link_libraries(mister_services PUBLIC ${TASTY_LIBCHDR_LIBRARY})
  target_compile_definitions(mister_services PRIVATE TASTY_HAVE_LIBCHDR=1)
endif()

if(TASTY_WITH_MINIZIP)
  if(NOT TASTY_MINIZIP_INCLUDE_DIR OR NOT TASTY_MINIZIP_LIBRARY)
    message(FATAL_ERROR
      "TASTY_WITH_MINIZIP=ON but libminizip was not found "
      "(include=${TASTY_MINIZIP_INCLUDE_DIR} lib=${TASTY_MINIZIP_LIBRARY}). "
      "The x86-64 host ships the armhf -dev package's headers under "
      "/usr/include/minizip but no x86-64 library; use the arm-buildroot "
      "toolchain, whose sysroot ships libminizip.so and include/minizip/unzip.h.")
  endif()
  target_include_directories(mister_services SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})

  target_link_libraries(mister_services PUBLIC ${TASTY_MINIZIP_LIBRARY})
  target_compile_definitions(mister_services PRIVATE TASTY_HAVE_MINIZIP=1)
endif()

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
  svc/button_map.h
  svc/cec_state_machine.h
  svc/chd_source.h
  svc/config.h
  svc/config_parser.h
  svc/config_snapshot.h
  svc/cue_policy.h
  svc/deadzone_rule.h
  svc/device_identity.h
  svc/device_match.h
  svc/device_report.h
  svc/dir_entry.h
  svc/disc_counters.h
  svc/disc_engine.h
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
  svc/filter_bank.h
  svc/filter_resend.h
  svc/filter_send_plan.h
  svc/filter_set.h
  svc/filter_store.h
  svc/frame_shaper.h
  svc/geometry_gate.h
  svc/gzip_inflate.h
  svc/i2c_adapter.h
  svc/image_opener.h
  svc/input_device.h
  svc/input_emitter.h
  svc/input_service.h
  svc/key_edge.h
  svc/map_store.h
  svc/mapping_wizard.h
  svc/modeline.h
  svc/movable_counter.h
  svc/no_merge_rule.h
  svc/per_core_setting.h
  svc/player_slots.h
  svc/pll_params.h
  svc/pll_solver.h
  svc/raw_disk_image.h
  svc/scaling_policy.h
  svc/scaling_words.h
  svc/rw_interleaver.h
  svc/scan_filter.h
  svc/sector_descrambler.h
  svc/pending_writes.h
  svc/read_bounded.h
  svc/search_policy.h
  svc/io_coworker.h
  svc/io_main.h
  svc/file_slot_backend.h
  svc/slot_path.h
  svc/storage_backend.h
  svc/storage_lifecycle.h
  svc/storage_service.h
  svc/toc.h
  svc/toc_frame.h
  svc/track.h
  svc/types.h
  svc/vfs.h
  svc/video_sample.h
  svc/video_service.h
  svc/xml_scan.h
)

if(TASTY_BUILD_TESTS)
  foreach(tasty_mod IN ITEMS audio config decomp disc disc_read_service disk_image filter gzip_inflate input integration io_main storage toc_frame vfs video xml_scan)
    set(tasty_src "tests/${tasty_mod}_unit.cpp")
    set(tasty_test "tasty.svc_${tasty_mod}")
    if(tasty_mod STREQUAL "integration")

      set(tasty_src "tests/l2_integration.cpp")
      set(tasty_test "tasty.l2_integration")
    endif()
    add_executable(tasty_svc_${tasty_mod}_unit ${tasty_src})
    target_link_libraries(tasty_svc_${tasty_mod}_unit PRIVATE mister_services tasty_test_options)

    target_include_directories(tasty_svc_${tasty_mod}_unit PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
    target_compile_definitions(tasty_svc_${tasty_mod}_unit PRIVATE
      TASTY_CORPORA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../harness/corpora"

      TASTY_CAPTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../harness/capture/captures"

      TASTY_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/goldens")
    add_test(NAME ${tasty_test} COMMAND tasty_svc_${tasty_mod}_unit)
    set_tests_properties(${tasty_test} PROPERTIES TIMEOUT 60)
  endforeach()

  target_link_libraries(tasty_svc_gzip_inflate_unit PRIVATE ZLIB::ZLIB)

  if(TASTY_WITH_LIBCHDR)
    target_compile_definitions(tasty_svc_decomp_unit PRIVATE TASTY_HAVE_LIBCHDR=1)
  endif()
  if(TASTY_WITH_MINIZIP)
    target_compile_definitions(tasty_svc_decomp_unit PRIVATE TASTY_HAVE_MINIZIP=1)
    target_include_directories(tasty_svc_decomp_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})

    target_compile_definitions(tasty_svc_vfs_unit PRIVATE TASTY_HAVE_MINIZIP=1)
  endif()
endif()
