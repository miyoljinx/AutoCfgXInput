cmake_minimum_required(VERSION 3.15)
project(WineXInputAutoOverride CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(WineXInputAutoOverride SHARED wine_xinput_override.cpp)

# Output directly as a .asi file without 'lib' prefix
set_target_properties(WineXInputAutoOverride PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "WineXInputAutoOverride"
)

target_link_libraries(WineXInputAutoOverride PRIVATE advapi32)
