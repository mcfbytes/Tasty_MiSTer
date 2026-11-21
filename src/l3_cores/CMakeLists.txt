add_library(mister_cores_base STATIC
  src/addressed_load.cpp
  src/boot_ladder.cpp
  src/core_profile.cpp
  src/core_support.cpp
  src/disc_save_migration.cpp
  src/fio_cheat_sink.cpp
  src/mailbox_carriage.cpp
  src/mailbox_window.cpp
  src/md5.cpp
  src/nvram_store.cpp
  src/payload_pieces.cpp
  src/stream_load_bracket.cpp
  src/stream_load_host.cpp
)
target_include_directories(mister_cores_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_cores_base
  PUBLIC mister_svc mister_svc_disc mister_proto_link
  PRIVATE tasty_options)

add_library(mister_cores_cd STATIC
  src/cd/cd_cadence_gate.cpp
  src/cd/cd_core.cpp
  src/cd/cd_core_detail.h
  src/cd/cd_disc_slot.cpp
  src/cd/cd_drive.cpp
  src/cd/cd_engine.cpp
  src/cd/cd_wire_port.cpp
  src/cd/cdwire.cpp
  src/cd/megacd_ladder.cpp
  src/cd/pce_command.cpp
  src/cd/pce_dir_info_reply.cpp
  src/cd/pce_scsi_engine.cpp
  src/cd/pce_sense_reply.cpp
  src/cd/pce_subq.cpp
  src/cd/pce_subq_reply.cpp
  src/cd/pcecd_ladder.cpp
  src/cd/saturn_cdd_engine.cpp
  src/cd/saturn_ladder.cpp
  src/cd/saturn_stock_toc.cpp
  src/cd/sega_cdd_engine.cpp
  src/cd/threedo_cdd_engine.cpp
  src/cd/threedo_ladder.cpp
)
target_link_libraries(mister_cores_cd
  PUBLIC mister_cores_base mister_svc_disc
  PRIVATE tasty_options)

add_library(mister_cores_n64dd STATIC
  src/n64dd/n64dd_disk_map.cpp
  src/n64dd/n64dd_expander.cpp
  src/n64dd/n64dd_ram_save.cpp
  src/n64dd/n64dd_system_area.cpp
)
target_link_libraries(mister_cores_n64dd
  PUBLIC mister_cores_base
  PRIVATE tasty_options)

add_library(mister_cores_n64 STATIC
  src/n64/n64_cheat_engine.cpp
  src/n64/n64_core.cpp
  src/n64/n64_cpak.cpp
  src/n64/n64_db_reader.cpp
  src/n64/n64_detect.cpp
  src/n64/n64_load_facts.cpp
  src/n64/n64_loader.cpp
  src/n64/n64_rom_pass.cpp
  src/n64/n64_ipl_pass.cpp
  src/n64/n64_disk_pass.cpp
  src/n64/n64_save_store.cpp
  src/n64/n64_saves.cpp
  src/n64/word_reverse.cpp
)
target_link_libraries(mister_cores_n64
  PUBLIC mister_cores_n64dd mister_cores_base mister_svc_input
  PRIVATE tasty_options)

add_library(mister_cores_mra STATIC
  src/mra/arcade_core.cpp
  src/mra/arcade_facts.cpp
  src/mra/arcade_manifest_role.cpp
  src/mra/mra_doc.cpp
  src/mra/mra_stager.cpp
  src/mra/rom_bytes.cpp
  src/mra/rom_set_assembler.cpp
  src/mra/rom_stream.cpp
)
target_link_libraries(mister_cores_mra
  PUBLIC mister_cores_base
  PRIVATE tasty_options)

add_library(mister_cores_st STATIC
  src/st/acsi_servant.cpp
  src/st/st_config.cpp
  src/st/st_core.cpp
  src/st/st_loader.cpp
  src/st/st_media_ladder.cpp
  src/st/st_option_row.cpp
)
target_link_libraries(mister_cores_st
  PUBLIC mister_cores_base
  PRIVATE tasty_options)

