cmake_minimum_required(VERSION 3.15)
project(BassBoosterPlugin CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch MinHook directly from GitHub
include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
)
FetchContent_MakeAvailable(minhook)

# Define our plugin library
add_library(BassBooster SHARED bass_booster.cpp)

set_target_properties(BassBooster PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "BassBooster"
)

# Tell CMake to use MinHook's include headers and link its static library target
target_include_directories(BassBooster PRIVATE ${minhook_SOURCE_DIR}/include)
target_link_libraries(BassBooster PRIVATE minhook winmm ole32)

# Statically link runtime dependencies so Wine doesn't throw Error 126
target_link_options(BassBooster PRIVATE -static -static-libgcc -static-libstdc++)
