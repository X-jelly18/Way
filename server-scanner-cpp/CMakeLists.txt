cmake_minimum_required(VERSION 3.16)
project(server_scanner CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_executable(server_scanner
  src/main.cpp
  src/color.cpp
  src/strutil.cpp
  src/config.cpp
  src/output.cpp
  src/concurrency.cpp
  src/netprobe.cpp
  src/http.cpp
  src/cidr.cpp
  src/scan.cpp
)
target_include_directories(server_scanner PRIVATE include)
target_link_libraries(server_scanner PRIVATE CURL::libcurl Threads::Threads)

if(NOT MSVC)
  target_compile_options(server_scanner PRIVATE -Wall -Wextra -O2)
endif()
