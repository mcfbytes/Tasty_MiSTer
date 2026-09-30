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
  infra/log_lane.h
  infra/log_rec.h
  infra/message_sum.h
  infra/persist.h
  infra/rt_stats.h
  infra/seat.h
  infra/seat_main.h
  infra/seat_park.h
  infra/never_park.h
  infra/slot_pool.h
  infra/loan_channel.h
  infra/spsc_ring.h
  infra/telemetry.h
  infra/unique_fd.h
  infra/wake_flag.h
)

if(TASTY_BUILD_TESTS)
  add_executable(tasty_infra_crc32_unit tests/crc32_unit.cpp)
  target_link_libraries(tasty_infra_crc32_unit PRIVATE mister_infra tasty_test_options)
  add_test(NAME tasty.infra_crc32 COMMAND tasty_infra_crc32_unit)
  set_tests_properties(tasty.infra_crc32 PROPERTIES TIMEOUT 60)

  find_package(Threads REQUIRED)
  add_executable(tasty_infra_loan_channel_unit tests/loan_channel_unit.cpp)
  target_link_libraries(tasty_infra_loan_channel_unit PRIVATE mister_infra tasty_test_options Threads::Threads)
  add_test(NAME tasty.infra_loan_channel COMMAND tasty_infra_loan_channel_unit)
  set_tests_properties(tasty.infra_loan_channel PROPERTIES TIMEOUT 120)

  add_executable(tasty_infra_park_smoke tests/park_smoke.cpp)
  target_link_libraries(tasty_infra_park_smoke PRIVATE mister_infra tasty_test_options)
  add_test(NAME tasty.infra_park_smoke COMMAND tasty_infra_park_smoke)
  set_tests_properties(tasty.infra_park_smoke PROPERTIES TIMEOUT 60)

  add_executable(tasty_infra_wake_flag_arm_unit tests/wake_flag_arm_unit.cpp)
  target_link_libraries(tasty_infra_wake_flag_arm_unit PRIVATE mister_infra tasty_test_options)
  add_test(NAME tasty.infra_wake_flag_arm COMMAND tasty_infra_wake_flag_arm_unit)
  set_tests_properties(tasty.infra_wake_flag_arm PROPERTIES TIMEOUT 60)

  add_executable(tasty_infra_spsc_ring_unit tests/spsc_ring_unit.cpp)
  target_link_libraries(tasty_infra_spsc_ring_unit PRIVATE mister_infra tasty_test_options Threads::Threads)
  add_test(NAME tasty.infra_spsc_ring COMMAND tasty_infra_spsc_ring_unit)
  set_tests_properties(tasty.infra_spsc_ring PROPERTIES TIMEOUT 60)

  add_executable(tasty_infra_seat_park_unit tests/seat_park_unit.cpp)
  target_link_libraries(tasty_infra_seat_park_unit PRIVATE mister_infra tasty_test_options Threads::Threads)
  add_test(NAME tasty.infra_seat_park COMMAND tasty_infra_seat_park_unit)
  set_tests_properties(tasty.infra_seat_park PROPERTIES TIMEOUT 60)

  add_executable(tasty_infra_seat_main_unit tests/seat_main_unit.cpp)
  target_link_libraries(tasty_infra_seat_main_unit PRIVATE mister_infra tasty_test_options)
  add_test(NAME tasty.infra_seat_main COMMAND tasty_infra_seat_main_unit)
  set_tests_properties(tasty.infra_seat_main PROPERTIES TIMEOUT 60)

  add_executable(tasty_infra_pause_latch_unit tests/pause_latch_unit.cpp)
  target_link_libraries(tasty_infra_pause_latch_unit PRIVATE mister_infra tasty_test_options)
  add_test(NAME tasty.infra_pause_latch COMMAND tasty_infra_pause_latch_unit)
  set_tests_properties(tasty.infra_pause_latch PROPERTIES TIMEOUT 60)

  add_executable(tasty_infra_inbox_unit tests/inbox_unit.cpp)
  target_link_libraries(tasty_infra_inbox_unit PRIVATE mister_infra tasty_test_options)
  add_test(NAME tasty.infra_inbox COMMAND tasty_infra_inbox_unit)
  set_tests_properties(tasty.infra_inbox PROPERTIES TIMEOUT 60)

  add_executable(tasty_infra_lost_wakeup_unit tests/lost_wakeup_unit.cpp)
  target_link_libraries(tasty_infra_lost_wakeup_unit PRIVATE mister_infra tasty_test_options Threads::Threads)
  add_test(NAME tasty.infra_lost_wakeup COMMAND tasty_infra_lost_wakeup_unit)
  set_tests_properties(tasty.infra_lost_wakeup PROPERTIES TIMEOUT 120)

  foreach(model IN ITEMS wake_inbox_lost_wakeup spsc_wrap telemetry_seqlock counter_monotonic pause_latch_barrier)
    add_executable(tasty_infra_model_${model} tests/model/${model}.cpp)
    target_link_libraries(tasty_infra_model_${model} PRIVATE mister_infra tasty_test_options Threads::Threads)
    add_test(NAME tasty.infra_model_${model} COMMAND tasty_infra_model_${model})
    set_tests_properties(tasty.infra_model_${model} PROPERTIES TIMEOUT 120)
  endforeach()
  add_test(NAME tasty.infra_genmc
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/model/run-genmc.sh
                   ${CMAKE_CURRENT_SOURCE_DIR}/include)
  set_tests_properties(tasty.infra_genmc PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  add_executable(tasty_infra_model_loan_channel_handoff tests/model/loan_channel_handoff.cpp)
  target_link_libraries(tasty_infra_model_loan_channel_handoff
                        PRIVATE mister_infra tasty_test_options Threads::Threads)
  add_test(NAME tasty.infra_model_loan_channel_handoff
           COMMAND tasty_infra_model_loan_channel_handoff)
  set_tests_properties(tasty.infra_model_loan_channel_handoff PROPERTIES TIMEOUT 120)
  add_test(NAME tasty.infra_genmc_loan
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/model/run-genmc-loan.sh
                   ${CMAKE_CURRENT_SOURCE_DIR}/include)
  set_tests_properties(tasty.infra_genmc_loan PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  add_test(NAME tasty.infra_genmc_cells
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/model/run-genmc-cells.sh
                   ${CMAKE_CURRENT_SOURCE_DIR}/include)
  set_tests_properties(tasty.infra_genmc_cells PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)

  add_executable(tasty_infra_slot_pool_unit tests/slot_pool_unit.cpp)
  target_link_libraries(tasty_infra_slot_pool_unit PRIVATE mister_infra tasty_test_options)
  add_test(NAME tasty.infra_slot_pool COMMAND tasty_infra_slot_pool_unit)
  set_tests_properties(tasty.infra_slot_pool PROPERTIES TIMEOUT 60)

  add_executable(tasty_infra_claim_set_unit tests/claim_set_unit.cpp)
  target_link_libraries(tasty_infra_claim_set_unit PRIVATE mister_infra tasty_test_options)
  add_test(NAME tasty.infra_claim_set COMMAND tasty_infra_claim_set_unit)
  set_tests_properties(tasty.infra_claim_set PROPERTIES TIMEOUT 60)

  add_executable(tasty_infra_message_sum_unit tests/message_sum_unit.cpp)
  target_link_libraries(tasty_infra_message_sum_unit PRIVATE mister_infra tasty_test_options)
  add_test(NAME tasty.infra_message_sum COMMAND tasty_infra_message_sum_unit)
  set_tests_properties(tasty.infra_message_sum PROPERTIES TIMEOUT 60)

  set(_unrouted "an on\\(\\) overload for an alternative this sink is not routed")
  set(_msgsum_probe_unrouted_on "${_unrouted}")
  set(_msgsum_probe_generic_on "${_unrouted}")
  set(_msgsum_probe_rvalue_on "${_unrouted}")
  set(_msgsum_probe_missing_on "cannot convert .*Probe::Load.* to .*Probe::Ping&|no matching member function for call to .on.|no viable conversion from .*Probe::Load.* to .*Probe::Ping")
  set(_msgsum_probe_untotal "a message sum whose alternatives are not total")
  set(_msgsum_probe_prvalue_ctx "cannot convert .*Round.* to .*Round&|cannot bind to a temporary")
  foreach(probe IN ITEMS unrouted_on generic_on rvalue_on missing_on untotal prvalue_ctx)
    add_executable(tasty_infra_message_sum_${probe}_probe EXCLUDE_FROM_ALL
                   tests/message_sum_${probe}_probe.cpp)
    target_link_libraries(tasty_infra_message_sum_${probe}_probe PRIVATE mister_infra tasty_test_options)
    add_test(NAME tasty.infra_message_sum_${probe}_probe
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                     --target tasty_infra_message_sum_${probe}_probe)
    set_tests_properties(tasty.infra_message_sum_${probe}_probe PROPERTIES
      PASS_REGULAR_EXPRESSION "${_msgsum_probe_${probe}}" TIMEOUT 120 RUN_SERIAL TRUE
      LABELS compile_probe)
  endforeach()

  get_property(_gf_genmc_tests DIRECTORY PROPERTY TESTS)
  list(FILTER _gf_genmc_tests INCLUDE REGEX "genmc")
  set_tests_properties(${_gf_genmc_tests} PROPERTIES LABELS slow)
endif()
