option(TASTY_WERROR "Compile both contracts with -Werror" ON)
if(TASTY_WERROR)
  set(TASTY_WERROR_FLAG -Werror)
else()
  set(TASTY_WERROR_FLAG "")
endif()

add_library(tasty_options INTERFACE)
target_compile_options(tasty_options INTERFACE
  -D_FILE_OFFSET_BITS=64
  -D_TIME_BITS=64
  -Wall
  -Wextra
  ${TASTY_WERROR_FLAG}
  -Wshadow
  -Wconversion
  -Wsign-conversion
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Woverloaded-virtual
  -Wcast-qual
  -Wnull-dereference
  -Wformat=2
  -Wsuggest-override
  -Wextra-semi
  -Wdouble-promotion
  -fno-exceptions
  -fno-rtti
)

add_library(tasty_test_options INTERFACE)

target_include_directories(tasty_test_options INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/../tests)
target_compile_options(tasty_test_options INTERFACE
  -D_FILE_OFFSET_BITS=64
  -D_TIME_BITS=64
  -Wall
  -Wextra
  ${TASTY_WERROR_FLAG}
  -Wshadow
  -Wnon-virtual-dtor
  -Wsuggest-override
  -Wextra-semi
)
