// fracture - frame rate above the original 30 fps.
//
// The game's Direct3D presents with D3DPRESENT_INTERVAL_TWO: each frame is
// queued for the second vblank after the previous one, which is 30 fps at
// the guest's 60 Hz. The fracture_frame_rate cvar swaps in a shorter interval
// for that one step, so the guest vblank (and the timers the game counts in
// vblanks) keeps its 60 Hz.

#pragma once

namespace fracture::frame_rate {

// Reads the fracture_frame_rate cvar and follows its changes. Call before
// the guest starts.
void Init();

}  // namespace fracture::frame_rate