add_library(mister_cores_neogeo STATIC
  src/neogeo/neo_file.cpp
  src/neogeo/neo_transform.cpp
  src/neogeo/neogeo_alias_source.cpp
  src/neogeo/neogeo_core.cpp
  src/neogeo/neogeo_loader.cpp
  src/neogeo/neogeocd_ladder.cpp
  src/neogeo/romsets.cpp
)
target_link_libraries(mister_cores_neogeo
  PUBLIC mister_cores_cd
  PRIVATE tasty_options)
tasty_add_header_checks(tasty_l3_neogeo_header_checks LIB mister_cores_neogeo HEADERS
  cores/neogeo/fix_shape.h
  cores/neogeo/i_rom_shape.h
  cores/neogeo/neo_header.h
  cores/neogeo/neo_quirk.h
  cores/neogeo/neo_transform.h
  cores/neogeo/neogeo_alias_source.h
  cores/neogeo/neogeo_core.h
  cores/neogeo/neogeo_file_index.h
  cores/neogeo/neogeo_loader.h
  cores/neogeo/neogeo_walk_tally.h
  cores/neogeo/neogeocd_ladder.h
  cores/neogeo/romset_config.h
  cores/neogeo/romset_file.h
  cores/neogeo/romset_index.h
  cores/neogeo/romset_plan.h
  cores/neogeo/spr_dbl_shape.h
  cores/neogeo/spr_dbl_swap_shape.h
  cores/neogeo/spr_shape.h
)

add_library(mister_cores_psx STATIC
  src/psx/psx_core.cpp
  src/psx/psx_disc_rules.cpp
  src/psx/psx_ladder.cpp
  src/psx/psx_sector_map.cpp
  src/psx/psx_sector_program.cpp
)
target_link_libraries(mister_cores_psx
  PUBLIC mister_cores_cd
  PRIVATE tasty_options)
tasty_add_header_checks(tasty_l3_psx_header_checks LIB mister_cores_psx HEADERS
  cores/psx/psx_core.h
  cores/psx/psx_disc_rules.h
  cores/psx/psx_ladder.h
  cores/psx/psx_sector_map.h
  cores/psx/psx_sector_program.h
  cores/psx/psx_staging.h
)

add_library(mister_cores_cdi STATIC
  src/cdi/cdi_block.cpp
  src/cdi/cdi_core.cpp
  src/cdi/cdi_disc_rules.cpp
  src/cdi/cdi_ladder.cpp
)
target_link_libraries(mister_cores_cdi
  PUBLIC mister_cores_cd
  PRIVATE tasty_options)
tasty_add_header_checks(tasty_l3_cdi_header_checks LIB mister_cores_cdi HEADERS
  cores/cdi/cdi_block.h
  cores/cdi/cdi_core.h
  cores/cdi/cdi_disc_rules.h
  cores/cdi/cdi_ladder.h
  cores/cdi/q_control.h
  cores/cdi/toc_entry.h
  cores/cdi/toc_point.h
)

add_library(mister_cores_snes STATIC
  src/snes/snes_core.cpp
  src/snes/snes_loader.cpp
  src/snes/snes_rom.cpp
  src/snes/msu_machine.cpp
  src/snes/sector_read_ahead.cpp
  src/snes/msu_companion.cpp
  src/snes/msu_data_loader.cpp
)
target_link_libraries(mister_cores_snes PUBLIC mister_cores_base PRIVATE tasty_options)
tasty_add_header_checks(tasty_l3_snes_header_checks LIB mister_cores_snes HEADERS
  cores/snes/snes_core.h
  cores/snes/snes_loader.h
  cores/snes/snes_rom.h
  cores/snes/snes_rom_file.h
  cores/snes/snes_header_fields.h
  cores/snes/snes_cart_type.h
  cores/snes/sgb_core.h
  cores/snes/msu_machine.h
  cores/snes/msu_companion.h
  cores/snes/msu_data_loader.h
  cores/snes/msu_wire.h
  cores/snes/snes_msu_decl.h
  cores/snes/sector_read_ahead.h
)

