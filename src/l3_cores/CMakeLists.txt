
set(TASTY_CORES_TASTY
  src/md5.cpp
  src/boot_ladder.cpp
  src/payload_pieces.cpp
  src/psx_ladder.cpp
  src/core_profile.cpp
  src/fio_cheat_sink.cpp
  src/file_tx.cpp
  src/stream_load_bracket.cpp
  src/generic_core.cpp
  src/core_support.cpp
  src/mdp_cue.cpp
  src/mdp_source.cpp
  src/megadrive_core.cpp
  src/nvram_store.cpp
  src/wav_data.cpp
  src/psx_core.cpp
  src/registry.cpp
  src/snes_core.cpp
  src/snes_rom.cpp
  src/msu_machine.cpp
  src/msu_companion.cpp
  src/msu_data_loader.cpp
  src/mailbox_servants.cpp
  src/movie_archive.cpp
  src/movie_codec.cpp
  src/nes_movie_codec.cpp
  src/nes_bk2_codec.cpp
  src/rom_digest.cpp
  src/ram_image.cpp
  src/snes_movie_codec.cpp
  src/snes_lsmv_codec.cpp
  src/snes_bk2_codec.cpp
  src/megadrive_movie_codec.cpp
  src/psx_movie_codec.cpp
  src/manifests/megadrive.cpp
  src/manifests/psx.cpp
  src/manifests/snes.cpp
)
add_library(mister_cores STATIC ${TASTY_CORES_TASTY})
target_include_directories(mister_cores PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_cores
  PUBLIC mister_services
  PRIVATE tasty_options)

target_compile_definitions(mister_cores PRIVATE TASTY_VDATE="${TASTY_VDATE}")

option(TASTY_CDDA_DUMP "Compile the CD-DA capture instrument (NOT an RT-lawful build)" OFF)
if(TASTY_CDDA_DUMP)
  target_compile_definitions(mister_cores PRIVATE TASTY_CDDA_DUMP_ENABLED=1)
endif()

