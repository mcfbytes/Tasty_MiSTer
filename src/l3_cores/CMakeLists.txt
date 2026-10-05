
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
  add_executable(tasty_cores_nes_movie_codec_unit tests/nes_movie_codec_unit.cpp)
  target_link_libraries(tasty_cores_nes_movie_codec_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_nes_movie_codec COMMAND tasty_cores_nes_movie_codec_unit)
  set_tests_properties(tasty.cores_nes_movie_codec PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_nes_bk2_codec_unit tests/nes_bk2_codec_unit.cpp)
  target_link_libraries(tasty_cores_nes_bk2_codec_unit PRIVATE mister_cores tasty_test_options)
  target_include_directories(tasty_cores_nes_bk2_codec_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})
  add_test(NAME tasty.cores_nes_bk2_codec COMMAND tasty_cores_nes_bk2_codec_unit)
  set_tests_properties(tasty.cores_nes_bk2_codec PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_snes_movie_codec_unit tests/snes_movie_codec_unit.cpp)
  target_link_libraries(tasty_cores_snes_movie_codec_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_snes_movie_codec COMMAND tasty_cores_snes_movie_codec_unit)
  set_tests_properties(tasty.cores_snes_movie_codec PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_ram_image_unit tests/ram_image_unit.cpp)
  target_link_libraries(tasty_cores_ram_image_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_ram_image COMMAND tasty_cores_ram_image_unit)
  set_tests_properties(tasty.cores_ram_image PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_megadrive_movie_codec_unit tests/megadrive_movie_codec_unit.cpp)
  target_link_libraries(tasty_cores_megadrive_movie_codec_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_megadrive_movie_codec COMMAND tasty_cores_megadrive_movie_codec_unit)
  set_tests_properties(tasty.cores_megadrive_movie_codec PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_psx_movie_codec_unit tests/psx_movie_codec_unit.cpp)
  target_link_libraries(tasty_cores_psx_movie_codec_unit PRIVATE mister_cores tasty_test_options)
  target_compile_definitions(tasty_cores_psx_movie_codec_unit PRIVATE
    TASTY_PSX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/psx")
  add_test(NAME tasty.cores_psx_movie_codec COMMAND tasty_cores_psx_movie_codec_unit)
  set_tests_properties(tasty.cores_psx_movie_codec PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_movie_archive_unit tests/movie_archive_unit.cpp)
  target_link_libraries(tasty_cores_movie_archive_unit PRIVATE mister_cores tasty_test_options)
  target_include_directories(tasty_cores_movie_archive_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})
  add_test(NAME tasty.cores_movie_archive COMMAND tasty_cores_movie_archive_unit)
  set_tests_properties(tasty.cores_movie_archive PROPERTIES TIMEOUT 300)

  foreach(mod IN ITEMS arcade cd cdi pce megacd neogeo psx saturn st threedo)
    add_executable(tasty_cores_${mod}_unit tests/${mod}_core_unit.cpp)
    target_link_libraries(tasty_cores_${mod}_unit PRIVATE mister_cores tasty_test_options)

    target_compile_definitions(tasty_cores_${mod}_unit PRIVATE
      TASTY_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/goldens")
    add_test(NAME tasty.cores_${mod} COMMAND tasty_cores_${mod}_unit)
    set_tests_properties(tasty.cores_${mod} PROPERTIES TIMEOUT 60)
  endforeach()

  add_executable(tasty_cores_threedo_cue_unit tests/threedo_cue_unit.cpp)
  target_link_libraries(tasty_cores_threedo_cue_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_threedo_cue COMMAND tasty_cores_threedo_cue_unit)
  set_tests_properties(tasty.cores_threedo_cue PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_cheat_sink_unit tests/cheat_sink_unit.cpp)
  target_link_libraries(tasty_cores_cheat_sink_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_cheat_sink COMMAND tasty_cores_cheat_sink_unit)
  set_tests_properties(tasty.cores_cheat_sink PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_n64_rom_unit tests/n64_rom_unit.cpp)
  target_link_libraries(tasty_cores_n64_rom_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_n64_rom COMMAND tasty_cores_n64_rom_unit)
  set_tests_properties(tasty.cores_n64_rom PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_n64_db_unit tests/n64_db_unit.cpp)
  target_compile_definitions(tasty_cores_n64_db_unit PRIVATE
    TASTY_N64_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/n64")
  target_link_libraries(tasty_cores_n64_db_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_n64_db COMMAND tasty_cores_n64_db_unit)
  set_tests_properties(tasty.cores_n64_db PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_n64_unit tests/n64_unit.cpp)
  target_compile_definitions(tasty_cores_n64_unit PRIVATE
    TASTY_CONFSTR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/confstr")
  target_link_libraries(tasty_cores_n64_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_n64 COMMAND tasty_cores_n64_unit)
  set_tests_properties(tasty.cores_n64 PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_n64dd_unit tests/n64dd_unit.cpp)
  target_link_libraries(tasty_cores_n64dd_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_n64dd COMMAND tasty_cores_n64dd_unit)
  set_tests_properties(tasty.cores_n64dd PROPERTIES TIMEOUT 120)

  add_executable(tasty_cores_n64_cheat_unit tests/n64_cheat_unit.cpp)
  target_link_libraries(tasty_cores_n64_cheat_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_n64_cheat COMMAND tasty_cores_n64_cheat_unit)
  set_tests_properties(tasty.cores_n64_cheat PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_n64_saves_unit tests/n64_saves_unit.cpp)
  target_link_libraries(tasty_cores_n64_saves_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_n64_saves COMMAND tasty_cores_n64_saves_unit)
  set_tests_properties(tasty.cores_n64_saves PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_n64_cpak_unit tests/n64_cpak_unit.cpp)
  target_link_libraries(tasty_cores_n64_cpak_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_n64_cpak COMMAND tasty_cores_n64_cpak_unit)
  set_tests_properties(tasty.cores_n64_cpak PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_nvram_store_unit tests/nvram_store_unit.cpp)
  target_link_libraries(tasty_cores_nvram_store_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_nvram_store COMMAND tasty_cores_nvram_store_unit)
  set_tests_properties(tasty.cores_nvram_store PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_boot_ladder_unit tests/boot_ladder_unit.cpp)
  target_link_libraries(tasty_cores_boot_ladder_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_boot_ladder COMMAND tasty_cores_boot_ladder_unit)
  set_tests_properties(tasty.cores_boot_ladder PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_image_sink_unit tests/image_sink_unit.cpp)
  target_link_libraries(tasty_cores_image_sink_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_image_sink COMMAND tasty_cores_image_sink_unit)
  set_tests_properties(tasty.cores_image_sink PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_host_file_server_unit tests/host_file_server_unit.cpp)
  target_link_libraries(tasty_cores_host_file_server_unit
    PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_host_file_server
           COMMAND tasty_cores_host_file_server_unit)
  set_tests_properties(tasty.cores_host_file_server PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_mra_doc_unit tests/mra_doc_unit.cpp)
  target_link_libraries(tasty_cores_mra_doc_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_mra_doc COMMAND tasty_cores_mra_doc_unit)
  set_tests_properties(tasty.cores_mra_doc PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_stock_scan_unit tests/stock_scan_unit.cpp)
  target_link_libraries(tasty_cores_stock_scan_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_stock_scan COMMAND tasty_cores_stock_scan_unit)
  set_tests_properties(tasty.cores_stock_scan PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_romsets_unit tests/romsets_unit.cpp)
  target_link_libraries(tasty_cores_romsets_unit PRIVATE mister_cores tasty_test_options)
  target_compile_definitions(tasty_cores_romsets_unit PRIVATE
    TASTY_NEOGEO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/neogeo")
  add_test(NAME tasty.cores_romsets COMMAND tasty_cores_romsets_unit)
  set_tests_properties(tasty.cores_romsets PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_neogeo_manifest_unit tests/neogeo_manifest_unit.cpp)
  target_link_libraries(tasty_cores_neogeo_manifest_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_neogeo_manifest COMMAND tasty_cores_neogeo_manifest_unit)
  set_tests_properties(tasty.cores_neogeo_manifest PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_neogeo_alias_source_unit tests/neogeo_alias_source_unit.cpp)
  target_link_libraries(tasty_cores_neogeo_alias_source_unit PRIVATE mister_cores tasty_test_options)
  target_compile_definitions(tasty_cores_neogeo_alias_source_unit PRIVATE
    TASTY_NEOGEO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/neogeo")
  add_test(NAME tasty.cores_neogeo_alias_source COMMAND tasty_cores_neogeo_alias_source_unit)
  set_tests_properties(tasty.cores_neogeo_alias_source PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_neo_file_unit tests/neo_file_unit.cpp)
  target_link_libraries(tasty_cores_neo_file_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_neo_file COMMAND tasty_cores_neo_file_unit)
  set_tests_properties(tasty.cores_neo_file PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_neo_transform_unit tests/neo_transform_unit.cpp)
  target_link_libraries(tasty_cores_neo_transform_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_neo_transform COMMAND tasty_cores_neo_transform_unit)
  set_tests_properties(tasty.cores_neo_transform PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_neogeo_loader_unit tests/neogeo_loader_unit.cpp)
  target_link_libraries(tasty_cores_neogeo_loader_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_neogeo_loader COMMAND tasty_cores_neogeo_loader_unit)
  set_tests_properties(tasty.cores_neogeo_loader PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_rom_assembly_unit tests/rom_assembly_unit.cpp)
  target_link_libraries(tasty_cores_rom_assembly_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_rom_assembly COMMAND tasty_cores_rom_assembly_unit)
  set_tests_properties(tasty.cores_rom_assembly PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_config_blob_unit tests/config_blob_unit.cpp)
  target_link_libraries(tasty_cores_config_blob_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_config_blob COMMAND tasty_cores_config_blob_unit)
  set_tests_properties(tasty.cores_config_blob PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_config_slots_unit tests/config_slots_unit.cpp)
  target_link_libraries(tasty_cores_config_slots_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_config_slots COMMAND tasty_cores_config_slots_unit)
  set_tests_properties(tasty.cores_config_slots PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_option_rows_unit tests/option_rows_unit.cpp)
  target_link_libraries(tasty_cores_option_rows_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_option_rows COMMAND tasty_cores_option_rows_unit)
  set_tests_properties(tasty.cores_option_rows PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_snes_rom_unit tests/snes_rom_unit.cpp)
  target_link_libraries(tasty_cores_snes_rom_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_snes_rom COMMAND tasty_cores_snes_rom_unit)
  set_tests_properties(tasty.cores_snes_rom PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_snes_msu_unit tests/snes_msu_unit.cpp)
  target_link_libraries(tasty_cores_snes_msu_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_snes_msu COMMAND tasty_cores_snes_msu_unit)
  set_tests_properties(tasty.cores_snes_msu PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_snes_core_unit tests/snes_core_unit.cpp)
  target_link_libraries(tasty_cores_snes_core_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_snes_core COMMAND tasty_cores_snes_core_unit)
  set_tests_properties(tasty.cores_snes_core PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_uef_unit tests/uef_unit.cpp)
  target_link_libraries(tasty_cores_uef_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_uef COMMAND tasty_cores_uef_unit)
  set_tests_properties(tasty.cores_uef PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_megadrive_unit tests/megadrive_unit.cpp)
  target_link_libraries(tasty_cores_megadrive_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_megadrive COMMAND tasty_cores_megadrive_unit)
  set_tests_properties(tasty.cores_megadrive PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_mdp_cue_unit tests/mdp_cue_unit.cpp)
  target_link_libraries(tasty_cores_mdp_cue_unit PRIVATE mister_cores tasty_test_options)
  add_test(NAME tasty.cores_mdp_cue COMMAND tasty_cores_mdp_cue_unit)
  set_tests_properties(tasty.cores_mdp_cue PROPERTIES TIMEOUT 60)

  add_executable(tasty_cores_electron_core_unit tests/electron_core_unit.cpp)
  target_link_libraries(tasty_cores_electron_core_unit PRIVATE mister_cores tasty_test_options)

  find_package(ZLIB REQUIRED)
  target_link_libraries(tasty_cores_electron_core_unit PRIVATE ZLIB::ZLIB)
  add_test(NAME tasty.cores_electron_core COMMAND tasty_cores_electron_core_unit)
  set_tests_properties(tasty.cores_electron_core PROPERTIES TIMEOUT 60)

  get_filename_component(TASTY_L3_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  target_link_libraries(tasty_cores_megacd_unit PRIVATE trace_replay)
  target_compile_definitions(tasty_cores_megacd_unit PRIVATE
    TASTY_CAPTURE_DIR="${TASTY_L3_REPO_ROOT}/harness/capture/captures")
  set_tests_properties(tasty.cores_megacd PROPERTIES TIMEOUT 900)
endif()
