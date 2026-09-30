add_library(mister_reactor STATIC
  src/core_notifiers.cpp
  src/executive.cpp
  src/frame_clock.cpp
  src/frame_main.cpp
  src/notifier.cpp
  src/round_timer.cpp
)
target_include_directories(mister_reactor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_reactor
  PUBLIC mister_hal
  PRIVATE tasty_options)

tasty_add_header_checks(tasty_reactor_header_checks LIB mister_reactor HEADERS
  reactor/cause_set.h
  reactor/core_notifiers.h
  reactor/core_state.h
  reactor/core_token.h
  reactor/executive.h
  reactor/frame_clock.h
  reactor/frame_main.h
  reactor/link_decoder.h
  reactor/link_decoder_decl.h
  reactor/notifier.h
  reactor/notifier_slot.h
  reactor/round_timer.h
  reactor/round_timing.h
  reactor/tick.h
)

if(TASTY_BUILD_TESTS)

  foreach(mod IN ITEMS core_notifiers executive frame_clock notifier round_timer)
    add_executable(tasty_reactor_${mod}_unit tests/${mod}_unit.cpp)
    target_link_libraries(tasty_reactor_${mod}_unit PRIVATE mister_reactor tasty_test_options)
    add_test(NAME tasty.reactor_${mod} COMMAND tasty_reactor_${mod}_unit)
    set_tests_properties(tasty.reactor_${mod} PROPERTIES TIMEOUT 60)
  endforeach()

  target_include_directories(tasty_reactor_executive_unit
                             PRIVATE ${CMAKE_SOURCE_DIR}/tests)

  add_executable(tasty_corestate_redef_probe EXCLUDE_FROM_ALL
                 tests/corestate_redef_probe.cpp)
  target_link_libraries(tasty_corestate_redef_probe PRIVATE mister_reactor tasty_test_options)
  add_test(NAME tasty.reactor_corestate_redef
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                   --target tasty_corestate_redef_probe)
  set_tests_properties(tasty.reactor_corestate_redef PROPERTIES
    WILL_FAIL TRUE
    LABELS compile_probe
    TIMEOUT 120

    RUN_SERIAL TRUE)
endif()
