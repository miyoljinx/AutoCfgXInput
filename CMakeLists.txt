cmake_minimum_required(VERSION 3.15)
project(BassBoosterPlugin CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force MinHook to build static library
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Fetch MinHook
include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
)
FetchContent_GetProperties(minhook)
if(NOT minhook_POPULATED)
    FetchContent_Populate(minhook)
    add_subdirectory(${minhook_SOURCE_DIR} ${minhook_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Create BassBooster target
add_library(BassBooster SHARED bass_booster.cpp)

set_target_properties(BassBooster PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "BassBooster"
)

# Include directories
target_include_directories(BassBooster PRIVATE ${minhook_SOURCE_DIR}/include)

# Link directly to the generated MinHook static library target
target_link_libraries(BassBooster PRIVATE $<TARGET_FILE:MinHook> winmm ole32)

# Statically link C/C++ runtimes to avoid ERROR_MOD_NOT_FOUND (126) in Wine
target_link_options(BassBooster PRIVATE -static -static-libgcc -static-libstdc++)
