// fracture - the exports of a game DLL (fracture_v1.dll, fracture_v2.dll)
// and what its recompiled code needs from fracture.exe. See game_api.h.

#include "fracture_pch.h"

#include <rex/ppc/func.h>

#include "fracture_frame_rate.h"
#include "game_api.h"

#if defined(_WIN32)
#define FRACTURE_GAME_EXPORT extern "C" __declspec(dllexport)
#else
#define FRACTURE_GAME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

const fracture::game::HostApi* g_host = nullptr;

}  // namespace

FRACTURE_GAME_EXPORT const rex::PPCImageInfo* FractureGame_ImageInfo() {
  return &PPCImageConfig;
}

// Called by fracture.exe right after loading the DLL, before the guest runs.
FRACTURE_GAME_EXPORT bool FractureGame_SetHost(const fracture::game::HostApi* host) {
  if (!host || host->api_version != fracture::game::kApiVersion || !host->xam_input_get_state ||
      !host->frame_interval) {
    return false;
  }
  g_host = host;
  return true;
}

// The recompiled code calls this symbol directly (it is not dllimport), so a
// definition here wins over the runtime's import library: every controller
// read of the game goes through fracture.exe's filter.
extern "C" REX_FUNC(__imp__XamInputGetState) {
  g_host->xam_input_get_state(ctx, base);
}

namespace fracture::frame_rate {

// For the swap hooks (src/game/swap_interval.h); the cvar lives in the exe.
uint32_t Interval() {
  return g_host->frame_interval();
}

}  // namespace fracture::frame_rate
