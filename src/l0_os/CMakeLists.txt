add_library(mister_os STATIC
  src/anon_mapping.cpp
  src/block_device.cpp
  src/bt_probe.cpp
  src/child_process.cpp
  src/clock.cpp
  src/deadline.cpp
  src/evdev.cpp
  src/framebuffer.cpp
  src/i2c_device.cpp
  src/kernel_rt_probe.cpp
  src/mmio_region.cpp
  src/monotonic_clock.cpp
  src/nanosleep_delay.cpp
  src/net_probe.cpp
  src/process_title.cpp
  src/run_sync.cpp
  src/small_file.cpp
  src/stack_touch.cpp
  src/task_sched.cpp
  src/thread_cpu_clock.cpp
  src/uinput_keyboard.cpp
  src/uio_handle.cpp
  src/uio_line_ref.cpp
  src/vm_compaction.cpp
  src/vm_status.cpp
  src/vt_console.cpp
  src/write_all.cpp
)
target_include_directories(mister_os PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mister_os
  PUBLIC mister_infra
  PRIVATE tasty_options)

tasty_add_header_checks(tasty_os_header_checks LIB mister_os HEADERS
  os/anon_mapping.h
  os/block_device.h
  os/bt_probe.h
  os/child_process.h
  os/clock.h
  os/deadline.h
  os/delay.h
  os/evdev.h
  os/framebuffer.h
  os/i2c_device.h
  os/kernel_rt.h
  os/kernel_rt_probe.h
  os/kernel_tid.h
  os/key_injector.h
  os/linux_vt_console.h
  os/ui_vt_console.h
  os/map_length.h
  os/mmio_region.h
  os/monotonic_clock.h
  os/nanosleep_delay.h
  os/net_probe.h
  os/path_buf.h
  os/process_title.h
  os/raw_monotonic_clock.h
  os/run_sync.h
  os/small_file.h
  os/stack_touch.h
  os/task_sched.h
  os/thread_cpu_clock.h
  os/types.h
  os/uio_handle.h
  os/uio_line_ref.h
  os/uio_line_space.h
  os/uinput_keyboard.h
  os/vt_console.h
  os/vm_compaction.h
  os/vm_status.h
  os/write_all.h
  os/writeback_paths.h
  os/writeback_probe.h
)

