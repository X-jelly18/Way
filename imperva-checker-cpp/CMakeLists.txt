cmake_minimum_required(VERSION 3.16)
project(imperva_checker CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_executable(imperva_checker src/main.cpp)
target_link_libraries(imperva_checker PRIVATE CURL::libcurl Threads::Threads)

if(NOT MSVC)
  target_compile_options(imperva_checker PRIVATE -Wall -Wextra -O2)
endif()
