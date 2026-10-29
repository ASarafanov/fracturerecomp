// fracture - controller input for the settings screen.

#include "fracture_menu_input.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

REXCVAR_DEFINE_STRING(fracture_menu_chord, "rb+start", "Fracture",
                      "Controller buttons that open and close the settings screen: rb+start, "
                      "lb+rb+start, back+start, l3+r3 or none")
    .allowed({"rb+start", "lb+rb+start", "back+start", "l3+r3", "none"});

namespace fracture::menu_input {
namespace {

using Clock = std::chrono::steady_clock;

// A controller the game stopped reading this long ago counts as released.
constexpr auto kStaleAfter = std::chrono::milliseconds(250);
constexpr uint32_t kErrorDeviceNotConnected = 0x48F;
constexpr size_t kGamepadOffset = 4;  // X_INPUT_STATE: be32 packet_number first
constexpr size_t kGamepadSize = 12;   // X_INPUT_GAMEPAD

struct Slot {
  Pad pad;
  Clock::time_point time{};  // last successful read; default = never
  bool chord_down = false;
  uint16_t hidden = 0;  // buttons hidden from the game until released
};

std::mutex g_mutex;
std::array<Slot, 4> g_slots;
std::atomic<bool> g_menu_open{false};
std::atomic<uint16_t> g_chord_mask{0};
std::function<void()> g_on_chord;

uint16_t ChordMask(std::string_view spec) {
  if (spec == "rb+start") {
    return kRightShoulder | kStart;
  }
  if (spec == "lb+rb+start") {
    return kLeftShoulder | kRightShoulder | kStart;
  }
  if (spec == "back+start") {
    return kBack | kStart;
  }
  if (spec == "l3+r3") {
    return kLeftThumb | kRightThumb;
  }
  return 0;
}

uint16_t LoadBE16(const uint8_t* p) {
  return uint16_t((p[0] << 8) | p[1]);
}

void StoreBE16(uint8_t* p, uint16_t value) {
  p[0] = uint8_t(value >> 8);
  p[1] = uint8_t(value);
}

// Runs right after the SDK filled the game's X_INPUT_STATE.
void FilterGuestState(uint32_t user, uint8_t* state) {
  uint8_t* gamepad = state + kGamepadOffset;
  if (user >= g_slots.size()) {
    if (g_menu_open.load(std::memory_order_relaxed)) {
      std::memset(gamepad, 0, kGamepadSize);
    }
    return;
  }
  const Pad pad{LoadBE16(gamepad), int16_t(LoadBE16(gamepad + 4)),
                int16_t(LoadBE16(gamepad + 6))};
  const uint16_t chord = g_chord_mask.load(std::memory_order_relaxed);
  bool fire = false;
  bool hide_all = false;
  uint16_t hidden = 0;
  {
    std::lock_guard lock(g_mutex);
    Slot& slot = g_slots[user];
    slot.pad = pad;
    slot.time = Clock::now();
    const bool chord_down = chord != 0 && (pad.buttons & chord) == chord;
    fire = chord_down && !slot.chord_down;
    slot.chord_down = chord_down;
    // The chord itself never reaches the game either (Start would pause it).
    hide_all = g_menu_open.load(std::memory_order_relaxed) || chord_down;
    slot.hidden = hide_all ? pad.buttons : uint16_t(slot.hidden & pad.buttons);
    hidden = slot.hidden;
  }
  if (hide_all) {
    std::memset(gamepad, 0, kGamepadSize);
  } else if (hidden) {
    StoreBE16(gamepad, pad.buttons & ~hidden);
  }
  if (fire && g_on_chord) {
    g_on_chord();
  }
}

void MarkDisconnected(uint32_t user) {
  if (user < g_slots.size()) {
    std::lock_guard lock(g_mutex);
    g_slots[user] = {};
  }
}

using GuestFunction = void (*)(PPCContext&, uint8_t*);

// The SDK's own XamInputGetState, looked up by its export name in whichever
// runtime DLL is loaded (rexruntime.dll, rexruntimerd.dll, ... per build type).
GuestFunction FindOriginal() {
#if defined(_WIN32)
  HMODULE modules[512];
  DWORD needed = 0;
  if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
    return nullptr;
  }
  const HMODULE self = GetModuleHandleW(nullptr);
  const DWORD count = std::min<DWORD>(needed / sizeof(HMODULE), DWORD(std::size(modules)));
  for (DWORD i = 0; i < count; ++i) {
    if (modules[i] == self) {
      continue;
    }
    if (FARPROC function = GetProcAddress(modules[i], "__imp__XamInputGetState")) {
      return reinterpret_cast<GuestFunction>(function);
    }
  }
#endif
  return nullptr;
}


}  // namespace

void Init(std::function<void()> on_chord) {
  g_on_chord = std::move(on_chord);
  g_chord_mask.store(ChordMask(REXCVAR_GET(fracture_menu_chord)));
  rex::cvar::RegisterChangeCallback("fracture_menu_chord",
                                    [](std::string_view, std::string_view value) {
                                      g_chord_mask.store(ChordMask(value));
                                    });
}

uint16_t ChordMask() {
  return g_chord_mask.load(std::memory_order_relaxed);
}

void SetMenuOpen(bool open) {
  g_menu_open.store(open);
}

Pad LatestPad() {
  const auto now = Clock::now();
  Pad merged;
  std::lock_guard lock(g_mutex);
  for (const Slot& slot : g_slots) {
    if (slot.time == Clock::time_point{} || now - slot.time > kStaleAfter) {
      continue;
    }
    merged.buttons |= slot.pad.buttons;
    if (std::abs(slot.pad.thumb_lx) > std::abs(merged.thumb_lx)) {
      merged.thumb_lx = slot.pad.thumb_lx;
    }
    if (std::abs(slot.pad.thumb_ly) > std::abs(merged.thumb_ly)) {
      merged.thumb_ly = slot.pad.thumb_ly;
    }
  }
  return merged;
}

}  // namespace fracture::menu_input

// Replaces the SDK's XamInputGetState for the game: generated code calls this
// symbol directly (it is not dllimport), so a definition here wins over the
// import library. The real one does the work; the result is then filtered.
// Guest arguments: r3 = user index, r4 = flags, r5 = X_INPUT_STATE*.
extern "C" REX_FUNC(__imp__XamInputGetState) {
  static const auto original = [] {
    const auto function = fracture::menu_input::FindOriginal();
    if (function) {
      REXLOG_DEBUG("Menu input: filtering the game's XamInputGetState");
    } else {
      REXLOG_ERROR("Menu input: the SDK's XamInputGetState was not found");
    }
    return function;
  }();
  if (!original) {
    ctx.r3.u64 = fracture::menu_input::kErrorDeviceNotConnected;
    return;
  }
  const uint32_t user = ctx.r3.u32;
  const uint32_t state = ctx.r5.u32;
  original(ctx, base);
  if (ctx.r3.u32 != 0 || state == 0) {
    fracture::menu_input::MarkDisconnected(user);
    return;
  }
  fracture::menu_input::FilterGuestState(user, base + state);
}
