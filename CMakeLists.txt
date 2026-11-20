# Windows build: the game's recompiled code with the SDK's own Windows runtime (Direct3D 12, XAudio2, SDL3).
# Needs Clang and LLD (LLVM for Windows), CMake and Ninja, and your own default.xbe in assets/.
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#   cmake --build build
cmake_minimum_required(VERSION 3.20)
project(SpiderMan2 CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(sdk/cmake/XbeRecomp.cmake)
file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/game/")
add_xbe_image(SpiderMan2 default.xbe game.toml 128 game)
# the renderer uses SSE4.1 intrinsics; M_PI needs _USE_MATH_DEFINES with the Windows CRT
target_compile_options(SpiderMan2 PRIVATE -msse4.2)
target_compile_definitions(SpiderMan2 PRIVATE _USE_MATH_DEFINES NOMINMAX)
