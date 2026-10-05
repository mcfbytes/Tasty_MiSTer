add_library(mister_os STATIC
  src/bt_probe.cpp
  src/child_process.cpp
  src/clock.cpp
  src/i2c_device.cpp
  src/kernel_rt.cpp
  src/mmio.cpp
  src/net_probe.cpp
  src/run_sync.cpp
  src/small_file.cpp
  src/uinput_keyboard.cpp
  src/vt_console.cpp
  src/vm_compaction.cpp
)
target_include_directories(mister_os PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_os
  PUBLIC mister_infra
  PRIVATE tasty_options)

tasty_add_header_checks(tasty_os_header_checks LIB mister_os HEADERS
  os/bt_probe.h
  os/child_process.h
  os/clock.h
  os/deadline.h
  os/delay.h
  os/i2c_device.h
  os/kernel_rt.h
  os/kernel_rt_probe.h
  os/key_injector.h
  os/linux_vt_console.h
  os/mmio_region.h
  os/monotonic_clock.h
  os/nanosleep_delay.h
  os/net_probe.h
  os/run_sync.h
  os/small_file.h
  os/thread_cpu_clock.h
  os/types.h
  os/uio_handle.h
  os/uinput_keyboard.h
  os/vt_console.h
  os/vm_compaction.h
  os/writeback_probe.h
)

if(TASTY_BUILD_TESTS)

  foreach(mod IN ITEMS child_process clock i2c_device mmio run_sync)
    add_executable(tasty_os_${mod}_unit tests/${mod}_unit.cpp)
    target_link_libraries(tasty_os_${mod}_unit PRIVATE mister_os tasty_test_options)
    add_test(NAME tasty.os_${mod} COMMAND tasty_os_${mod}_unit)
    set_tests_properties(tasty.os_${mod} PROPERTIES TIMEOUT 60)
  endforeach()
endif()
