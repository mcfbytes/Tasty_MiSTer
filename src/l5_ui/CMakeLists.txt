add_library(mister_ui STATIC
  src/arc_label.cpp
  src/browser_model.cpp
  src/browser_page.cpp
  src/cheats_page.cpp
  src/config_slots_page.cpp
  src/confirm_page.cpp
  src/core_cell_mirror.cpp
  src/core_pick_host.cpp
  src/core_settings_page.cpp
  src/dips_page.cpp
  src/event_trace.cpp
  src/game_slot.cpp
  src/hd_key_remap.cpp
  src/hd_label.cpp
  src/info_page.cpp
  src/joymap_page.cpp
  src/list_view.cpp
  src/menu_core_binding.cpp
  src/menu_model.cpp
  src/navigator.cpp
  src/option_items.cpp
  src/options_page.cpp
  src/osd_renderer.cpp
  src/progress_page.cpp
  src/script_policy.cpp
  src/script_pick_host.cpp
  src/script_run_page.cpp
  src/script_runner.cpp
  src/slot_pick_host.cpp
  src/system_page.cpp
  src/ui_round.cpp
)
target_include_directories(mister_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_ui
  PUBLIC mister_app
  PRIVATE tasty_options)

tasty_add_header_checks(tasty_ui_header_checks LIB mister_ui HEADERS
  ui/bt_ops.h
  ui/bt_pair_policy.h
  ui/browse_source.h
  ui/browser_entry.h
  ui/browser_host.h
  ui/browser_launch.h
  ui/browser_page.h
  ui/browser_session.h
  ui/build_env.h
  ui/config_slots_page.h
  ui/confirm_host.h
  ui/confirm_launch.h
  ui/confirm_page.h
  ui/core_cell_mirror.h
  ui/core_pick_host.h
  ui/core_options_page.h
  ui/core_settings_page.h
  ui/cheats_page.h
  ui/dips_page.h
  ui/event_trace.h
  ui/fb_terminal_host.h
  ui/game_slot.h
  ui/glyph_rom.h
  ui/hd_key_remap.h
  ui/hd_label.h
  ui/host_sampler.h
  ui/info_page.h
  ui/joymap_page.h
  ui/key_decoder.h
  ui/list_view.h
  ui/menu_core_binding.h
  ui/menu_model.h
  ui/navigator.h
  ui/option_host.h
  ui/option_items/addon_item.h
  ui/option_items/cfg_ver_item.h
  ui/option_items/cheats_item.h
  ui/option_items/cycle_item.h
  ui/option_items/def_mra_item.h
  ui/option_items/dip_item.h
  ui/option_items/exit_item.h
  ui/option_items/file_item.h
  ui/option_items/image_item.h
  ui/option_items/info_item.h
  ui/option_items/joy_names_item.h
  ui/option_items/manual_item.h
  ui/option_items/no_osd_item.h
  ui/option_items/page_item.h
  ui/option_items/separator_item.h
  ui/option_items/toggle_close_item.h
  ui/option_items/toggle_item.h
  ui/option_items/unknown_item.h
  ui/option_items/version_item.h
  ui/options_model.h
  ui/options_page.h
  ui/osd_renderer.h
  ui/osd_style_preference.h
  ui/osd_visible_file.h
  ui/page.h
  ui/page_ask.h
  ui/page_context.h
  ui/page_launches.h
  ui/page_id.h
  ui/page_stack.h
  ui/script_file_policy.h
  ui/script_kind.h
  ui/script_launch.h
  ui/script_pick_host.h
  ui/script_policy.h
  ui/script_run_page.h
  ui/script_runner.h
  ui/shell_bt_ops.h
  ui/slot_pick_host.h
  ui/slot_labels.h
  ui/status_bar_facts.h
  ui/status_view.h
  ui/system_facts.h
  ui/system_page.h
  ui/transition.h
  ui/types.h
  ui/ui_event.h
  ui/ui_round.h
  ui/vfs_browse_source.h
)


target_sources(mister_ui PRIVATE
  src/hd_about_plan.cpp
  src/hd_controllers_plan.cpp
  src/hd_core_about_plan.cpp
  src/hd_display_plan.cpp
  src/hd_focus_memory.cpp
  src/hd_list_page.cpp
  src/hd_media_plan.cpp
  src/hd_recent_games_plan.cpp
  src/recent_launch.cpp
  src/recent_row.cpp
  src/hd_rows.cpp
  src/hd_settings_plan.cpp
  src/hd_slot_sheet_plan.cpp
  src/placer.cpp
)

tasty_add_header_checks(tasty_ui_hd_list_header_checks LIB mister_ui HEADERS
  ui/core_verbs.h
  ui/hd_about_plan.h
  ui/hd_controllers_plan.h
  ui/hd_core_about_plan.h
  ui/hd_display_plan.h
  ui/hd_focus_memory.h
  ui/hd_list_page.h
  ui/hd_media_plan.h
  ui/hd_plan.h
  ui/hd_plan_inputs.h
  ui/hd_recent_games_plan.h
  ui/recent_launch.h
  ui/recent_row.h
  ui/hd_row.h
  ui/hd_rows/hd_action.h
  ui/hd_rows/hd_ask_row.h
  ui/hd_rows/hd_close_row.h
  ui/hd_rows/hd_push_row.h
  ui/hd_rows/hd_separator_row.h
  ui/hd_rows/hd_value_row.h
  ui/hd_settings_plan.h
  ui/hd_slot_sheet_plan.h
  ui/placement.h
  ui/placer.h
  ui/slot_focus.h
)


target_sources(mister_ui PRIVATE
  src/classic_options_view.cpp
  src/hd_options_plan.cpp
  src/hd_options_view.cpp
)

tasty_add_header_checks(tasty_ui_options_view_header_checks LIB mister_ui HEADERS
  ui/classic_options_view.h
  ui/hd_options_plan.h
  ui/hd_options_view.h
  ui/hd_rows/hd_option_row.h
  ui/hd_rows/hd_save_row.h
  ui/options_surface.h
  ui/place_facts.h
  ui/options_view.h
)


target_sources(mister_ui PRIVATE
  src/game_title.cpp
  src/now_playing.cpp
)

tasty_add_header_checks(tasty_ui_now_playing_header_checks LIB mister_ui HEADERS
  ui/game_title.h
  ui/now_playing.h
)


target_sources(mister_ui PRIVATE
  src/hd_home_plan.cpp
  src/hd_root_plan.cpp
  src/saved_baseline.cpp
)

tasty_add_header_checks(tasty_ui_hd_root_header_checks LIB mister_ui HEADERS
  ui/hd_home_plan.h
  ui/hd_rewalk.h
  ui/hd_root_plan.h
  ui/hd_rows/hd_gated_row.h
  ui/hd_rows/hd_pair_row.h
  ui/saved_baseline.h
)



