add_library(mister_proto STATIC
  src/block_slots.cpp
  src/conf_str.cpp
  src/core_session.cpp
  src/download_session.cpp
  src/frame_counter_read.cpp
  src/image_sink.cpp
  src/joystick.cpp
  src/osd.cpp
  src/ps2.cpp
  src/savestate_engine.cpp
  src/spi_block_decoder.cpp
  src/spi_block_poll.cpp
  src/spi_conf_str_decoder.cpp
  src/spi_fio_queue.cpp
  src/spi_identify_decoder.cpp
  src/spi_osd_mask_decoder.cpp
  src/spi_sampler.cpp
  src/spi_status_decoder.cpp
  src/status_register.cpp
)
target_include_directories(mister_proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_proto
  PUBLIC mister_reactor
  PRIVATE tasty_options)

tasty_add_header_checks(tasty_proto_header_checks LIB mister_proto HEADERS
  proto/bit_field.h
  proto/block_geometry.h
  proto/block_geometry_hook.h
  proto/block_slots.h
  proto/late_answers.h
  proto/conf_str.h
  proto/core_session.h
  proto/download_session.h
  proto/frame_count.h
  proto/frame_counter_read.h
  proto/image_bracket.h
  proto/image_sink.h
  proto/image_source.h
  proto/resident_image_source.h
  proto/item_table.h
  proto/link_event.h
  proto/link_op.h
  proto/link_port.h
  proto/decoder_row.h
  proto/session_live.h
  proto/link_router.h
  proto/save_ask.h
  proto/joystick.h
  proto/osd_surface.h
  proto/osd_target.h
  proto/osd_transport.h
  proto/ps2_keyboard.h
  proto/ps2_mouse.h
  proto/ps2_frame.h
  proto/mailbox_act.h
  proto/mailbox_frame.h
  proto/mailbox_frame_decl.h
  proto/mailbox_poll.h
  proto/ps2_wire.h
  proto/save_request.h
  proto/save_state_decl.h
  proto/savestate_engine.h
  proto/sd_request.h
  proto/session_params.h
  proto/slot_attributes.h
  proto/slot_roles.h
  proto/spi_block_decoder.h
  proto/spi_block_poll.h
  proto/spi_conf_str_decoder.h
  proto/spi_fio_queue.h
  proto/fio_window.h
  proto/spi_image_sink.h
  proto/spi_identify_decoder.h
  proto/spi_osd_mask_decoder.h
  proto/osd_focus.h
  proto/spi_sampler.h
  proto/spi_status_decoder.h
  proto/status_register.h
  proto/status_word.h
  proto/status_cell.h
  proto/storage_channel.h
  proto/storage_completion.h
  proto/storage_request.h
  proto/types.h
  proto/volume_cmd.h
  proto/rotation_dir.h
)

if(TASTY_BUILD_TESTS)
  get_filename_component(TASTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
  set(TASTY_TESTS_DIR "${TASTY_ROOT}/tests")

  function(tasty_proto_suite tasty_name)
    cmake_parse_arguments(A "HARNESS;REPLAY" "SOURCE;TEST;TIMEOUT" "" ${ARGN})
    if(NOT A_SOURCE)
      set(A_SOURCE "tests/${tasty_name}_unit.cpp")
    endif()
    if(NOT A_TEST)
      set(A_TEST "tasty.proto_${tasty_name}")
    endif()
    if(NOT A_TIMEOUT)
      set(A_TIMEOUT 120)
    endif()

    set(tasty_srcs "${A_SOURCE}")
    if(A_HARNESS)
      list(APPEND tasty_srcs "${TASTY_TESTS_DIR}/support/fakecore_transport.cpp")
    endif()

    tasty_unit_test(tasty_proto_${tasty_name}_unit
      NAME ${A_TEST}
      SOURCES ${tasty_srcs}
      INCLUDES ${TASTY_TESTS_DIR}
      DEFINES TASTY_CORPORA_DIR="${TASTY_ROOT}/../harness/corpora"
        TASTY_CAPTURE_DIR="${TASTY_ROOT}/../harness/capture/captures"
        TASTY_PROTO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        TASTY_BLK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
      LIBS mister_proto tasty_test_options
      TIMEOUT ${A_TIMEOUT})
    if(A_HARNESS)
      target_link_libraries(tasty_proto_${tasty_name}_unit PRIVATE fakecore)
    endif()
    if(A_REPLAY)
      target_link_libraries(tasty_proto_${tasty_name}_unit PRIVATE trace_replay fakecore)
    endif()
  endfunction()

  foreach(tasty_mod IN ITEMS block_slots decoder_row download_session joystick link link_port osd ps2 save_request savestate spi_block_decoder spi_block_poll spi_conf_str_decoder spi_fio_queue spi_identify_decoder spi_osd_mask_decoder spi_sampler spi_status_decoder status_register)
    tasty_proto_suite(${tasty_mod})
  endforeach()

  tasty_proto_suite(conf_str SOURCE tests/confstr_parity.cpp)
  tasty_proto_suite(conf_str_items SOURCE tests/conf_str_items.cpp
                 TEST tasty.proto_conf_str_items)

  tasty_proto_suite(core_session SOURCE tests/core_session_unit.cpp HARNESS)
  tasty_proto_suite(frame_counter_read HARNESS)

  tasty_proto_suite(block_slots_parity  SOURCE tests/block_slots_parity.cpp
                 TEST tasty.proto_block_slots_parity  HARNESS REPLAY)
  tasty_proto_suite(core_session_parity SOURCE tests/core_session_parity.cpp
                 TEST tasty.proto_core_session_parity HARNESS REPLAY)
  tasty_proto_suite(osd_parity          SOURCE tests/osd_capture_parity.cpp
                 TEST tasty.proto_osd_parity          REPLAY)
endif()
