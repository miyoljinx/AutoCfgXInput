cmake_minimum_required(VERSION 3.16)
include(FetchContent)

project(BassBoosterPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4
)
FetchContent_MakeAvailable(minhook)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")

add_library(BassBooster SHARED bass_booster.cpp)

target_link_libraries(BassBooster PRIVATE
    minhook
    winmm
    ole32
)

# Output as BassBooster.asi instead of .dll
set_target_properties(BassBooster PROPERTIES
    PREFIX ""
    OUTPUT_NAME "BassBooster"
    SUFFIX ".asi"
)
