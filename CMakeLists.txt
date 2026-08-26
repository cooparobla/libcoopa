cmake_minimum_required(VERSION 3.20)
cmake_policy(VERSION 3.20)

project(libcoopa)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMAKE_CURRENT_SOURCE_DIR/CMAKE_CURRENT_BINARY_DIR, not CMAKE_SOURCE_DIR/
# CMAKE_BINARY_DIR: this file is now also pulled in via add_subdirectory()
# from gfxcoopa (and, once migrated, every other consumer), where
# CMAKE_SOURCE_DIR/CMAKE_BINARY_DIR refer to the *top-level* project's dirs,
# not libcoopa's own. Using the un-prefixed variables here was latent and
# harmless while libcoopa only ever built standalone; it broke the moment
# something else add_subdirectory'd it.
get_filename_component(ROOT_DIR_PARENT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

# Configure and include root directory header
configure_file(configuration/root_directory.h.in configuration/root_directory.h)

# --- Consumable target ---
# libcoopa is header-only (scene/asset/util layer, no Vulkan or windowing
# content whatsoever). This INTERFACE target lets gfxcoopa and its downstream
# consumers `target_link_libraries(... coopa::lib)` instead of re-deriving
# these include paths and defines by hand.
add_library(libcoopa_lib INTERFACE)
add_library(coopa::lib ALIAS libcoopa_lib)
target_compile_features(libcoopa_lib INTERFACE cxx_std_20)
target_include_directories(libcoopa_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}            # coopa/...
    ${CMAKE_CURRENT_SOURCE_DIR}/includes   # glm, fkYAML, phmap
    ${CMAKE_CURRENT_BINARY_DIR}/configuration)
target_compile_definitions(libcoopa_lib INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)

# Add executable
add_executable(libcoopa test.cpp)
target_link_libraries(libcoopa PRIVATE coopa::lib)