tasty_add_header_checks(tasty_l3_header_checks LIB mister_cores HEADERS
  cores/acsi_dma.h
  cores/acsi_engine.h
  cores/arcade_core.h
  cores/manifest_doc_role.h
  cores/assembled_rom.h
  cores/blank_region.h
  cores/blank_save_spec.h
  cores/boot_asset.h
  cores/boot_ladder.h
  cores/button_override.h
  cores/cd_census_rows.h
  cores/cd_core.h
  cores/cd_diagnostics.h
  cores/cd_drive.h
  cores/cd_engine.h
  cores/cd_engine_stats.h
  cores/cd_flow_query.h
  cores/cd_head_prime.h
  cores/cd_io_indices.h
  cores/cd_profile.h
  cores/cd_service_rows.h
  cores/cd_tick_periods.h
  cores/cd_transport.h
  cores/ladder_context.h
  cores/ladder_host.h
  cores/mount_status.h
  cores/save_extent.h
  cores/stream_load.h
  cores/stream_load_bracket.h
  cores/stream_load_end.h
  cores/load_ask.h
  cores/load_plan.h
  cores/loader.h
  cores/loader_context.h
  cores/transfer_row.h
  cores/megacd_ladder.h
  cores/pcecd_ladder.h
  cores/psx_ladder.h
  cores/threedo_ladder.h
  cores/saturn_ladder.h
  cores/neogeocd_ladder.h
  cores/cdi_ladder.h
  cores/cdwire.h
  cores/cheat_geometry.h
  cores/cheat_lookup.h
  cores/cheat_records.h
  cores/cheat_sink.h
  cores/config_blob.h
  cores/config_fit.h
  cores/config_slots.h
  cores/core_init_context.h
  cores/core_init_host.h
  cores/core_init_step.h
  cores/core_profile.h
  cores/core_window.h
  cores/core_support.h
  cores/dip_row.h
  cores/dip_row_view.h
  cores/dip_switches.h
  cores/electron_core.h
  cores/file_slot.h
  cores/fio_cheat_sink.h
  cores/generic_core.h
  cores/init_step.h
  cores/stock_scan.h
  cores/host_file_sandbox.h
  cores/host_file_server.h
  cores/host_services.h
  cores/core_grant.h
  cores/core_manifest.h
  cores/mailbox_window.h
  cores/mdp_cue.h
  cores/mdp_source.h
  cores/megadrive_core.h
  cores/md5.h
  cores/minimig_core.h
  cores/mounted_path.h
  cores/mra_cheat.h
  cores/mra_doc.h
  cores/cheat_engine.h
  cores/cheat_engine_row.h
  cores/n64_analog.h
  cores/n64_cheat_code.h
  cores/n64_cheat_engine.h
  cores/n64_core.h
  cores/n64_file_route.h
  cores/n64_disk_facts.h
  cores/n64_load_facts.h
  cores/n64_loader.h
  cores/n64_cpak.h
  cores/n64_db.h
  cores/n64_db_tags.h
  cores/n64_ipl3.h
  cores/n64_ipl3_row.h
  cores/n64_options.h
  cores/n64_patch_table.h
  cores/n64_rom.h
  cores/n64_save_binding.h
  cores/n64_save_layout.h
  cores/n64_save_port.h
  cores/n64_save_store.h
  cores/n64_save_type.h
  cores/n64_saves.h
  cores/n64_path.h
  cores/n64dd_disk_map.h
  cores/n64dd_expander.h
  cores/n64dd_geometry.h
  cores/n64dd_ram_save.h
  cores/n64dd_system_area.h
  cores/loader_memo.h
  cores/window_job.h
  cores/neo_file.h
  cores/neogeo_alias_source.h
  cores/neogeo_loader.h
  cores/neogeo_core.h
  cores/neo_transform.h
  cores/nvram_store.h
  cores/pce_scsi_engine.h
  cores/pcm_census_rows.h
  cores/pcm_extent.h
  cores/post_notify.h
  cores/pcm_feed.h
  cores/pcm_feed_state.h
  cores/pcm_source.h
  cores/pcm_wire_rows.h
  cores/psx_core.h
  cores/cdi_core.h
  cores/cdi_block.h
  cores/rom.h
  cores/saturn_cue.h
  cores/saturn_cdd_engine.h
  cores/cd_quirk_row.h
  cores/saturn_ring.h
  cores/saturn_seek.h
  cores/save_channel.h
  cores/save_partition.h
  cores/rom_node.h
  cores/rom_set_assembler.h
  cores/romset_alias.h
  cores/romset_alias_source.h
  cores/romset_files.h
  cores/romsets.h
  cores/registry.h
  cores/save_upload.h
  cores/sector_kick.h
  cores/sector_kick_row.h
  cores/sega_cdd_engine.h
  cores/signature_row.h
  cores/snes_core.h
  cores/snes_rom.h
  cores/msu_wire.h
  cores/msu_machine.h
  cores/msu_companion.h
  cores/msu_data_loader.h
  cores/mailbox_port.h
  cores/mailbox_rows.h
  cores/mailbox_census_rows.h
  cores/mailbox_servant.h
  cores/mailbox_servants.h
  cores/companion_ask.h
  cores/companion_plan.h
  cores/companion_load.h
  cores/movie_archive.h
  cores/movie_codec.h
  cores/movie_system.h
  cores/nes_movie_codec.h
  cores/nes_bk2_codec.h
  cores/digest_value.h
  cores/rom_digest.h
  cores/ram_image.h
  cores/snes_movie_codec.h
  cores/snes_lsmv_codec.h
  cores/snes_bk2_codec.h
  cores/megadrive_movie_codec.h
  cores/psx_movie_codec.h
  cores/st_config_layout.h
  cores/st_core.h
  cores/st_sysctrl.h
  cores/stage_plan.h
  cores/stwire.h
  cores/staging_core.h
  cores/staging_policy.h
  cores/switch_table.h
  cores/threedo_cdd_engine.h
  cores/types.h
  cores/uef.h
  cores/wav_data.h
  cores/manifests/electron.h
  cores/manifests/megacd.h
  cores/manifests/megadrive.h
  cores/manifests/n64.h
  cores/manifests/neogeo.h
  cores/manifests/neogeocd_fragment.h
  cores/manifests/pce.h
  cores/manifests/psx.h
  cores/manifests/snes.h
  cores/manifests/st.h
  cores/manifests/threedo.h
  cores/manifests/saturn.h
  cores/manifests/cdi.h
)

