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
  reactor/frame_record.h
  reactor/frame_main.h
  reactor/link_decoder.h
  reactor/link_decoder_decl.h
  reactor/notifier.h
  reactor/notifier_slot.h
  reactor/round_timer.h
  reactor/round_timing.h
  reactor/tick.h
)