add_library(mister_cores_megadrive STATIC
  src/megadrive/megadrive_core.cpp
  src/megadrive/mdp_cue.cpp
  src/megadrive/mdp_source.cpp
  src/megadrive/wav_data.cpp
)
target_link_libraries(mister_cores_megadrive PUBLIC mister_cores_base PRIVATE tasty_options)
tasty_add_header_checks(tasty_l3_megadrive_header_checks LIB mister_cores_megadrive HEADERS
  cores/megadrive/megadrive_core.h
  cores/megadrive/megadrive_windows.h
  cores/megadrive/md_cmd_flags.h
  cores/megadrive/owed_answer.h
  cores/megadrive/mdp_cue.h
  cores/megadrive/mdp_source.h
  cores/megadrive/wav_data.h
)

add_library(mister_cores_electron STATIC
  src/electron/electron_core.cpp
  src/electron/uef.cpp
  src/electron/uef_loader.cpp
)
target_link_libraries(mister_cores_electron PUBLIC mister_cores_base PRIVATE tasty_options)
tasty_add_header_checks(tasty_l3_electron_header_checks LIB mister_cores_electron HEADERS
  cores/electron/electron_core.h
  cores/electron/uef.h
  cores/electron/uef_loader.h
  cores/electron/uef_tape_file.h
)

add_library(mister_cores_minimig STATIC
  src/minimig/minimig_core.cpp
  src/minimig/host_file_sandbox.cpp
)
target_link_libraries(mister_cores_minimig PUBLIC mister_cores_base PRIVATE tasty_options)
tasty_add_header_checks(tasty_l3_minimig_header_checks LIB mister_cores_minimig HEADERS
  cores/minimig/minimig_core.h
  cores/minimig/host_file_server.h
  cores/minimig/host_file_sandbox.h
)

add_library(mister_cores STATIC
  src/generic_core.cpp
  src/pcm_source_factory.cpp
  src/registry.cpp
  src/servant_factories.cpp
  src/manifests/megadrive.cpp
  src/manifests/psx.cpp
  src/manifests/snes.cpp
  src/manifests/electron.cpp
  src/manifests/megacd.cpp
  src/manifests/n64.cpp
  src/manifests/neogeo.cpp
  src/manifests/pce.cpp
  src/manifests/st.cpp
  src/manifests/threedo.cpp
  src/manifests/saturn.cpp
  src/manifests/cdi.cpp
)
target_link_libraries(mister_cores
  PUBLIC mister_cores_base mister_cores_cd mister_cores_neogeo mister_cores_psx mister_cores_cdi mister_cores_n64dd mister_cores_n64 mister_cores_mra mister_cores_st mister_cores_snes mister_cores_megadrive mister_cores_electron mister_cores_minimig
         mister_svc mister_svc_input mister_svc_disc mister_svc_video mister_proto_link
  PRIVATE tasty_options)

add_library(mister_cores_tas STATIC
  src/tas/movie_archive.cpp
  src/tas/json_flatten.cpp
  src/tas/bk2_log.cpp
  src/tas/bk2_text.cpp
  src/tas/movie_codec.cpp
  src/tas/movie_lines.cpp
  src/tas/movie_rom_match.cpp
  src/tas/movie_system.cpp
  src/tas/nes_movie_codec.cpp
  src/tas/nes_bk2_codec.cpp
  src/tas/rom_digest.cpp
  src/tas/ram_image.cpp
  src/tas/snes_movie_codec.cpp
  src/tas/snes_lsmv_codec.cpp
  src/tas/snes_bk2_codec.cpp
  src/tas/megadrive_movie_codec.cpp
  src/tas/psx_movie_codec.cpp
  src/tas/psx_disc_key.cpp
)
target_link_libraries(mister_cores_tas
  PUBLIC mister_cores
  PRIVATE tasty_options)

target_compile_definitions(mister_cores PRIVATE TASTY_VDATE="${TASTY_VDATE}")

option(TASTY_CDDA_DUMP "Compile the CD-DA capture instrument (NOT an RT-lawful build)" OFF)
if(TASTY_CDDA_DUMP)
  target_compile_definitions(mister_cores_cd PRIVATE TASTY_CDDA_DUMP_ENABLED=1)
endif()

