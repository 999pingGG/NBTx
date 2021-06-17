project(nbtx C)

cmake_minimum_required(VERSION 2.6)

option(NBTX_BUILD_EXAMPLES "Build NBTx examples and tests" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Output paths
set(EXECUTABLE_OUTPUT_PATH bin)

find_program(BASH_PROGRAM bash)
find_package(ZLIB REQUIRED)

ADD_LIBRARY(nbtx buffer.c
  nbtx_loading.c
  nbtx_parsing.c
  nbtx_treeops.c
  nbtx_util.c
)

target_include_directories(nbtx PRIVATE ${ZLIB_INCLUDE_DIRS})

if(NBTX_BUILD_EXAMPLES)
  ADD_EXECUTABLE(check check.c)
  ADD_EXECUTABLE(afl_check afl_check.c)
  ADD_EXECUTABLE(nbtxreader main.c)
  TARGET_LINK_LIBRARIES(check PRIVATE nbtx ZLIB::ZLIB)
  TARGET_LINK_LIBRARIES(afl_check PRIVATE nbtx ZLIB::ZLIB)
  TARGET_LINK_LIBRARIES(nbtxreader PRIVATE nbtx ZLIB::ZLIB)
  
  include(CTest)
  ADD_TEST(test_hello_world ${EXECUTABLE_OUTPUT_PATH}/check ${CMAKE_CURRENT_SOURCE_DIR}/testdata/hello_world.nbtx)
  ADD_TEST(test_simple_level ${EXECUTABLE_OUTPUT_PATH}/check ${CMAKE_CURRENT_SOURCE_DIR}/testdata/simple_level.nbtx)
  ADD_TEST(test_issue_13 ${EXECUTABLE_OUTPUT_PATH}/check ${CMAKE_CURRENT_SOURCE_DIR}/testdata/issue_13.nbtx)
  ADD_TEST(test_issue_18 ${EXECUTABLE_OUTPUT_PATH}/check ${CMAKE_CURRENT_SOURCE_DIR}/testdata/issue_18.nbtx)
  ADD_TEST(test_afl ${CMAKE_CURRENT_SOURCE_DIR}/afl_check.sh ${EXECUTABLE_OUTPUT_PATH}/afl_check)  
endif()
