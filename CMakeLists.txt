cmake_minimum_required(VERSION 3.20)
project(FileCollector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FC_TSAN "Build with ThreadSanitizer" OFF)
if(FC_TSAN)
  add_compile_options(-fsanitize=thread -g)
  add_link_options(-fsanitize=thread)
endif()

include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)


add_library(file_collector src/IntervalSet.cpp src/FileCollector.cpp)
target_include_directories(file_collector PUBLIC include)
if(MSVC)
  target_compile_options(file_collector PRIVATE /W4 /permissive-)
else()
  target_compile_options(file_collector PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
find_package(Threads REQUIRED)
add_executable(tests tests/IntervalSetTest.cpp tests/FileCollectorTest.cpp tests/FileCollectorThreadsTest.cpp)
target_link_libraries(tests PRIVATE file_collector GTest::gtest_main Threads::Threads)
include(GoogleTest)
gtest_discover_tests(tests DISCOVERY_MODE PRE_TEST)