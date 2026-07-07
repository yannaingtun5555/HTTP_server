cmake_minimum_required(VERSION 3.10)

project(HTTP_Server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Default to Release build with aggressive optimizations
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native -flto)
    add_link_options(-flto)
endif()

# Suppress CMP0167 warning for Boost
cmake_policy(SET CMP0115 NEW)

# ── Boost ────────────────────────────────────────────────
# Use system-installed Boost (dnf install boost-devel boost-log ...)
find_package(Boost REQUIRED COMPONENTS log log_setup system thread filesystem)

# ── JsonCpp ──────────────────────────────────────────────
# Use system-installed JsonCpp (dnf install jsoncpp-devel)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# ── Threads ──────────────────────────────────────────────
find_package(Threads REQUIRED)

# ── Executable ───────────────────────────────────────────
add_executable(HTTP_Server
    main.cpp
    Configuration/config_parser.cpp
    Error_handling/error_handling.cpp
    Core/connection.cpp
    Core/request_handler.cpp
    Core/session.cpp
    Routing/routing.cpp
    File_Management/file_handler.cpp
    Core/response_generator.cpp
    Thread/thread.cpp
    Monitoring/monitoring.cpp
    Core/global.cpp
    Proxy/proxy_handler.cpp
)

# ── Include directories ──────────────────────────────────
target_include_directories(HTTP_Server PRIVATE
    Configuration
    Error_handling
    Core
    Routing
    File_Management
    Thread
    Monitoring
    Proxy
    ${Boost_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
)

# ── Link libraries ───────────────────────────────────────
target_link_libraries(HTTP_Server
    ${Boost_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    Threads::Threads
)