tasty_add_header_checks(tasty_l3_base_header_checks LIB mister_cores_base HEADERS
  cores/scsi_cdb.h
  cores/fnv1a64.h
  cores/addressed_load.h
  cores/disk_port.h
  cores/disk_servant.h
  cores/disk_servant_row.h
  cores/slot_mount.h
  cores/manifest_doc_facts.h
  cores/manifest_doc_role.h
  cores/boot_asset.h
  cores/boot_ladder.h
  cores/button_override.h
  cores/cd_diagnostics.h
  cores/cd_flow.h
  cores/cd_msf.h
  cores/cd_service_rows.h
  cores/ladder_context.h
  cores/ladder_host.h
  cores/mount_status.h
  cores/mount_state.h
  cores/save_extent.h
  cores/save_copy_level.h
  cores/stream_ask.h
  cores/stream_load.h
  cores/stream_load_bracket.h
  cores/stream_load_end.h
  cores/stream_load_host.h
  cores/stream_tx_record.h
  cores/load_ask.h
  cores/load_plan.h
  cores/loader.h
  cores/loader_context.h
  cores/transfer_row.h
  cores/window_spot.h
  cores/cheat_unit.h
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
  cores/dip_row_view.h
  cores/dip_switches.h
  cores/file_slot.h
  cores/fio_cheat_sink.h
  cores/grant_set.h
  cores/init_step.h
  cores/stock_scan.h
  cores/mailbox_req_id.h
  cores/host_services.h
  cores/core_grant.h
  cores/core_manifest.h
  cores/mailbox_window.h
  cores/md5.h
  cores/mounted_path.h
  cores/manifest_act.h
  cores/manifest_counts.h
  cores/send_bracket.h
  cores/stage_window.h
  cores/manifest_stager.h
  cores/cheat_engine.h
  cores/cheat_engine_row.h
  cores/save_row_count.h
  cores/save_probe_rows.h
  cores/loader_memo.h
  cores/window_job.h
  cores/window_job_order.h
  cores/job_arena.h
  cores/nvram_store.h
  cores/disc_save_migration.h
  cores/scsi_sense.h
  cores/pcm_census_rows.h
  cores/pcm_extent.h
  cores/post_notify.h
  cores/pcm_feed.h
  cores/pcm_feed_state.h
  cores/pcm_handovers.h
  cores/pcm_ring.h
  cores/pcm_source.h
  cores/pcm_wire_rows.h
  cores/save_channel.h
  cores/save_partition.h
  cores/browse_dirs.h
  cores/entry_lookup.h
  cores/romset_alias.h
  cores/romset_drop.h
  cores/romset_no_match.h
  cores/romset_retype.h
  cores/romset_files.h
  cores/save_servant.h
  cores/save_upload.h
  cores/sector_kick.h
  cores/sector_kick_row.h
  cores/signature_row.h
  cores/mailbox_carriage.h
  cores/mailbox_port.h
  cores/mailbox_rows.h
  cores/mailbox_census_rows.h
  cores/mailbox_servant.h
  cores/mailbox_servant_row.h
  cores/servant_grant.h
  cores/servant_id.h
  cores/companion_ask.h
  cores/companion_plan.h
  cores/companion_load.h
  cores/stage_plan.h
  cores/staging_core.h
  cores/staging_policy.h
  cores/types.h
  cores/progress.h
  cores/role_row.h
  cores/baud_halves.h
  cores/cd_drive_state.h
  cores/companion.h
  cores/config_codec.h
  cores/config_slot_count.h
  cores/core_adopt.h
  cores/core_window_decl.h
  cores/core_window_grant.h
  cores/dip_setting.h
  cores/file_tx.h
  cores/host_bit_row.h
  cores/host_policy.h
  cores/ladder_carry.h
  cores/ladder_pc.h
  cores/load_progress.h
  cores/nvram_save.h
  cores/option_row.h
  cores/option_row_view.h
  cores/option_rows.h
  cores/pause_on_osd.h
  cores/progress_ticker.h
  cores/reset_row.h
  cores/romset_alias_source.h
  cores/row_shape_key.h
  cores/save_digit.h
  cores/save_state_triggers.h
  cores/save_state_triggers_table.h
  cores/save_store_column.h
  cores/start_rule.h
  cores/wire_chunk.h
)

