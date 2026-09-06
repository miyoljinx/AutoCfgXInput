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

# Collect MinHook source files directly
file(GLOB MINHOOK_SOURCES
    "${minhook_SOURCE_DIR}/src/*.c"
    "${minhook_SOURCE_DIR}/src/hde/*.c"
)

# Build BassBooster together with MinHook sources directly
add_library(BassBooster SHARED bass_booster.cpp ${MINHOOK_SOURCES})

set_target_properties(BassBooster PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "BassBooster"
)

# Include MinHook headers
target_include_directories(BassBooster PRIVATE 
    ${minhook_SOURCE_DIR}/include
    ${minhook_SOURCE_DIR}/src
    ${minhook_SOURCE_DIR}/src/hde
)

# Link Windows Multimedia system library
target_link_libraries(BassBooster PRIVATE winmm)

# Statically link runtime dependencies to avoid ERROR_MOD_NOT_FOUND (126) in Wine
target_link_options(BassBooster PRIVATE -static -static-libgcc -static-libstdc++)
