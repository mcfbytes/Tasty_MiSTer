add_library(mister_infra STATIC
  src/error.cpp
  src/seat.cpp
)
target_include_directories(mister_infra PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_infra PRIVATE tasty_options)

tasty_add_header_checks(tasty_infra_header_checks LIB mister_infra HEADERS
  infra/ascii.h
  infra/byte_view.h
  infra/claim_set.h
  infra/counter.h
  infra/crc32.h
  infra/endian.h
  infra/diag_log.h
  infra/error.h
  infra/fixed_ring.h
  infra/fixed_str.h
  infra/inbox.h
  infra/json_name.h
  infra/json_out.h
  infra/lat_hist.h
  infra/log_lane.h
  infra/log_rec.h
  infra/log_rec_dispatch.h
  infra/message_sum.h
  infra/opt_ref.h
  infra/park_fds.h
  infra/park_like.h
  infra/parse_int.h
  infra/lease_order.h
  infra/pause_latch.h
  infra/persist.h
  infra/posix_compat.h
  infra/rt_latency.h
  infra/rt_stats.h
  infra/seat.h
  infra/seat_main.h
  infra/seat_owner_io.h
  infra/seat_owner_main.h
  infra/seat_owner_ui.h
  infra/seat_park.h
  infra/seq_cell.h
  infra/never_park.h
  infra/slot_pool.h
  infra/loan_channel.h
  infra/spsc_ring.h
  infra/telemetry.h
  infra/unique_fd.h
  infra/kick_policy.h
  infra/wake_flag.h
)

