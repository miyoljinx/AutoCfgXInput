cmake_minimum_required(VERSION 3.15)
project(BassBoosterPlugin CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch MinHook from GitHub
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

# Include MinHook headers and link static library
target_include_directories(BassBooster PRIVATE ${minhook_SOURCE_DIR}/include)
target_link_libraries(BassBooster PRIVATE minhook winmm)

# Statically link C/C++ runtimes to avoid missing DLL errors in Wine
target_link_options(BassBooster PRIVATE -static -static-libgcc -static-libstdc++)
