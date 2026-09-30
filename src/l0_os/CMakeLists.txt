add_library(mister_os STATIC
  src/bt_probe.cpp
  src/clock.cpp
  src/i2c_device.cpp
  src/kernel_rt.cpp
  src/mmio.cpp
  src/net_probe.cpp
  src/run_sync.cpp
  src/small_file.cpp
  src/vm_compaction.cpp
)
target_include_directories(mister_os PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_os
  PUBLIC mister_infra
  PRIVATE tasty_options)

tasty_add_header_checks(tasty_os_header_checks LIB mister_os HEADERS
  os/bt_probe.h
  os/clock.h
  os/deadline.h
  os/delay.h
  os/i2c_device.h
  os/kernel_rt.h
  os/kernel_rt_probe.h
  os/mmio_region.h
  os/monotonic_clock.h
  os/nanosleep_delay.h
  os/net_probe.h
  os/run_sync.h
  os/small_file.h
  os/thread_cpu_clock.h
  os/types.h
  os/uio_handle.h
  os/vm_compaction.h
)

if(TASTY_BUILD_TESTS)

  foreach(mod IN ITEMS clock i2c_device mmio run_sync)
    add_executable(tasty_os_${mod}_unit tests/${mod}_unit.cpp)
    target_link_libraries(tasty_os_${mod}_unit PRIVATE mister_os tasty_test_options)
    add_test(NAME tasty.os_${mod} COMMAND tasty_os_${mod}_unit)
    set_tests_properties(tasty.os_${mod} PROPERTIES TIMEOUT 60)
  endforeach()
endif()
