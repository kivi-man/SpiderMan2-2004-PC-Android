#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps3 {

bool InitScreen();
// a frame, RGBA8 with red in the low byte, top row first
void Show(const uint32_t* rgba, uint32_t w, uint32_t h);

// the folder the port keeps its logs, hard disk and saves in, and finds the game in (ps3/main.cpp)
extern std::filesystem::path g_dir;

}  // namespace ps3
