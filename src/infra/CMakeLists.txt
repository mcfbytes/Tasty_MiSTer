add_library(mister_infra STATIC
  src/error.cpp
  src/seat.cpp
)
target_include_directories(mister_infra PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_infra PRIVATE tasty_options)

add_library(mister_infra_xthread INTERFACE)
target_include_directories(mister_infra_xthread INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_infra_xthread INTERFACE mister_infra)

tasty_add_header_checks(tasty_infra_header_checks LIB mister_infra HEADERS
  infra/ascii.h
  infra/byte_offset.h
  infra/byte_count.h
  infra/byte_view.h
  infra/cell_gen.h
  infra/crc32.h
  infra/endian.h
  infra/enum_set.h
  infra/error.h
  infra/errno.h
  infra/file_mode.h
  infra/fixed_ring.h
  infra/fixed_str.h
  infra/heartbeat.h
  infra/affine.h
  infra/span_cut.h
  infra/run.h
  infra/index_range.h
  infra/index_set.h
  infra/indexed_array.h
  infra/indexed_span.h
  infra/json_name.h
  infra/json_out.h
  infra/json_width.h
  infra/lat_hist.h
  infra/park_timeout.h
  infra/message_sum.h
  infra/mint_guard.h
  infra/narrow_duration.h
  infra/opt_ref.h
  infra/parse_int.h
  infra/path_parts.h
  infra/path_text.h
  infra/period_grid.h
  infra/persist.h
  infra/posix_compat.h
  infra/rename_files.h
  infra/rt_latency.h
  infra/saturating.h
  infra/service_set.h
  infra/service_slot.h
  infra/seat.h
  infra/seat_owner_diag.h
  infra/seat_owner_encode.h
  infra/seat_owner_input.h
  infra/seat_owner_io.h
  infra/seat_owner_main.h
  infra/seat_owner_ui.h
  infra/niche.h
  infra/tally.h
  infra/unique_fd.h
  infra/utf8.h
  infra/wired_end.h
  infra/decimal_text.h
  infra/hex_text.h
  infra/seat_role.h
  infra/serial.h
  infra/prelude.h
)

tasty_add_header_checks(tasty_infra_xthread_header_checks LIB mister_infra_xthread HEADERS
  infra/xthread/claim_set.h
  infra/xthread/coalesced_kick.h
  infra/xthread/counted_lane.h
  infra/xthread/counter.h
  infra/xthread/coworker.h
  infra/xthread/diag_log.h
  infra/xthread/grant_lease.h
  infra/xthread/inbox.h
  infra/xthread/kick_policy.h
  infra/xthread/loan_channel.h
  infra/xthread/never_park.h
  infra/xthread/observed.h
  infra/xthread/park_fds.h
  infra/xthread/park_like.h
  infra/xthread/pause_gen.h
  infra/xthread/pause_latch.h
  infra/xthread/polled.h
  infra/xthread/pool_slot.h
  infra/xthread/release_gen.h
  infra/xthread/release_latch.h
  infra/xthread/ring_slab.h
  infra/xthread/rt_stats.h
  infra/xthread/seat_main.h
  infra/xthread/seat_park.h
  infra/xthread/seq_cell.h
  infra/xthread/slot_pool.h
  infra/xthread/slot_stride.h
  infra/xthread/spsc_ring.h
  infra/xthread/telemetry.h
  infra/xthread/wake_flag.h
)

