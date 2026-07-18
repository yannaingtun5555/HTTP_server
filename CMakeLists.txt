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

# ── Boost ────────────────────────────────────────────────────
find_package(Boost REQUIRED COMPONENTS log log_setup system thread filesystem)

# ── JsonCpp ──────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# ── libpqxx (C++ PostgreSQL client) ──────────────────────────
pkg_check_modules(PQXX REQUIRED libpqxx)

# ── libcurl (for K8s REST API calls) ─────────────────────────
find_package(CURL REQUIRED)

# ── Threads ──────────────────────────────────────────────────
find_package(Threads REQUIRED)

# ── Executable ───────────────────────────────────────────────
add_executable(HTTP_Server
    main.cpp

    # Original modules
    Configuration/config_parser.cpp
    Error_handling/error_handling.cpp
    Core/connection.cpp
    Core/request_handler.cpp
    Core/session.cpp
    Core/response_generator.cpp
    Core/global.cpp
    Routing/routing.cpp
    File_Management/file_handler.cpp
    Thread/thread.cpp
    Monitoring/monitoring.cpp
    Proxy/proxy_handler.cpp

    # New modules
    Configuration/server_config.cpp
    Configuration/site_config.cpp
    DB/server_db.cpp
    K8s/k8s_controller.cpp
    FrontEnd/fe_builder.cpp
    Core/internal_api/internal_api.cpp
)

# ── Include directories ──────────────────────────────────────
target_include_directories(HTTP_Server PRIVATE
    Configuration
    Error_handling
    Core
    Core/internal_api
    Routing
    File_Management
    Thread
    Monitoring
    Proxy
    DB
    K8s
    FrontEnd
    ${Boost_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
    ${PQXX_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

# ── Link libraries ───────────────────────────────────────────
target_link_libraries(HTTP_Server
    ${Boost_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    ${PQXX_LIBRARIES}
    ${CURL_LIBRARIES}
    Threads::Threads
)

# ── Test executable ──────────────────────────────────────────
add_executable(test_k8s_controller
    K8s/test_k8s_controller.cpp
    K8s/k8s_controller.cpp
    Configuration/site_config.cpp
    Configuration/config_parser.cpp
    DB/server_db.cpp
)

target_include_directories(test_k8s_controller PRIVATE
    Configuration
    DB
    K8s
    ${JSONCPP_INCLUDE_DIRS}
    ${PQXX_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(test_k8s_controller
    ${JSONCPP_LIBRARIES}
    ${PQXX_LIBRARIES}
    ${CURL_LIBRARIES}
    Threads::Threads
)

