cmake_minimum_required(VERSION 3.16)
project(MFDP VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(ZLIB REQUIRED)
find_package(Threads)

set(MFDP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "MFDP root holding config/ and adapters/")

set(MFDP_INTERNAL_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core
  ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline
)

add_library(mfdp STATIC
  src/core/errors.cpp
  src/core/types.cpp
  src/core/languages.cpp
  src/core/tokens.cpp
  src/core/sha256.cpp
  src/core/json.cpp
  src/runtime/adapter.cpp
  src/runtime/config.cpp
  src/runtime/rpc.cpp
  src/pipeline/parse.cpp
  src/pipeline/resolve.cpp
  src/pipeline/builder.cpp
  src/pipeline/format.cpp
  src/pipeline/context.cpp
  src/api.cpp
)
target_include_directories(mfdp PUBLIC include PRIVATE ${MFDP_INTERNAL_INCLUDES})
target_link_libraries(mfdp PUBLIC ZLIB::ZLIB)
target_compile_definitions(mfdp PRIVATE MFDP_DEFAULT_ROOT="${MFDP_ROOT}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(mfdp PRIVATE -Wall -Wextra)
endif()

add_executable(mfdp_cli apps/cli/main.cpp)
set_target_properties(mfdp_cli PROPERTIES OUTPUT_NAME mfdp)
target_include_directories(mfdp_cli PRIVATE ${MFDP_INTERNAL_INCLUDES})
target_link_libraries(mfdp_cli PRIVATE mfdp)

add_executable(mfdp_bench apps/bench/main.cpp)
set_target_properties(mfdp_bench PROPERTIES OUTPUT_NAME mfdp-bench)
target_include_directories(mfdp_bench PRIVATE ${MFDP_INTERNAL_INCLUDES})
target_link_libraries(mfdp_bench PRIVATE mfdp)

add_executable(mfdp_example examples/api_usage.cpp)
target_link_libraries(mfdp_example PRIVATE mfdp)

include(CTest)
if(BUILD_TESTING)
  add_executable(mfdp_tests
    tests/test_main.cpp
    tests/test_core.cpp
    tests/test_write.cpp
    tests/test_more_languages.cpp
    tests/test_config.cpp
    tests/test_cli.cpp
  )
  target_link_libraries(mfdp_tests PRIVATE mfdp)
  target_include_directories(mfdp_tests PRIVATE ${MFDP_INTERNAL_INCLUDES})
  target_compile_definitions(mfdp_tests PRIVATE
    MFDP_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests"
    MFDP_CLI_PATH="$<TARGET_FILE:mfdp_cli>")
  add_dependencies(mfdp_tests mfdp_cli)
  add_test(NAME mfdp_tests COMMAND mfdp_tests)
  set_tests_properties(mfdp_tests PROPERTIES ENVIRONMENT "MFDP_ROOT=${MFDP_ROOT}")
endif()

install(TARGETS mfdp_cli mfdp_bench RUNTIME DESTINATION bin)
install(DIRECTORY include/mfdp DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY adapters/ DESTINATION share/mfdp/adapters)
install(DIRECTORY config/ DESTINATION share/mfdp/config)
install(TARGETS mfdp ARCHIVE DESTINATION lib)