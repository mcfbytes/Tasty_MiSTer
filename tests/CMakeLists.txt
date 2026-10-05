add_executable(tasty_unit tasty_unit.cpp)
target_include_directories(tasty_unit PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/firmware)
target_link_libraries(tasty_unit PRIVATE mister_fw_tasty tasty_test_options)
target_include_directories(tasty_unit SYSTEM PRIVATE ${TASTY_MINIZIP_INCLUDE_DIR})
add_test(NAME tasty.unit COMMAND tasty_unit)
set_tests_properties(tasty.unit PROPERTIES TIMEOUT 60)
add_test(NAME tasty.compare COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/compare/test_compare.py")
set_tests_properties(tasty.compare PROPERTIES TIMEOUT 120)
