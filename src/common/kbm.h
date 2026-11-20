// Keyboard and mouse as controller 1 (Windows build; the other builds get empty stand ins)
#pragma once
#include <cstdint>

#include "core/input.h"

namespace kbm {
void Load(const void* table);  // settings.toml's [keyboard_mouse]
void Save(void* table);
void Window(void* hwnd);  // the game window is up: raw mouse input
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
void Pad(int port, input::Pad& pad);  // merged into a polled pad
}  // namespace kbm
