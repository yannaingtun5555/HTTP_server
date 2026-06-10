cmake_minimum_required(VERSION 3.10)

project(HTTP_Server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Ensure CMake can locate the proper Boost version
cmake_policy(SET CMP0115 NEW)


# Set paths for Boost 1.86 installation
set(BOOST_ROOT "/usr/local/boost_1_86_0")
set(Boost_INCLUDE_DIR "/usr/local/boost_1_86_0/include")
set(Boost_LIBRARY_DIR "/usr/local/boost_1_86_0/lib")

set(Boost_USE_STATIC_LIBS ON)
#set(Boost_USE_STATIC_RUNTIME ON)

# Find Boost libraries required for your project
find_package(Boost REQUIRED COMPONENTS log log_setup system thread)

find_package(Boost REQUIRED COMPONENTS log_setup)

set(JSONCPP_LIBRARIES /usr/local/lib/libjsoncpp.a)

# Find JsonCpp library
find_package(jsoncpp REQUIRED)

# Define the executable and its source files
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

# Specify include directories
target_include_directories(HTTP_Server PRIVATE 
    Configuration 
    Error_handling 
    Core 
    Routing   
    File_Management 
    Thread 
    Monitoring
    Proxy
    ${Boost_INCLUDE_DIRS}  # Include Boost headers
    ${JSONCPP_INCLUDE_DIRS} # Include JsonCpp headers
)

# Ensure CMake uses the specified Boost 1.86 library paths
link_directories(${Boost_LIBRARY_DIR})

# Link the required libraries to the executable
target_link_libraries(HTTP_Server 
    ${Boost_LIBRARIES}      # Link Boost libraries
    ${JSONCPP_LIBRARIES} 
)

#target_link_libraries(HTTP_Server ${Boost_LIBRARIES} ${JSONCPP_LIBRARIES})


# Ensure static linking of system libraries
set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
