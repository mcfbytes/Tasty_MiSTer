add_library(mister_infra STATIC
  src/error.cpp
  src/seat.cpp
)
target_include_directories(mister_infra PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_infra PRIVATE tasty_options)

tasty_add_header_checks(tasty_infra_header_checks LIB mister_infra HEADERS
  infra/claim_set.h
  infra/counter.h
  infra/crc32.h
  infra/diag_log.h
  infra/error.h
  infra/fixed_str.h
  infra/inbox.h
  infra/json_name.h
  infra/json_out.h
  infra/log_lane.h
  infra/log_rec.h
  infra/message_sum.h
  infra/opt_ref.h
  infra/park_fds.h
  infra/persist.h
  infra/rt_stats.h
  infra/seat.h
  infra/seat_main.h
  infra/seat_park.h
  infra/seq_cell.h
  infra/never_park.h
  infra/slot_pool.h
  infra/loan_channel.h
  infra/spsc_ring.h
  infra/telemetry.h
  infra/unique_fd.h
  infra/wake_flag.h
)

if(TASTY_BUILD_TESTS)
  tasty_unit_test(tasty_infra_crc32_unit
    NAME tasty.infra_crc32
    SOURCES tests/crc32_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  find_package(Threads REQUIRED)
  tasty_unit_test(tasty_infra_loan_channel_unit
    NAME tasty.infra_loan_channel
    SOURCES tests/loan_channel_unit.cpp
    LIBS mister_infra tasty_test_options Threads::Threads
    TIMEOUT 120)

  tasty_unit_test(tasty_infra_park_smoke
    NAME tasty.infra_park_smoke
    SOURCES tests/park_smoke.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_wake_flag_arm_unit
    NAME tasty.infra_wake_flag_arm
    SOURCES tests/wake_flag_arm_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_spsc_ring_unit
    NAME tasty.infra_spsc_ring
    SOURCES tests/spsc_ring_unit.cpp
    LIBS mister_infra tasty_test_options Threads::Threads
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_seat_park_unit
    NAME tasty.infra_seat_park
    SOURCES tests/seat_park_unit.cpp
    LIBS mister_infra tasty_test_options Threads::Threads
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_park_fds_unit
    NAME tasty.infra_park_fds
    SOURCES tests/park_fds_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_seat_main_unit
    NAME tasty.infra_seat_main
    SOURCES tests/seat_main_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_pause_latch_unit
    NAME tasty.infra_pause_latch
    SOURCES tests/pause_latch_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_json_out_unit
    NAME tasty.infra_json_out
    SOURCES tests/json_out_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_inbox_unit
    NAME tasty.infra_inbox
    SOURCES tests/inbox_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_lost_wakeup_unit
    NAME tasty.infra_lost_wakeup
    SOURCES tests/lost_wakeup_unit.cpp
    LIBS mister_infra tasty_test_options Threads::Threads
    TIMEOUT 120)

  foreach(model IN ITEMS wake_inbox_lost_wakeup spsc_wrap telemetry_seqlock counter_monotonic
                        pause_latch_barrier seq_cell_exact)
    tasty_unit_test(tasty_infra_model_${model}
      NAME tasty.infra_model_${model}
      SOURCES tests/model/${model}.cpp
      LIBS mister_infra tasty_test_options Threads::Threads
      TIMEOUT 120)
  endforeach()
  add_test(NAME tasty.infra_genmc
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/model/run-genmc.sh
                   ${CMAKE_CURRENT_SOURCE_DIR}/include)
  set_tests_properties(tasty.infra_genmc PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  tasty_unit_test(tasty_infra_model_loan_channel_handoff
    NAME tasty.infra_model_loan_channel_handoff
    SOURCES tests/model/loan_channel_handoff.cpp
    LIBS mister_infra tasty_test_options Threads::Threads
    TIMEOUT 120)
  add_test(NAME tasty.infra_genmc_loan
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/model/run-genmc-loan.sh
                   ${CMAKE_CURRENT_SOURCE_DIR}/include)
  set_tests_properties(tasty.infra_genmc_loan PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  add_test(NAME tasty.infra_genmc_cells
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/model/run-genmc-cells.sh
                   ${CMAKE_CURRENT_SOURCE_DIR}/include)
  set_tests_properties(tasty.infra_genmc_cells PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  tasty_unit_test(tasty_infra_seq_cell_unit
    NAME tasty.infra_seq_cell
    SOURCES tests/seq_cell_unit.cpp
    LIBS mister_infra tasty_test_options Threads::Threads
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_slot_pool_unit
    NAME tasty.infra_slot_pool
    SOURCES tests/slot_pool_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_opt_ref_unit
    NAME tasty.infra_opt_ref
    SOURCES tests/opt_ref_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_claim_set_unit
    NAME tasty.infra_claim_set
    SOURCES tests/claim_set_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  tasty_unit_test(tasty_infra_message_sum_unit
    NAME tasty.infra_message_sum
    SOURCES tests/message_sum_unit.cpp
    LIBS mister_infra tasty_test_options
    TIMEOUT 60)

  set(_unrouted "an on\\(\\) overload for an alternative this sink is not routed")
  set(_msgsum_probe_unrouted_on "${_unrouted}")
  set(_msgsum_probe_generic_on "${_unrouted}")
  set(_msgsum_probe_rvalue_on "${_unrouted}")
  set(_msgsum_probe_missing_on "cannot convert .*Probe::Load.* to .*Probe::Ping&|no matching member function for call to .on.|no viable conversion from .*Probe::Load.* to .*Probe::Ping")
  set(_msgsum_probe_untotal "a message sum whose alternatives are not total")
  set(_msgsum_probe_prvalue_ctx "cannot convert .*Round.* to .*Round&|cannot bind to a temporary")
  foreach(probe IN ITEMS unrouted_on generic_on rvalue_on missing_on untotal prvalue_ctx)
    tasty_unit_test(tasty_infra_message_sum_${probe}_probe
      NAME tasty.infra_message_sum_${probe}_probe
      SOURCES tests/message_sum_${probe}_probe.cpp
      EXCLUDE_FROM_ALL
      LIBS mister_infra tasty_test_options
      COMMAND ${CMAKE_COMMAND}
        --build
        ${CMAKE_BINARY_DIR}
        --target
        tasty_infra_message_sum_${probe}_probe
      LABELS compile_probe
      TIMEOUT 120
      PROPERTIES PASS_REGULAR_EXPRESSION "${_msgsum_probe_${probe}}" RUN_SERIAL TRUE)
  endforeach()

  get_property(_tasty_genmc_tests DIRECTORY PROPERTY TESTS)
  list(FILTER _tasty_genmc_tests INCLUDE REGEX "genmc")
  set_tests_properties(${_tasty_genmc_tests} PROPERTIES LABELS slow)
endif()
