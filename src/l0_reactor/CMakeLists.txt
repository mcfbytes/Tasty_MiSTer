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
    tasty_unit_test(tasty_reactor_${mod}_unit
      NAME tasty.reactor_${mod}
      SOURCES tests/${mod}_unit.cpp
      LIBS mister_reactor tasty_test_options
      TIMEOUT 60)
  endforeach()

  target_include_directories(tasty_reactor_executive_unit
                             PRIVATE ${CMAKE_SOURCE_DIR}/tests)

  if(NOT CMAKE_CROSSCOMPILING)
    set_tests_properties(tasty.reactor_executive
                         PROPERTIES FAIL_REGULAR_EXPRESSION "NOT CHECKED:")
  endif()

  tasty_unit_test(tasty_corestate_redef_probe
    NAME tasty.reactor_corestate_redef
    SOURCES tests/corestate_redef_probe.cpp
    EXCLUDE_FROM_ALL
    LIBS mister_reactor tasty_test_options
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target tasty_corestate_redef_probe
    LABELS compile_probe
    TIMEOUT 120
    PROPERTIES WILL_FAIL
      TRUE

      RUN_SERIAL
      TRUE)
endif()
