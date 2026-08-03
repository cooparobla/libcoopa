cmake_minimum_required(VERSION 3.0)
cmake_policy(VERSION 3.0)

project(trav)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(ROOT_DIR_PARENT "${CMAKE_SOURCE_DIR}" DIRECTORY)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/includes/)
include_directories(${CMAKE_SOURCE_DIR})

# Configure and include root directory header
configure_file(configuration/root_directory.h.in configuration/root_directory.h)
include_directories(${CMAKE_BINARY_DIR}/configuration)

# Add executable
add_executable(trav coopa/test.cpp)