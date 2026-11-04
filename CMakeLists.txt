cmake_minimum_required(VERSION 3.16)
project(tinynet VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "tinynet uses epoll/eventfd/timerfd and only builds on Linux (use WSL on Windows).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TINYNET_BUILD_TESTS "Build unit tests" ON)
option(TINYNET_BUILD_EXAMPLES "Build example servers" ON)
option(TINYNET_BUILD_BENCHMARKS "Build benchmark clients" ON)
set(TINYNET_SANITIZER "" CACHE STRING "Enable a sanitizer: address, thread, or undefined")

include(cmake/Dependencies.cmake)

add_library(tinynet
    src/Buffer.cpp
    src/Channel.cpp
    src/EpollPoller.cpp
    src/EventLoop.cpp
    src/EventLoopThread.cpp
    src/EventLoopThreadPool.cpp
    src/InetAddress.cpp
    src/Poller.cpp
    src/Socket.cpp
    src/TcpConnection.cpp
    src/TcpServer.cpp
    src/Timer.cpp
)
add_library(tinynet::tinynet ALIAS tinynet)
target_include_directories(tinynet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tinynet PUBLIC Threads::Threads)
target_compile_options(tinynet PRIVATE -Wall -Wextra -Wpedantic -Wshadow)

if(TINYNET_SANITIZER)
    add_compile_options(-fsanitize=${TINYNET_SANITIZER} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${TINYNET_SANITIZER})
    target_compile_options(tinynet PUBLIC -fsanitize=${TINYNET_SANITIZER} -fno-omit-frame-pointer)
    target_link_options(tinynet PUBLIC -fsanitize=${TINYNET_SANITIZER})
endif()

if(TINYNET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TINYNET_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(TINYNET_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
