// fracture - the frame rate hook's work, shared by both releases' hooks.
//
// The game's Direct3D (linked into default.xex) queues each frame for a
// vblank in one swap step; each release's hooks wrap that function and call
// this with the original.

#pragma once

#include <rex/ppc/context.h>

#include <cstdint>

#include "fracture_frame_rate.h"

namespace fracture::game {

// D3D device field holding D3DPRESENT_PARAMETERS::PresentationInterval (the
// same offset in both releases). The swap step maps it to a vblank count
// (ONE -> 1, TWO -> 2, THREE -> 3, IMMEDIATE -> 0) and queues the frame
// with it.
inline constexpr uint32_t kPresentIntervalOffset = 13596;

inline uint32_t LoadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

inline void StoreBE32(uint8_t* p, uint32_t value) {
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

// Runs the swap step `original` (r3 = device) with the interval of the
// fracture_frame_rate cvar. It is replaced only for this call and restored
// right after, so the rest of the game keeps reading its own value.
inline void SwapWithInterval(PPCContext& ctx, uint8_t* base,
                             void (*original)(PPCContext&, uint8_t*)) {
  const uint32_t interval = fracture::frame_rate::Interval();
  if (interval == 0) {
    original(ctx, base);
    return;
  }
  uint8_t* field = base + ctx.r3.u32 + kPresentIntervalOffset;
  const uint32_t game_interval = LoadBE32(field);
  StoreBE32(field, interval);
  original(ctx, base);
  StoreBE32(field, game_interval);
}

}  // namespace fracture::game