if(TASTY_BUILD_TESTS)
  tasty_unit_test(tasty_cores_nes_movie_codec_unit
    NAME tasty.cores_nes_movie_codec
    SOURCES tests/nes_movie_codec_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_nes_bk2_codec_unit
    NAME tasty.cores_nes_bk2_codec
    SOURCES tests/nes_bk2_codec_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)
  target_include_directories(tasty_cores_nes_bk2_codec_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})

  tasty_unit_test(tasty_cores_snes_movie_codec_unit
    NAME tasty.cores_snes_movie_codec
    SOURCES tests/snes_movie_codec_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_ram_image_unit
    NAME tasty.cores_ram_image
    SOURCES tests/ram_image_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_megadrive_movie_codec_unit
    NAME tasty.cores_megadrive_movie_codec
    SOURCES tests/megadrive_movie_codec_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_psx_movie_codec_unit
    NAME tasty.cores_psx_movie_codec
    SOURCES tests/psx_movie_codec_unit.cpp
    DEFINES TASTY_PSX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/psx"
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_movie_archive_unit
    NAME tasty.cores_movie_archive
    SOURCES tests/movie_archive_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 300)
  target_include_directories(tasty_cores_movie_archive_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})

  foreach(mod IN ITEMS arcade cd cdi pce megacd neogeo psx saturn st threedo)

    tasty_unit_test(tasty_cores_${mod}_unit
      NAME tasty.cores_${mod}
      SOURCES tests/${mod}_core_unit.cpp
      DEFINES TASTY_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/goldens"
      LIBS mister_cores tasty_test_options
      TIMEOUT 60)
  endforeach()

  tasty_unit_test(tasty_cores_threedo_cue_unit
    NAME tasty.cores_threedo_cue
    SOURCES tests/threedo_cue_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_cheat_sink_unit
    NAME tasty.cores_cheat_sink
    SOURCES tests/cheat_sink_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_n64_rom_unit
    NAME tasty.cores_n64_rom
    SOURCES tests/n64_rom_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_n64_db_unit
    NAME tasty.cores_n64_db
    SOURCES tests/n64_db_unit.cpp
    DEFINES TASTY_N64_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/n64"
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_n64_unit
    NAME tasty.cores_n64
    SOURCES tests/n64_unit.cpp
    DEFINES TASTY_CONFSTR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/confstr"
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_n64dd_unit
    NAME tasty.cores_n64dd
    SOURCES tests/n64dd_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 120)

  tasty_unit_test(tasty_cores_n64_cheat_unit
    NAME tasty.cores_n64_cheat
    SOURCES tests/n64_cheat_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_n64_saves_unit
    NAME tasty.cores_n64_saves
    SOURCES tests/n64_saves_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_n64_cpak_unit
    NAME tasty.cores_n64_cpak
    SOURCES tests/n64_cpak_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_nvram_store_unit
    NAME tasty.cores_nvram_store
    SOURCES tests/nvram_store_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_boot_ladder_unit
    NAME tasty.cores_boot_ladder
    SOURCES tests/boot_ladder_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_image_sink_unit
    NAME tasty.cores_image_sink
    SOURCES tests/image_sink_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_host_file_server_unit
    NAME tasty.cores_host_file_server
    SOURCES tests/host_file_server_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_mra_doc_unit
    NAME tasty.cores_mra_doc
    SOURCES tests/mra_doc_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_stock_scan_unit
    NAME tasty.cores_stock_scan
    SOURCES tests/stock_scan_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_romsets_unit
    NAME tasty.cores_romsets
    SOURCES tests/romsets_unit.cpp
    DEFINES TASTY_NEOGEO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/neogeo"
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_neogeo_manifest_unit
    NAME tasty.cores_neogeo_manifest
    SOURCES tests/neogeo_manifest_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_neogeo_alias_source_unit
    NAME tasty.cores_neogeo_alias_source
    SOURCES tests/neogeo_alias_source_unit.cpp
    DEFINES TASTY_NEOGEO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/neogeo"
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_neo_file_unit
    NAME tasty.cores_neo_file
    SOURCES tests/neo_file_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_neo_transform_unit
    NAME tasty.cores_neo_transform
    SOURCES tests/neo_transform_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_neogeo_loader_unit
    NAME tasty.cores_neogeo_loader
    SOURCES tests/neogeo_loader_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_rom_assembly_unit
    NAME tasty.cores_rom_assembly
    SOURCES tests/rom_assembly_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_config_blob_unit
    NAME tasty.cores_config_blob
    SOURCES tests/config_blob_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_config_slots_unit
    NAME tasty.cores_config_slots
    SOURCES tests/config_slots_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_option_rows_unit
    NAME tasty.cores_option_rows
    SOURCES tests/option_rows_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_snes_rom_unit
    NAME tasty.cores_snes_rom
    SOURCES tests/snes_rom_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_snes_msu_unit
    NAME tasty.cores_snes_msu
    SOURCES tests/snes_msu_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_snes_core_unit
    NAME tasty.cores_snes_core
    SOURCES tests/snes_core_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_uef_unit
    NAME tasty.cores_uef
    SOURCES tests/uef_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_megadrive_unit
    NAME tasty.cores_megadrive
    SOURCES tests/megadrive_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_mdp_cue_unit
    NAME tasty.cores_mdp_cue
    SOURCES tests/mdp_cue_unit.cpp
    LIBS mister_cores tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_cores_electron_core_unit
    NAME tasty.cores_electron_core
    SOURCES tests/electron_core_unit.cpp
    LIBS mister_cores tasty_test_options ZLIB::ZLIB
    TIMEOUT 60)
  find_package(ZLIB REQUIRED)

  get_filename_component(TASTY_L3_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  target_link_libraries(tasty_cores_megacd_unit PRIVATE trace_replay)
  target_compile_definitions(tasty_cores_megacd_unit PRIVATE
    TASTY_CAPTURE_DIR="${TASTY_L3_REPO_ROOT}/harness/capture/captures")
  set_tests_properties(tasty.cores_megacd PROPERTIES TIMEOUT 900)
endif()
