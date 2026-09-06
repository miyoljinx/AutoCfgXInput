cmake_minimum_required(VERSION 3.15)
project(BassBoosterPlugin CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Download or include MinHook
include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
)
FetchContent_MakeAvailable(minhook)

add_library(BassBooster SHARED bass_booster.cpp)

set_target_properties(BassBooster PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "BassBooster"
)

target_link_libraries(BassBooster PRIVATE minhook dsound winmm)
target_link_options(BassBooster PRIVATE -static -static-libgcc -static-libstdc++)
