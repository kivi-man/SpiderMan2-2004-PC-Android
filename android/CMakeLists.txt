# Android: libmain.so with the game's recompiled code, the SDK runtime without its Windows parts, the GL renderer on
# OpenGL ES 3.2, the port's hooks, this folder's Android layer and SDL3. Built by Gradle (app/build.gradle) per ABI.
cmake_minimum_required(VERSION 3.20)
project(SpiderMan2Android C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(REPO ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
set(GEN ${REPO}/generated/game)
if(NOT EXISTS ${GEN}/x86_func_mapping.cpp)
    message(FATAL_ERROR "no recompiled code in ${GEN}: build the Windows or Linux version first (it runs xbe_recomp on assets/default.xbe)")
endif()

# SDL3, static into libmain.so: the Windows build's copy when it is there
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
if(EXISTS ${REPO}/build/_deps/sdl3-src/CMakeLists.txt)
    add_subdirectory(${REPO}/build/_deps/sdl3-src sdl3 EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.16 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

file(GLOB RUNTIME CONFIGURE_DEPENDS ${REPO}/sdk/runtime/*/*.cpp)
list(FILTER RUNTIME EXCLUDE REGEX "/runtime/app/|/core/input\\.cpp$|/gpu/d3d12\\.cpp$|/audio/xaudio2\\.cpp$")
file(GLOB PORT CONFIGURE_DEPENDS ${REPO}/src/*/*.cpp ${REPO}/gl/*.cpp ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
list(APPEND PORT ${REPO}/sdk/harness/reference_renderer.cpp)  # the GL renderer borrows its screen boxes
file(GLOB GENERATED CONFIGURE_DEPENDS ${GEN}/*.cpp)

# the xbe this was made from: the game found on the phone must be the same
file(SHA1 ${REPO}/assets/default.xbe XBE_SHA1)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/xbe_sha1.cpp
     "extern const char kImageSha1[] = \"${XBE_SHA1}\";\nextern const char kImageCode[] = \"\";\n")

# the recompiled code alone, at -O2 whatever the build type: it is most of the time spent
add_library(game_code OBJECT ${GENERATED})
target_include_directories(game_code PRIVATE ${REPO}/sdk/runtime ${GEN})
target_compile_options(game_code PRIVATE -O2 -fno-strict-aliasing -Wno-unused-label -Wno-parentheses-equality
                       -Wno-unused-variable -Wno-everything)

add_library(main SHARED ${RUNTIME} ${PORT} $<TARGET_OBJECTS:game_code> ${CMAKE_CURRENT_BINARY_DIR}/xbe_sha1.cpp)
target_include_directories(main PRIVATE ${REPO}/src ${REPO}/sdk/runtime ${REPO}/sdk/harness ${GEN}
                           ${CMAKE_CURRENT_LIST_DIR}/third_party ${REPO}/sdk/harness/compat)
# as the Switch build: vertices converted where GL cannot read them raw, the render thread chosen at run time
target_compile_definitions(main PRIVATE XBE_GLES=1 XBE_RAW_VERTICES=0 XBE_RENDER_THREAD=0)
target_compile_options(main PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/src/compat.h)
# the repository's root for "gl/..." and "android/..." only: its VERSION file would stand in for <version> on a
# case-insensitive file system
target_compile_options(main PRIVATE -O2 -fno-strict-aliasing -Wno-unused-variable "-iquote${REPO}")
# the full log, as the Switch build writes it (NDEBUG keeps only errors)
target_compile_options(main PRIVATE -UNDEBUG)
target_link_libraries(main PRIVATE SDL3::SDL3-static GLESv3 EGL android log)
