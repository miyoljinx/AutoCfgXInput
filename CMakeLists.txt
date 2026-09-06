cmake_minimum_required(VERSION 3.15)
project(BassBoosterPlugin CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(BassBooster SHARED bass_booster.cpp)

set_target_properties(BassBooster PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "BassBooster"
)

# Statically link runtime dependencies to avoid Error 126
target_link_options(BassBooster PRIVATE -static -static-libgcc -static-libstdc++)