tasty_add_header_checks(tasty_l3_n64_header_checks LIB mister_cores_n64 HEADERS
  cores/n64/n64_detect.h
  cores/n64/n64_analog.h
  cores/n64/n64_byte_mask.h
  cores/n64/n64_cheat_code.h
  cores/n64/n64_cheat_engine.h
  cores/n64/n64_core.h
  cores/n64/n64_file_route.h
  cores/n64/n64_load_facts.h
  cores/n64/n64_load_map.h
  cores/n64/n64_loader.h
  cores/n64/n64_cpak.h
  cores/n64/n64_db.h
  cores/n64/n64_db_match.h
  cores/n64/n64_db_reader.h
  cores/n64/n64_db_tags.h
  cores/n64/n64_ipl3.h
  cores/n64/n64_ipl3_row.h
  cores/n64/n64_options.h
  cores/n64/n64_patch_table.h
  cores/n64/n64_rom.h
  cores/n64/n64_save_layout.h
  cores/n64/n64_layout_key_source.h
  cores/n64/n64_save_layout_key.h
  cores/n64/n64_save_attach.h
  cores/n64/n64_save_order.h
  cores/n64/n64_save_plan.h
  cores/n64/n64_save_store.h
  cores/n64/n64_memory_type.h
  cores/n64/save_rows.h
  cores/n64/n64_saves.h
  cores/n64/n64_path.h
  cores/n64/bootcode_sum.h
  cores/n64/halfword_swap.h
  cores/n64/n64_detect_flags.h
  cores/n64/n64_ntsc_cadence.h
  cores/n64/n64_option_view.h
  cores/n64/n64_pass_kind.h
  cores/n64/n64_rom_facts.h
  cores/n64/n64_row_pass.h
  cores/n64/n64_system_type.h
  cores/n64/save_word_reverse.h
)
tasty_add_header_checks(tasty_l3_n64dd_header_checks LIB mister_cores_n64dd HEADERS
  cores/n64dd/n64dd_disk_type.h
  cores/n64dd/n64dd_disk_map.h
  cores/n64dd/n64dd_expander.h
  cores/n64dd/n64dd_geometry.h
  cores/n64dd/n64dd_ram_save.h
  cores/n64dd/n64dd_system_area.h
  cores/n64dd/n64_ram_save_order.h
)

tasty_add_header_checks(tasty_l3_cd_header_checks LIB mister_cores_cd HEADERS
  cores/cd/saturn_cdd_frame.h
  cores/cd/saturn_mode.h
  cores/cd/sega_cdd_stat.h
  cores/cd/sega_frame_word.h
  cores/cd/cd_census_rows.h
  cores/cd/cd_command_round.h
  cores/cd/cd_command_words.h
  cores/cd/cd_core.h
  cores/cd/cd_dialect_facts.h
  cores/cd/cd_dialect_ports.h
  cores/cd/cd_cadence_gate.h
  cores/cd/cd_disc_slot.h
  cores/cd/cd_drive.h
  cores/cd/cd_engine.h
  cores/cd/cd_engine_stats.h
  cores/cd/cd_head_prime.h
  cores/cd/cd_io_indices.h
  cores/cd/cd_link.h
  cores/cd/cd_profile.h
  cores/cd/cd_sector_act.h
  cores/cd/cd_speed_index.h
  cores/cd/cd_tick_periods.h
  cores/cd/cd_wire_link.h
  cores/cd/cd_wire_port.h
  cores/cd/mount_answer.h
  cores/cd/megacd_ladder.h
  cores/cd/pcecd_ladder.h
  cores/cd/threedo_ladder.h
  cores/cd/saturn_ladder.h
  cores/cd/cdwire.h
  cores/cd/pce_scsi_engine.h
  cores/cd/pce_command.h
  cores/cd/pce_subq.h
  cores/cd/pce_subq_reply.h
  cores/cd/pce_sense_reply.h
  cores/cd/pce_dir_info_reply.h
  cores/cd/cdda_play_range.h
  cores/cd/saturn_slot_ring.h
  cores/cd/saturn_stock_toc.h
  cores/cd/saturn_pending.h
  cores/cd/saturn_cue.h
  cores/cd/saturn_cdd_engine.h
  cores/cd/cd_quirk_row.h
  cores/cd/saturn_ring.h
  cores/cd/saturn_seek.h
  cores/cd/sega_cdd_engine.h
  cores/cd/service_tick_row.h
  cores/cd/threedo_cdd_engine.h
  cores/cd/poll_ticks.h
  cores/cd/cd_speed_count.h
  cores/cd/descriptor_io.h
  cores/cd/row_cursor.h
  cores/cd/sector_io.h
  cores/cd/subcode_q_frame.h
)
tasty_add_header_checks(tasty_l3_header_checks LIB mister_cores HEADERS
  cores/generic_profile.h
  cores/servant_factories.h
  cores/generic_core.h
  cores/pcm_source_factory.h
  cores/registry.h
  cores/manifests/electron.h
  cores/manifests/megacd.h
  cores/manifests/megadrive.h
  cores/manifests/menu.h
  cores/manifests/n64.h
  cores/manifests/neogeo.h
  cores/manifests/pce.h
  cores/manifests/psx.h
  cores/manifests/snes.h
  cores/manifests/st.h
  cores/manifests/threedo.h
  cores/manifests/saturn.h
  cores/manifests/cdi.h
  cores/manifests/uneon.h
  cores/manifests/x86.h
)

