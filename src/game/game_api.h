// fracture - the interface between fracture.exe and the recompiled code of
// one release of the game.
//
// Each supported default.xex has its recompiled code, and the hooks written
// for its addresses, in a DLL of its own next to fracture.exe
// (fracture_v1.dll, fracture_v2.dll): the generated functions of both are
// named sub_<guest address> and could not share one binary. fracture.exe
// (install wizard, settings, everything else) loads the one that matches the
// game files before it creates the app, because the SDK takes the image
// layout in ReXApp's constructor.

#pragma once

#include <rex/image_info.h>
#include <rex/ppc/context.h>

#include <cstdint>

namespace fracture::game {

inline constexpr uint32_t kApiVersion = 1;

// What fracture.exe gives the DLL. Both are called on guest threads.
struct HostApi {
  uint32_t api_version = kApiVersion;
  // The game's XamInputGetState, through the settings screen's filter
  // (fracture_menu_input.h).
  void (*xam_input_get_state)(PPCContext& ctx, uint8_t* base) = nullptr;
  // fracture::frame_rate::Interval().
  uint32_t (*frame_interval)() = nullptr;
};

// Exported by each game DLL (extern "C").
using ImageInfoFunc = const rex::PPCImageInfo* (*)();
using SetHostFunc = bool (*)(const HostApi* host);
inline constexpr const char* kImageInfoExport = "FractureGame_ImageInfo";
inline constexpr const char* kSetHostExport = "FractureGame_SetHost";

}  // namespace fracture::game
