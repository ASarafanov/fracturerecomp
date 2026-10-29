// fracture - frame rate above the original 30 fps.
//
// The game's Direct3D presents with D3DPRESENT_INTERVAL_TWO: each frame is
// queued for the second vblank after the previous one, which is 30 fps at
// the guest's 60 Hz. The fracture_frame_rate cvar swaps in a shorter interval
// for that one step, so the guest vblank (and the timers the game counts in
// vblanks) keeps its 60 Hz.

#pragma once

#include <cstdint>

namespace fracture::frame_rate {

// Reads the fracture_frame_rate cvar and follows its changes. Call before
// the guest starts.
void Init();

// The D3DPRESENT_INTERVAL_* value the game's swap should use instead of its
// own, or 0 to leave the game's alone. Read by the swap hook of each
// release's recompiled code (src/game/), on guest threads.
uint32_t Interval();

}  // namespace fracture::frame_rate
