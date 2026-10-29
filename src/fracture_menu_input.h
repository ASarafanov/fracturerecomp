// fracture - controller input for the settings screen.
//
// Every controller read the game makes (XamInputGetState) goes through a
// wrapper here. That lets the settings screen see the pad exactly as the game
// would, hide it from the game while the screen is open, and spot the menu
// chord, without extra polls of the input drivers (an extra poll would use up
// the SDL driver's "state changed" flag and leave the game a stale packet
// number).

#pragma once

#include <rex/ppc/context.h>

#include <cstdint>
#include <functional>

namespace fracture::menu_input {

// The game's XamInputGetState: the game DLL forwards every call here
// (src/game/game_api.h). Runs on guest threads.
void GuestXamInputGetState(PPCContext& ctx, uint8_t* base);

// X_INPUT_GAMEPAD_* button bits.
inline constexpr uint16_t kDpadUp = 0x0001;
inline constexpr uint16_t kDpadDown = 0x0002;
inline constexpr uint16_t kDpadLeft = 0x0004;
inline constexpr uint16_t kDpadRight = 0x0008;
inline constexpr uint16_t kStart = 0x0010;
inline constexpr uint16_t kBack = 0x0020;
inline constexpr uint16_t kLeftThumb = 0x0040;
inline constexpr uint16_t kRightThumb = 0x0080;
inline constexpr uint16_t kLeftShoulder = 0x0100;
inline constexpr uint16_t kRightShoulder = 0x0200;
inline constexpr uint16_t kA = 0x1000;
inline constexpr uint16_t kB = 0x2000;
inline constexpr uint16_t kX = 0x4000;
inline constexpr uint16_t kY = 0x8000;

struct Pad {
  uint16_t buttons = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
};

// Reads the fracture_menu_chord cvar and follows its changes. `on_chord` runs
// on a guest thread whenever the chord goes down, open or closed.
void Init(std::function<void()> on_chord);

// While open, the game reads an untouched controller. Buttons still held when
// the screen closes stay hidden from the game until released, so the B that
// closed it does not also act in the game.
void SetMenuOpen(bool open);

// The buttons of the fracture_menu_chord cvar (0 for "none").
uint16_t ChordMask();

// All controllers merged, as the game last read them (unfiltered). Zero when
// the game has not read a controller for a moment (loading screens), so a
// held direction cannot keep repeating.
Pad LatestPad();

}  // namespace fracture::menu_input