tasty_add_header_checks(tasty_l3_mra_header_checks LIB mister_cores_mra HEADERS
  cores/mra/arcade_core.h
  cores/mra/arcade_facts.h
  cores/mra/arcade_manifest_role.h
  cores/mra/assembled_rom.h
  cores/mra/dip_row.h
  cores/mra/file_part.h
  cores/mra/fill_run.h
  cores/mra/interleave_end.h
  cores/mra/interleave_start.h
  cores/mra/interleave_widths.h
  cores/mra/mra_cheat.h
  cores/mra/mra_cheat_records.h
  cores/mra/mra_doc.h
  cores/mra/mra_lane.h
  cores/mra/mra_lane_writes.h
  cores/mra/mra_map_word.h
  cores/mra/mra_nvram_decl.h
  cores/mra/mra_stager.h
  cores/mra/mra_unit_len.h
  cores/mra/part_common.h
  cores/mra/patch.h
  cores/mra/rom.h
  cores/mra/rom_bytes.h
  cores/mra/rom_node.h
  cores/mra/rom_plan.h
  cores/mra/rom_set_assembler.h
  cores/mra/rom_stream.h
  cores/mra/switch_table.h
  cores/mra/text_part.h
)
tasty_add_header_checks(tasty_l3_st_header_checks LIB mister_cores_st HEADERS
  cores/st/acsi_servant.h
  cores/st/st_config.h
  cores/st/st_config_layout.h
  cores/st/st_core.h
  cores/st/st_ctrl.h
  cores/st/st_loader.h
  cores/st/st_media_fact.h
  cores/st/st_media_ladder.h
  cores/st/st_option_row.h
  cores/st/st_options.h
  cores/st/st_slot.h
  cores/st/st_sysctrl.h
)
tasty_add_header_checks(tasty_cores_tas_header_checks LIB mister_cores_tas HEADERS
  cores/tas/bk2_column.h
  cores/tas/bk2_columns.h
  cores/tas/bk2_log.h
  cores/tas/bk2_text.h
  cores/tas/movie_archive.h
  cores/tas/movie_codec.h
  cores/tas/i_movie_format.h
  cores/tas/i_replay_core.h
  cores/tas/movie_command_mask.h
  cores/tas/movie_facts.h
  cores/tas/movie_frame.h
  cores/tas/movie_frame_tally.h
  cores/tas/movie_lines.h
  cores/tas/movie_rom_match.h
  cores/tas/movie_region.h
  cores/tas/movie_refusal.h
  cores/tas/movie_rerecord_count.h
  cores/tas/movie_system.h
  cores/tas/nes_fm2_codec.h
  cores/tas/nes_movie_codec.h
  cores/tas/nes_bk2_codec.h
  cores/tas/digest_value.h
  cores/tas/rom_digest.h
  cores/tas/ram_image_recipe.h
  cores/tas/snes_movie_codec.h
  cores/tas/snes_lsmv_codec.h
  cores/tas/snes_bk2_codec.h
  cores/tas/megadrive_movie_codec.h
  cores/tas/megadrive_bk2_format.h
  cores/tas/megadrive_gmv_format.h
  cores/tas/psx_movie_codec.h
  cores/tas/psx_bk2_format.h
  cores/tas/psx_disc_key.h
)

